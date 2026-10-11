import sys
import os
import re
import json
import subprocess
from datetime import datetime

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

def run_ps1_extract(port_name="COM6"):
    """
    Connects to device over USB serial, requests GET_BATTERY_LOG,
    buffers all received chunks, and closes the port cleanly.
    """
    ps1_code = f"""
    $port = New-Object System.IO.Ports.SerialPort '{port_name}', 115200
    $port.ReadTimeout = 1000
    $port.WriteTimeout = 1000
    $port.DtrEnable = $false
    $port.RtsEnable = $false
    $port.ReadBufferSize = 65536

    try {{
        $port.Open()
        Start-Sleep -Milliseconds 300
        $port.DiscardInBuffer()

        $cmd = [System.Text.Encoding]::ASCII.GetBytes("GET_BATTERY_LOG`n")
        $port.Write($cmd, 0, $cmd.Length)

        $ms = New-Object System.IO.MemoryStream
        $buffer = New-Object byte[] 4096
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $lastRead = [System.Diagnostics.Stopwatch]::StartNew()

        while ($sw.ElapsedMilliseconds -lt 15000) {{
            if ($port.BytesToRead -gt 0) {{
                $count = $port.Read($buffer, 0, [Math]::Min($port.BytesToRead, $buffer.Length))
                if ($count -gt 0) {{
                    $ms.Write($buffer, 0, $count)
                    $lastRead.Restart()
                    $currentText = [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
                    if ($currentText -match "--- END BATTERY LOG ---") {{
                        break
                    }}
                }}
            }} else {{
                Start-Sleep -Milliseconds 30
                if ($ms.Length -gt 0 -and $lastRead.ElapsedMilliseconds -gt 2500) {{
                    break
                }}
            }}
        }}
        [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
    }} finally {{
        if ($port.IsOpen) {{
            $port.Close()
        }}
    }}
    """
    cmd = ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps1_code]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT_DIR)
    if proc.returncode != 0 and not proc.stdout.strip():
        cmd[0] = "powershell"
        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT_DIR)

    raw_output = proc.stdout.strip()
    return raw_output

def normalize_and_parse_logs(raw_text):
    """
    Normalizes log chunks, supports legacy 6-column, legacy 8-column (with heap),
    and current 8-column entries:
    Legacy 6:    timestamp,voltage,pct,charging,screenOn,wifiActive
    Legacy Heap: timestamp,voltage,pct,charging,brightness,wifiState,bleState,freeHeap (>1000)
    Current:     timestamp,voltage,pct,charging,brightness,wifiState,bleState,cpuFreqMhz (<=1000)
    """
    p8 = re.compile(r'(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}|up_\w+),(\d+\.\d{2,3}),(\d{1,3}),([01]),(\d{1,3}),([0-2]),([0-2]),(\d+)')
    p6 = re.compile(r'(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}|up_\w+),(\d+\.\d{2,3}),(\d{1,3}),([01]),([01]),([01])')

    entries = {}
    for m in p8.finditer(raw_text):
        t_str, v_str, pct_str, chg_str, b_str, w_str, ble_str, val8_str = m.groups()
        try:
            v = float(v_str)
            pct = int(pct_str)
            chg = int(chg_str)
            brightness = int(b_str)
            wifi_state = int(w_str)
            ble_state = int(ble_str)
            val8 = int(val8_str)
            # ESP32 CPU freq is typically 80, 160, 240 MHz; freeHeap is >10000 bytes
            cpu_freq = val8 if val8 <= 1000 else 0
            free_heap = val8 if val8 > 1000 else 0
            if 2.8 <= v <= 4.4 and 0 <= pct <= 100:
                entries[t_str] = {
                    "timestamp": t_str,
                    "voltage": round(v, 3),
                    "pct": pct,
                    "charging": chg,
                    "brightness": brightness,
                    "screen_on": 1 if brightness > 0 else 0,
                    "wifi_state": wifi_state,
                    "ble_state": ble_state,
                    "cpu_freq": cpu_freq,
                    "free_heap": free_heap
                }
        except ValueError:
            continue

    for m in p6.finditer(raw_text):
        t_str, v_str, pct_str, chg_str, scr_str, w_str = m.groups()
        if t_str in entries:
            continue
        try:
            v = float(v_str)
            pct = int(pct_str)
            chg = int(chg_str)
            screen_on = int(scr_str)
            wifi_active = int(w_str)
            if 2.8 <= v <= 4.4 and 0 <= pct <= 100:
                entries[t_str] = {
                    "timestamp": t_str,
                    "voltage": round(v, 3),
                    "pct": pct,
                    "charging": chg,
                    "brightness": 255 if screen_on else 0,
                    "screen_on": screen_on,
                    "wifi_state": 2 if wifi_active else 0,
                    "ble_state": 1,
                    "cpu_freq": 240,
                    "free_heap": 0
                }
        except ValueError:
            continue

    sorted_records = sorted(entries.values(), key=lambda x: x["timestamp"])
    return sorted_records

def analyze_telemetry(records):
    """
    Calculates discharge statistics, runtime, active vs sleep drain rates,
    and milestone checkpoints.
    """
    dated = [r for r in records if not r["timestamp"].startswith("up_")]
    if not dated:
        return {"total_records": len(records), "error": "No timestamped records available."}

    peak = max(dated, key=lambda x: x["voltage"])
    peak_idx = dated.index(peak)
    discharge = dated[peak_idx:]

    t_start = datetime.strptime(discharge[0]["timestamp"], "%Y-%m-%d %H:%M:%S")
    t_end = datetime.strptime(discharge[-1]["timestamp"], "%Y-%m-%d %H:%M:%S")
    duration_sec = (t_end - t_start).total_seconds()
    duration_hours = max(0.01, duration_sec / 3600.0)

    scr_off = [d for d in discharge if d["screen_on"] == 0]
    scr_on = [d for d in discharge if d["screen_on"] == 1]

    milestones = [100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 5, 2, 0]
    milestone_results = []
    seen = set()
    for m in milestones:
        found = next((d for d in discharge if d["pct"] <= m), None)
        if found and found["pct"] not in seen:
            seen.add(found["pct"])
            elapsed = (datetime.strptime(found["timestamp"], "%Y-%m-%d %H:%M:%S") - t_start).total_seconds() / 3600.0
            milestone_results.append({
                "target_pct": found["pct"],
                "voltage": found["voltage"],
                "elapsed_hours": round(elapsed, 2),
                "timestamp": found["timestamp"],
                "screen_on": found.get("screen_on", 1),
                "brightness": found.get("brightness", 255 if found.get("screen_on") else 0),
                "wifi_state": found.get("wifi_state", 0),
                "ble_state": found.get("ble_state", 1),
                "cpu_freq": found.get("cpu_freq", 0),
                "free_heap": found.get("free_heap", 0)
            })

    pct_dropped = discharge[0]["pct"] - discharge[-1]["pct"]
    overall_drain_per_hour = round(pct_dropped / duration_hours, 2)

    return {
        "total_records": len(records),
        "discharge_records": len(discharge),
        "peak_voltage": peak["voltage"],
        "peak_time": peak["timestamp"],
        "min_voltage": discharge[-1]["voltage"],
        "min_time": discharge[-1]["timestamp"],
        "start_time": discharge[0]["timestamp"],
        "end_time": discharge[-1]["timestamp"],
        "duration_hours": round(duration_hours, 2),
        "duration_formatted": f"{int(duration_hours)}h {int((duration_sec % 3600) // 60)}m",
        "overall_drain_per_hour": overall_drain_per_hour,
        "screen_off_samples": len(scr_off),
        "screen_on_samples": len(scr_on),
        "milestones": milestone_results
    }

def run_pipeline(port_name="COM6", raw_source=None):
    """
    Executes the ingestion, normalization, analysis, and dashboard update.
    """
    print(f"[*] Step 1/4: Ingesting logs from device (Port: {port_name})...")
    if raw_source:
        raw_text = raw_source
    else:
        raw_text = run_ps1_extract(port_name)

    if not raw_text:
        print("[!] Port read failed or empty response received.")
        return None

    print(f"[*] Step 2/4: Normalizing, splitting and validating entries...")
    records = normalize_and_parse_logs(raw_text)
    if not records:
        print("[!] No valid records parsed from stream.")
        return None

    data_json_path = os.path.join(ROOT_DIR, "battery_data.json")
    if os.path.exists(data_json_path):
        try:
            with open(data_json_path, "r", encoding="utf-8") as f:
                existing = json.load(f)
            merged = {d["timestamp"]: d for d in existing}
            for r in records:
                merged[r["timestamp"]] = r
            records = sorted(merged.values(), key=lambda x: x["timestamp"])
        except Exception:
            pass

    with open(data_json_path, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2)
    print(f"[+] Total stored records: {len(records)} saved to {data_json_path}")

    print("[*] Step 3/4: Analyzing telemetry and computing insights...")
    analysis = analyze_telemetry(records)

    analysis_json_path = os.path.join(ROOT_DIR, "utils", "analysis", "latest_analysis.json")
    with open(analysis_json_path, "w", encoding="utf-8") as f:
        json.dump(analysis, f, indent=2)

    print(f"[*] Step 4/4: Generating updated battery.html dashboard...")
    generate_dashboard(records, analysis)

    print(f"[+] Pipeline complete!")
    print(f"    - Discharge Runtime: {analysis.get('duration_formatted', 'N/A')}")
    print(f"    - Voltage Span:      {analysis.get('min_voltage')}V -> {analysis.get('peak_voltage')}V")
    print(f"    - Dashboard:         battery.html")
    return {"records": records, "analysis": analysis}

def generate_dashboard(records, analysis):
    """
    Generates battery.html with Web Serial API ingestion and clear buttons,
    real-time Chart.js dual-axis visualization, dynamic stats cards,
    and dynamic milestones table matching active chart range.
    """
    dated = [r for r in records if not r["timestamp"].startswith("up_")]

    # Find the single biggest continuous discharge window across all history:
    # A continuous discharge starts at a local charge peak and runs until next charging or end
    longest_discharge = []
    current_discharge = []
    for d in dated:
        if d.get("charging", 0) == 1:
            if len(current_discharge) > len(longest_discharge):
                longest_discharge = current_discharge
            current_discharge = []
        else:
            current_discharge.append(d)
    if len(current_discharge) > len(longest_discharge):
        longest_discharge = current_discharge

    if not longest_discharge:
        longest_discharge = dated

    t_discharge_start = longest_discharge[0]["timestamp"].replace(" ", "T")
    t_discharge_end = longest_discharge[-1]["timestamp"].replace(" ", "T")

    # Serialize normalized records into JSON string for client-side reactivity
    client_dataset = []
    for d in dated:
        client_dataset.append({
            "t": d["timestamp"],
            "x": d["timestamp"].replace(" ", "T"),
            "v": d["voltage"],
            "p": d["pct"],
            "chg": d.get("charging", 0),
            "scr": d.get("screen_on", 1 if d.get("brightness", 255) > 0 else 0),
            "b": d.get("brightness", 255 if d.get("screen_on") else 0),
            "w": d.get("wifi_state", 0),
            "ble": d.get("ble_state", 1),
            "freq": d.get("cpu_freq", 0),
            "heap": d.get("free_heap", 0)
        })

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sugarota Battery Telemetry & Analysis</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/date-fns"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns"></script>
    <style>
        :root {{
            --bg-primary: #0a0e17;
            --bg-card: #131b2e;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --accent-v: #38bdf8;
            --accent-pct: #34d399;
            --accent-amber: #fbbf24;
            --accent-red: #f87171;
            --border: #23314d;
            --btn-bg: #1e293b;
            --btn-hover: #334155;
            --accent-primary: #0284c7;
        }}
        * {{
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        }}
        body {{
            background: var(--bg-primary);
            color: var(--text-main);
            padding: 2rem;
            min-height: 100vh;
        }}
        .header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border);
            padding-bottom: 1.5rem;
            margin-bottom: 2rem;
            flex-wrap: wrap;
            gap: 1rem;
        }}
        .title h1 {{
            font-size: 1.75rem;
            font-weight: 700;
            letter-spacing: -0.025em;
        }}
        .title p {{
            color: var(--text-muted);
            margin-top: 0.35rem;
            font-size: 0.95rem;
        }}
        .action-bar {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            flex-wrap: wrap;
        }}
        .btn {{
            background: var(--btn-bg);
            color: var(--text-main);
            border: 1px solid var(--border);
            padding: 0.6rem 1.1rem;
            border-radius: 0.5rem;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 0.5rem;
            transition: all 0.2s ease;
        }}
        .btn:hover {{
            background: var(--btn-hover);
            border-color: #475569;
        }}
        .btn-primary {{
            background: #0284c7;
            border-color: #38bdf8;
            color: white;
            box-shadow: 0 0 15px rgba(56, 189, 248, 0.25);
        }}
        .btn-primary:hover {{
            background: #0369a1;
        }}
        .btn-danger {{
            background: rgba(239, 68, 68, 0.15);
            border-color: rgba(239, 68, 68, 0.4);
            color: var(--accent-red);
        }}
        .btn-danger:hover {{
            background: rgba(239, 68, 68, 0.3);
            border-color: var(--accent-red);
        }}
        .btn:disabled {{
            opacity: 0.6;
            cursor: not-allowed;
        }}
        .stats-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 1.25rem;
            margin-bottom: 2rem;
        }}
        .stat-card {{
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 0.75rem;
            padding: 1.25rem;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2);
        }}
        .stat-label {{
            font-size: 0.8rem;
            color: var(--text-muted);
            text-transform: uppercase;
            letter-spacing: 0.05em;
            font-weight: 600;
        }}
        .stat-value {{
            font-size: 1.75rem;
            font-weight: 700;
            margin: 0.4rem 0 0.2rem;
            color: var(--text-main);
        }}
        .stat-desc {{
            font-size: 0.85rem;
            color: var(--text-muted);
        }}
        .chart-container {{
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 0.75rem;
            padding: 1.5rem;
            margin-bottom: 2rem;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2);
            position: relative;
            height: 540px;
        }}
        .chart-controls {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.5rem;
        }}
        .segmented {{
            display: flex;
            background: #0a0e17;
            border: 1px solid var(--border);
            border-radius: 0.5rem;
            padding: 0.2rem;
        }}
        .segmented button {{
            background: transparent;
            border: none;
            color: var(--text-muted);
            padding: 0.4rem 0.8rem;
            border-radius: 0.35rem;
            font-size: 0.8rem;
            font-weight: 600;
            cursor: pointer;
        }}
        .segmented button.active {{
            background: #1e293b;
            color: var(--text-main);
        }}
        .segmented button:disabled {{
            opacity: 0.4;
            cursor: not-allowed;
            pointer-events: none;
        }}
        .grid-2 {{
            display: grid;
            grid-template-columns: 2fr 1fr;
            gap: 1.5rem;
        }}
        @media (max-width: 900px) {{
            .grid-2 {{
                grid-template-columns: 1fr;
            }}
        }}
        .panel {{
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: 0.75rem;
            padding: 1.5rem;
        }}
        .panel h3 {{
            font-size: 1.15rem;
            margin-bottom: 1rem;
        }}
        .milestones-table {{
            width: 100%;
            border-collapse: collapse;
            font-size: 0.85rem;
        }}
        .milestones-table th, .milestones-table td {{
            padding: 0.55rem 0.6rem;
            text-align: left;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
        }}
        .milestones-table th {{
            color: var(--text-muted);
            font-weight: 600;
            text-transform: uppercase;
            font-size: 0.75rem;
        }}
        .badge {{
            display: inline-block;
            padding: 0.2rem 0.45rem;
            border-radius: 0.3rem;
            font-size: 0.75rem;
            font-weight: 600;
        }}
        .badge-on {{ background: rgba(56, 189, 248, 0.15); color: #38bdf8; }}
        .badge-off {{ background: rgba(148, 163, 184, 0.15); color: #94a3b8; }}
        .badge-active {{ background: rgba(52, 211, 153, 0.15); color: #34d399; }}
        .badge-warn {{ background: rgba(251, 191, 36, 0.15); color: #fbbf24; }}
        .badge-chg {{ background: rgba(245, 158, 11, 0.2); color: #f59e0b; border: 1px solid rgba(245, 158, 11, 0.4); }}
        .badge-dischg {{ background: rgba(56, 189, 248, 0.15); color: #38bdf8; }}
        #pipelineStatus {{
            font-size: 0.85rem;
            color: var(--accent-amber);
            display: none;
            align-items: center;
            gap: 0.5rem;
        }}
        .spinner {{
            width: 14px;
            height: 14px;
            border: 2px solid rgba(251, 191, 36, 0.3);
            border-top-color: var(--accent-amber);
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
        }}
        @keyframes spin {{ to {{ transform: rotate(360deg); }} }}
    </style>
</head>
<body>
    <div class="header">
        <div class="title">
            <h1>Sugarota Battery Analysis Dashboard</h1>
            <p>Empirical Discharge Curve, Telemetry Normalization & Calibration Model</p>
        </div>
        <div class="action-bar">
            <span id="pipelineStatus"><span class="spinner"></span> Working over USB...</span>
            <button class="btn btn-primary" id="btnIngest" onclick="triggerWebSerialPipeline('GET')">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
                Extract from Device (USB)
            </button>
            <button class="btn btn-danger" id="btnClear" onclick="triggerWebSerialPipeline('CLEAR')">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path></svg>
                Clear battery logs on the device
            </button>
            <a href="installer.html" class="btn" style="text-decoration: none;" title="Return to Sugarota Web Installer">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5M12 19l-7-7 7-7"/></svg>
                Web Installer
            </a>
        </div>
    </div>

    <div class="stats-grid">
        <div class="stat-card">
            <div class="stat-label">Discharge Runtime</div>
            <div class="stat-value" id="cardRuntime" style="color: #38bdf8;">--</div>
            <div class="stat-desc" id="cardVoltageSpan">--</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Visible Data Points</div>
            <div class="stat-value" id="cardPoints" style="color: #34d399;">--</div>
            <div class="stat-desc" id="cardPointsDesc">--</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Discharge Drain Rate</div>
            <div class="stat-value" id="cardDrainRate" style="color: #a78bfa;">--</div>
            <div class="stat-desc" id="cardDrainDesc">--</div>
        </div>
        <div class="stat-card">
            <div class="stat-label">Cutoff Guard Threshold</div>
            <div class="stat-value" style="color: #fbbf24;">3.00V - 3.02V</div>
            <div class="stat-desc">0% mapped to 3.02V for safe shutdown</div>
        </div>
    </div>

    <div class="chart-container">
        <div class="chart-controls">
            <div class="segmented">
                <button id="viewAllBtn" class="active" onclick="switchView('all')">Full History (All Datapoints)</button>
                <button id="viewDischargeBtn" onclick="switchView('discharge')">Biggest Discharge Window</button>
            </div>
            <span style="font-size: 0.85rem; color: var(--text-muted);" id="rangeLabel">Points: Real Log Seconds &bull; Fixed Grid: 30m</span>
        </div>
        <div style="position: relative; height: calc(100% - 40px); width: 100%;">
            <canvas id="batteryChart"></canvas>
        </div>
    </div>

    <div class="grid-2" id="bottomGrid">
        <div class="panel" id="milestonesPanel">
            <h3 id="milestonesHeading">Discharge Milestones (Biggest Discharge Window)</h3>
            <table class="milestones-table">
                <thead>
                    <tr>
                        <th>Battery %</th>
                        <th>Voltage</th>
                        <th>Elapsed Time</th>
                        <th>Timestamp</th>
                        <th>Status</th>
                        <th>Screen</th>
                        <th>Brightness</th>
                        <th>Wi-Fi</th>
                        <th>BLE</th>
                        <th>CPU / Heap</th>
                    </tr>
                </thead>
                <tbody id="milestonesBody">
                </tbody>
            </table>
        </div>

        <div class="panel">
            <h3>Calibration Model Findings</h3>
            <ul style="padding-left: 1.25rem; color: var(--text-muted); line-height: 1.8; font-size: 0.9rem;">
                <li><strong>Empirical Remap:</strong> Voltage knee mapped from 80% to 0% based on {analysis.get('duration_formatted', '20.7h')} actual drain data.</li>
                <li><strong>Low-Battery Guard:</strong> 0% aligned to <strong>3.02V</strong> so percentage reaches 0% before firmware shutdown (&lt;3.00V).</li>
                <li><strong>Dynamic Telemetry:</strong> Milestone checkpoints, drain slope, and highlights update dynamically based on the active zoom/view window.</li>
            </ul>
        </div>
    </div>

    <script>
        const initialDataset = {json.dumps(client_dataset)};
        const cachedRaw = localStorage.getItem('sugarota_battery_dataset');
        let allDataset = cachedRaw ? JSON.parse(cachedRaw) : initialDataset;

        let dischargeStartIso = '{t_discharge_start}';
        let dischargeEndIso = '{t_discharge_end}';

        let chart = null;
        let currentView = 'all';

        const wifiLabels = ['Sleep/Off', 'Connecting', 'Connected/Active'];
        const bleLabels = ['Uninit', 'Advertising', 'Connected'];

        function getVisibleData(minX, maxX) {{
            if (!minX || !maxX) return allDataset;
            const t0 = new Date(minX).getTime();
            const t1 = new Date(maxX).getTime();
            return allDataset.filter(d => {{
                const dt = new Date(d.x).getTime();
                return dt >= t0 && dt <= t1;
            }});
        }}

        function getContiguousDischargeSegments(dataset) {{
            const segments = [];
            let current = [];
            for (let i = 0; i < dataset.length; i++) {{
                const pt = dataset[i];
                const prev = current.length ? current[current.length - 1] : null;
                // Exclude explicit charging flag or clear voltage increase between points
                const isChg = pt.chg === 1 || (prev && pt.v > prev.v + 0.05);
                if (isChg) {{
                    if (current.length > 1) segments.push(current);
                    current = [];
                }} else {{
                    current.push(pt);
                }}
            }}
            if (current.length > 1) segments.push(current);
            return segments;
        }}

        function findLongestDischarge(dataset) {{
            const segments = getContiguousDischargeSegments(dataset);
            if (!segments.length) return [];
            return segments.reduce((longest, curr) => curr.length > longest.length ? curr : longest, segments[0]);
        }}

        function updateDynamicComponents(minX, maxX) {{
            const visible = getVisibleData(minX, maxX);
            const longestDischarge = findLongestDischarge(allDataset);
            const hasDischarge = longestDischarge && longestDischarge.length >= 2;
            const dischargeBtn = document.getElementById('viewDischargeBtn');
            if (dischargeBtn) {{
                dischargeBtn.disabled = !hasDischarge;
                dischargeBtn.title = hasDischarge ? 'View biggest discharge window' : 'No discharge data points available (Device charging or idle)';
            }}

            if (!visible.length) {{
                document.getElementById('cardRuntime').innerText = '0h 0m';
                document.getElementById('cardVoltageSpan').innerText = '--';
                document.getElementById('cardPoints').innerText = '0';
                document.getElementById('cardDrainRate').innerText = '~0.00%/h';
                document.getElementById('cardDrainDesc').innerText = 'No data points';
                document.getElementById('milestonesBody').innerHTML = '<tr><td colspan="10" style="text-align:center; padding:1.5rem; color:var(--text-muted);">No data points recorded</td></tr>';
                return;
            }}

            const isFullHistory = !minX && !maxX;

            let runtimeMs = 0;
            let netPctDrop = 0;
            let displayMaxV = 0;
            let displayMinV = 0;
            let hasValidDischargePeriod = false;

            if (isFullHistory) {{
                // In Full History: consider ONLY discharge segments, skipping charging
                const dischargeSegments = getContiguousDischargeSegments(visible);
                let dischargeVList = [];

                dischargeSegments.forEach(seg => {{
                    const segT0 = new Date(seg[0].x).getTime();
                    const segT1 = new Date(seg[seg.length - 1].x).getTime();
                    runtimeMs += Math.max(0, segT1 - segT0);
                    netPctDrop += Math.max(0, seg[0].p - seg[seg.length - 1].p);
                    seg.forEach(pt => dischargeVList.push(pt.v));
                }});

                if (dischargeSegments.length > 0 && dischargeVList.length > 0) {{
                    hasValidDischargePeriod = true;
                    displayMaxV = Math.max(...dischargeVList);
                    displayMinV = Math.min(...dischargeVList);
                }} else {{
                    displayMaxV = Math.max(...visible.map(d => d.v));
                    displayMinV = Math.min(...visible.map(d => d.v));
                }}
            }} else {{
                // Dedicated window
                const t0 = new Date(visible[0].x).getTime();
                const t1 = new Date(visible[visible.length - 1].x).getTime();
                runtimeMs = Math.max(0, t1 - t0);
                netPctDrop = Math.max(0, visible[0].p - visible[visible.length - 1].p);
                const vList = visible.map(d => d.v);
                displayMaxV = Math.max(...vList);
                displayMinV = Math.min(...vList);
                hasValidDischargePeriod = visible.length >= 2 && !visible.every(d => d.chg === 1);
            }}

            if (!hasValidDischargePeriod || runtimeMs <= 0) {{
                document.getElementById('cardRuntime').innerText = '0h 0m';
                document.getElementById('cardVoltageSpan').innerText = `${{displayMinV.toFixed(3)}}V - ${{displayMaxV.toFixed(3)}}V (Charging / Idle)`;
                document.getElementById('cardDrainRate').innerText = '~0.00%/h';
                document.getElementById('cardDrainDesc').innerText = 'No discharge periods detected (Device charging)';
            }} else {{
                const durHours = runtimeMs / 3600000;
                const h = Math.floor(durHours);
                const m = Math.floor((durHours - h) * 60);

                document.getElementById('cardRuntime').innerText = `${{h}}h ${{m}}m`;
                document.getElementById('cardVoltageSpan').innerText = `${{displayMaxV.toFixed(3)}}V down to ${{displayMinV.toFixed(3)}}V`;

                const drainRate = durHours > 0 ? (netPctDrop / durHours).toFixed(2) : '0.00';
                document.getElementById('cardDrainRate').innerText = `~${{drainRate}}%/h`;
                document.getElementById('cardDrainDesc').innerText = isFullHistory
                    ? `Net -${{netPctDrop}}% drop across discharge periods`
                    : `${{visible[0].p}}% to ${{visible[visible.length - 1].p}}% over visible window`;
            }}

            document.getElementById('cardPoints').innerText = visible.length;
            document.getElementById('cardPointsDesc').innerText = `Out of ${{allDataset.length}} total logged samples`;

            // Milestones panel appears when Full History tab is active
            const milestonesPanel = document.getElementById('milestonesPanel');
            const bottomGrid = document.getElementById('bottomGrid');
            if (milestonesPanel && bottomGrid) {{
                if (isFullHistory) {{
                    milestonesPanel.style.display = 'block';
                    bottomGrid.style.gridTemplateColumns = '2fr 1fr';
                }} else {{
                    milestonesPanel.style.display = 'none';
                    bottomGrid.style.gridTemplateColumns = '1fr';
                }}
            }}

            // Milestone Table calculation: biggest discharge window
            const milestonesSource = longestDischarge;
            const headingEl = document.getElementById('milestonesHeading');
            if (headingEl) {{
                headingEl.innerText = 'Discharge Milestones (Biggest Discharge Window)';
            }}

            const tbody = document.getElementById('milestonesBody');
            tbody.innerHTML = '';

            if (!milestonesSource || milestonesSource.length < 2) {{
                tbody.innerHTML = '<tr><td colspan="10" style="text-align:center; padding:1.5rem; color:var(--text-muted);">No discharge window recorded (all samples are charging or insufficient)</td></tr>';
                return;
            }}

            const milestones = [100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 5, 2, 0];
            const seen = new Set();
            const mStartT = new Date(milestonesSource[0].x).getTime();

            milestones.forEach(target => {{
                const found = milestonesSource.find(d => d.p <= target);
                if (found && !seen.has(found.p)) {{
                    seen.add(found.p);
                    const elapsedH = ((new Date(found.x).getTime() - mStartT) / 3600000).toFixed(2);
                    const chgBadge = found.chg === 1
                        ? '<span class="badge badge-chg">Charging</span>'
                        : '<span class="badge badge-dischg">Discharge</span>';
                    const scrBadge = found.scr ? '<span class="badge badge-on">ON</span>' : '<span class="badge badge-off">OFF</span>';
                    const wifiBadge = found.w === 2 ? '<span class="badge badge-active">Active</span>' : (found.w === 1 ? '<span class="badge badge-warn">Connecting</span>' : '<span class="badge badge-off">Off</span>');
                    const bleBadge = found.ble === 2 ? '<span class="badge badge-active">Connected</span>' : (found.ble === 1 ? '<span class="badge badge-warn">Adv</span>' : '<span class="badge badge-off">Off</span>');
                    let extraStr = '-';
                    if (found.freq) {{
                        extraStr = `${{found.freq}} MHz`;
                    }} else if (found.heap) {{
                        extraStr = `${{Math.round(found.heap / 1024)}} KB`;
                    }}

                    const row = document.createElement('tr');
                    row.innerHTML = `
                        <td><strong>${{found.p}}%</strong></td>
                        <td>${{found.v.toFixed(3)}} V</td>
                        <td>+${{elapsedH}} h</td>
                        <td>${{found.t}}</td>
                        <td>${{chgBadge}}</td>
                        <td>${{scrBadge}}</td>
                        <td>${{found.b}}/255</td>
                        <td>${{wifiBadge}}</td>
                        <td>${{bleBadge}}</td>
                        <td>${{extraStr}}</td>
                    `;
                    tbody.appendChild(row);
                }}
            }});
        }}

        function initChart(minX, maxX) {{
            const ctx = document.getElementById('batteryChart').getContext('2d');
            if (chart) chart.destroy();

            const vPoints = allDataset.map(d => ({{ x: d.x, y: d.v, raw: d }}));
            const pPoints = allDataset.map(d => ({{ x: d.x, y: d.p, raw: d }}));

            chart = new Chart(ctx, {{
                type: 'line',
                data: {{
                    datasets: [
                        {{
                            label: 'Battery Voltage (V)',
                            data: vPoints,
                            borderColor: '#38bdf8',
                            backgroundColor: 'rgba(56, 189, 248, 0.1)',
                            yAxisID: 'yVoltage',
                            borderWidth: 2,
                            tension: 0.15,
                            pointRadius: 2.5,
                            pointHoverRadius: 5.5,
                            pointBackgroundColor: '#38bdf8'
                        }},
                        {{
                            label: 'Reported Battery (%)',
                            data: pPoints,
                            borderColor: '#34d399',
                            backgroundColor: 'rgba(52, 211, 153, 0.1)',
                            yAxisID: 'yPct',
                            borderWidth: 2,
                            tension: 0.15,
                            pointRadius: 2.5,
                            pointHoverRadius: 5.5,
                            pointBackgroundColor: '#34d399'
                        }}
                    ]
                }},
                options: {{
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: {{
                        mode: 'nearest',
                        axis: 'x',
                        intersect: false
                    }},
                    scales: {{
                        x: {{
                            type: 'time',
                            time: {{
                                unit: 'minute',
                                stepSize: 30,
                                displayFormats: {{ minute: 'HH:mm', hour: 'HH:mm' }},
                                tooltipFormat: 'yyyy-MM-dd HH:mm:ss'
                            }},
                            min: minX,
                            max: maxX,
                            ticks: {{
                                color: '#94a3b8',
                                autoSkip: false,
                                maxRotation: 45,
                                minRotation: 45,
                                font: {{ size: 11 }}
                            }},
                            grid: {{ color: '#23314d' }}
                        }},
                        yVoltage: {{
                            type: 'linear',
                            position: 'left',
                            min: 2.9,
                            max: 4.3,
                            ticks: {{
                                color: '#38bdf8',
                                stepSize: 0.2,
                                callback: val => val.toFixed(2) + ' V'
                            }},
                            grid: {{ color: '#131b2e' }},
                            title: {{ display: true, text: 'Voltage (Volts)', color: '#38bdf8', font: {{ size: 13, weight: '600' }} }}
                        }},
                        yPct: {{
                            type: 'linear',
                            position: 'right',
                            min: 0,
                            max: 105,
                            ticks: {{
                                color: '#34d399',
                                stepSize: 10,
                                callback: val => val + ' %'
                            }},
                            grid: {{ drawOnChartArea: false }},
                            title: {{ display: true, text: 'Battery (%)', color: '#34d399', font: {{ size: 13, weight: '600' }} }}
                        }}
                    }},
                    plugins: {{
                        tooltip: {{
                            callbacks: {{
                                title: items => {{
                                    if (!items.length) return '';
                                    const d = new Date(items[0].parsed.x);
                                    const pad = n => String(n).padStart(2, '0');
                                    return `${{d.getFullYear()}}-${{pad(d.getMonth()+1)}}-${{pad(d.getDate())}} ${{pad(d.getHours())}}:${{pad(d.getMinutes())}}:${{pad(d.getSeconds())}}`;
                                }},
                                label: ctx => {{
                                    const pt = ctx.raw.raw;
                                    if (ctx.datasetIndex === 0) {{
                                        return ` Voltage: ${{ctx.parsed.y.toFixed(3)}} V`;
                                    }}
                                    return [
                                        ` Battery: ${{ctx.parsed.y}}%`,
                                        ` Status: ${{pt.chg === 1 ? '⚡ Charging' : '🔋 Discharging'}}`,
                                        ` Screen: ${{pt.scr ? 'ON' : 'OFF'}} (Brightness: ${{pt.b}}/255)`,
                                        ` Wi-Fi: ${{wifiLabels[pt.w] || 'Off'}}`,
                                        ` BLE: ${{bleLabels[pt.ble] || 'Off'}}`,
                                        pt.freq ? ` CPU: ${{pt.freq}} MHz` : (pt.heap ? ` Free Heap: ${{Math.round(pt.heap / 1024)}} KB` : '')
                                    ].filter(Boolean);
                                }}
                            }}
                        }},
                        legend: {{
                            labels: {{ color: '#f8fafc', font: {{ size: 13, weight: '500' }}, padding: 16 }}
                        }}
                    }}
                }}
            }});

            updateDynamicComponents(minX, maxX);
        }}

        function switchView(mode) {{
            if (mode === 'discharge') {{
                const longestSeg = findLongestDischarge(allDataset);
                if (!longestSeg || longestSeg.length < 2) return;
                dischargeStartIso = longestSeg[0].x;
                dischargeEndIso = longestSeg[longestSeg.length - 1].x;
            }}
            currentView = mode;
            document.getElementById('viewDischargeBtn').classList.toggle('active', mode === 'discharge');
            document.getElementById('viewAllBtn').classList.toggle('active', mode === 'all');
            if (mode === 'discharge') {{
                initChart(dischargeStartIso, dischargeEndIso);
            }} else {{
                initChart(undefined, undefined);
            }}
        }}

        // Client-side Web Serial Ingestion & Clear Pipeline
        async function triggerWebSerialPipeline(action) {{
            if (!navigator.serial) {{
                alert('Web Serial API is not supported in this browser. Please use Chrome/Edge or run "just battery-pipeline" in terminal.');
                return;
            }}

            const btnIngest = document.getElementById('btnIngest');
            const btnClear = document.getElementById('btnClear');
            const statusEl = document.getElementById('pipelineStatus');
            btnIngest.disabled = true;
            btnClear.disabled = true;
            statusEl.style.display = 'inline-flex';

            let port = null;
            try {{
                port = await navigator.serial.requestPort();
                await port.open({{ baudRate: 115200 }});

                const writer = port.writable.getWriter();
                const encoder = new TextEncoder();

                if (action === 'CLEAR') {{
                    statusEl.innerHTML = '<span class="spinner"></span> Clearing battery logs on device...';
                    await writer.write(encoder.encode("CLEAR_BATTERY_LOG\\n"));
                    writer.releaseLock();
                    await new Promise(r => setTimeout(r, 600));
                    await port.close();
                    localStorage.setItem('sugarota_battery_dataset', JSON.stringify([]));
                    allDataset = [];
                    refreshDashboardData();
                    statusEl.innerText = 'Logs cleared on device! Dashboard reset.';
                    setTimeout(() => {{ statusEl.style.display = 'none'; }}, 3000);
                    return;
                }}

                // Ingestion action
                statusEl.innerHTML = '<span class="spinner"></span> Extracting battery logs over USB...';
                await writer.write(encoder.encode("GET_BATTERY_LOG\\n"));
                writer.releaseLock();

                const reader = port.readable.getReader();
                const decoder = new TextDecoder();
                let accumulated = "";
                const startTime = Date.now();

                while (Date.now() - startTime < 15000) {{
                    const {{ value, done }} = await Promise.race([
                        reader.read(),
                        new Promise(r => setTimeout(() => r({{ value: null, done: false }}), 2500))
                    ]);
                    if (done) break;
                    if (value) {{
                        accumulated += decoder.decode(value);
                        if (accumulated.includes("--- END BATTERY LOG ---")) break;
                    }}
                }}
                reader.releaseLock();
                await port.close();

                statusEl.innerHTML = '<span class="spinner"></span> Processing and refreshing dashboard...';
                const parsedRecords = parseAndNormalizeRawTelemetry(accumulated);

                alert(`Successfully extracted ${{accumulated.length}} bytes of telemetry (${{parsedRecords.length}} data points).`);

                if (parsedRecords.length > 0) {{
                    // Set allDataset to the extracted records from device
                    allDataset = parsedRecords.sort((a, b) => new Date(a.x) - new Date(b.x));
                    localStorage.setItem('sugarota_battery_dataset', JSON.stringify(allDataset));

                    // Re-calculate biggest discharge window
                    const longestSeg = findLongestDischarge(allDataset);
                    if (longestSeg && longestSeg.length > 0) {{
                        dischargeStartIso = longestSeg[0].x;
                        dischargeEndIso = longestSeg[longestSeg.length - 1].x;
                    }}

                    // Refresh chart and panels smoothly in background
                    refreshDashboardData();
                    statusEl.innerText = `Extracted ${{accumulated.length}} bytes (${{parsedRecords.length}} points)`;
                    setTimeout(() => {{ statusEl.style.display = 'none'; }}, 3500);
                }} else {{
                    statusEl.innerText = `Extracted ${{accumulated.length}} bytes (0 records)`;
                    setTimeout(() => {{ statusEl.style.display = 'none'; }}, 3500);
                }}
            }} catch (err) {{
                alert(`Serial connection error: ${{err.message}}`);
            }} finally {{
                if (port && port.readable) {{
                    try {{ await port.close(); }} catch(e) {{}}
                }}
                btnIngest.disabled = false;
                btnClear.disabled = false;
            }}
        }}

        function parseAndNormalizeRawTelemetry(text) {{
            const p8 = /(\\d{{4}}-\\d{{2}}-\\d{{2}}\\s+\\d{{2}}:\\d{{2}}:\\d{{2}}|up_\\w+),(\\d+\\.\\d{{2,3}}),(\\d{{1,3}}),([01]),(\\d{{1,3}}),([0-2]),([0-2]),(\\d+)/g;
            const p6 = /(\\d{{4}}-\\d{{2}}-\\d{{2}}\\s+\\d{{2}}:\\d{{2}}:\\d{{2}}|up_\\w+),(\\d+\\.\\d{{2,3}}),(\\d{{1,3}}),([01]),([01]),([01])/g;
            const map = new Map();

            let match;
            while ((match = p8.exec(text)) !== null) {{
                const [, t_str, v_str, pct_str, chg_str, b_str, w_str, ble_str, val8_str] = match;
                const v = parseFloat(v_str);
                const pct = parseInt(pct_str, 10);
                if (v >= 2.8 && v <= 4.4 && pct >= 0 && pct <= 100 && !t_str.startsWith('up_')) {{
                    const b = parseInt(b_str, 10);
                    const val8 = parseInt(val8_str, 10);
                    const cpuFreq = val8 <= 1000 ? val8 : 0;
                    const freeHeap = val8 > 1000 ? val8 : 0;
                    map.set(t_str, {{
                        t: t_str,
                        x: t_str.replace(' ', 'T'),
                        v: parseFloat(v.toFixed(3)),
                        p: pct,
                        chg: parseInt(chg_str, 10),
                        scr: b > 0 ? 1 : 0,
                        b: b,
                        w: parseInt(w_str, 10),
                        ble: parseInt(ble_str, 10),
                        freq: cpuFreq,
                        heap: freeHeap
                    }});
                }}
            }}

            while ((match = p6.exec(text)) !== null) {{
                const [, t_str, v_str, pct_str, chg_str, scr_str, w_str] = match;
                if (!map.has(t_str) && !t_str.startsWith('up_')) {{
                    const v = parseFloat(v_str);
                    const pct = parseInt(pct_str, 10);
                    if (v >= 2.8 && v <= 4.4 && pct >= 0 && pct <= 100) {{
                        const scr = parseInt(scr_str, 10);
                        map.set(t_str, {{
                            t: t_str,
                            x: t_str.replace(' ', 'T'),
                            v: parseFloat(v.toFixed(3)),
                            p: pct,
                            chg: parseInt(chg_str, 10),
                            scr: scr,
                            b: scr ? 255 : 0,
                            w: parseInt(w_str, 10) ? 2 : 0,
                            ble: 1,
                            freq: 240,
                            heap: 0
                        }});
                    }}
                }}
            }}
            return Array.from(map.values());
        }}

        function refreshDashboardData() {{
            const longestSeg = findLongestDischarge(allDataset);
            const hasDischarge = longestSeg && longestSeg.length >= 2;
            if (currentView === 'discharge' && hasDischarge) {{
                dischargeStartIso = longestSeg[0].x;
                dischargeEndIso = longestSeg[longestSeg.length - 1].x;
                initChart(dischargeStartIso, dischargeEndIso);
            }} else {{
                currentView = 'all';
                document.getElementById('viewDischargeBtn').classList.remove('active');
                document.getElementById('viewAllBtn').classList.add('active');
                initChart(undefined, undefined);
            }}
        }}

        // Initialize default view to Full History (All Datapoints)
        initChart(undefined, undefined);
    </script>
</body>
</html>
"""
    dashboard_path = os.path.join(ROOT_DIR, "battery.html")
    with open(dashboard_path, "w", encoding="utf-8") as f:
        f.write(html_content)
    print(f"[+] Saved dashboard to {dashboard_path}")

if __name__ == "__main__":
    port_arg = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    run_pipeline(port_arg)
