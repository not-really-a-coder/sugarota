#include "web_portal.h"
#include "storage.h"

WebServer server(80);

const char config_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sugarota Configuration</title>
<style>
body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; padding: 20px 20px 100px 20px; background: #050508; color: #e0e0e0; margin: 0; }
.logo-title { font-family: 'Outfit', sans-serif; font-size: 28px; font-weight: 800; margin: 0; padding-top: 10px; background: linear-gradient(135deg, #ffffff 30%, #00f0ff 65%, #00ff66 100%); -webkit-background-clip: text; background-clip: text; -webkit-text-fill-color: transparent; text-transform: uppercase; text-align: center; letter-spacing: -0.03em; }
.subtitle { color: #8e8e9f; font-size: 14px; margin-top: 8px; text-align: center; margin-bottom: 30px; font-weight: 500; }
.section { background: rgba(13, 13, 21, 0.75); padding: 20px; border-radius: 12px; margin-bottom: 20px; border: 1px solid rgba(255,255,255,0.05); box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
h3 { color: #fff; margin-top: 0; font-size: 18px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 8px; font-family: 'Outfit', sans-serif; }
label { display: block; margin-top: 12px; font-size: 14px; color: #bbb; }
input[type="text"], input[type="password"], input[type="number"], select { width: 100%; padding: 12px; margin-top: 6px; background: #1a1a1a; color: #fff; border: 1px solid #333; border-radius: 6px; box-sizing: border-box; font-size: 16px; transition: border-color 0.2s; }
input:focus, select:focus { border-color: #00f0ff; outline: none; }
input[type="checkbox"] { transform: scale(1.5); margin-right: 10px; accent-color: #00ff66; }
.checkbox-container { display: flex; align-items: center; margin-top: 15px; }
.pwd-container { position: relative; }
.pwd-toggle { position: absolute; right: 15px; top: 14px; cursor: pointer; font-size: 12px; user-select: none; color: #aaa; background: #333; padding: 6px 10px; border-radius: 4px; font-weight: bold; }
.bottom-bar { position: fixed; bottom: 0; left: 0; width: 100%; background: rgba(5,5,8,0.95); padding: 15px; box-shadow: 0 -2px 20px rgba(0,0,0,0.8); box-sizing: border-box; display: flex; justify-content: center; z-index: 100; border-top: 1px solid rgba(255,255,255,0.05); backdrop-filter: blur(10px); }
button { width: 100%; max-width: 400px; padding: 16px; background: linear-gradient(135deg, #00f0ff, #00ff66); color: #000; border: none; border-radius: 8px; font-weight: 800; font-size: 18px; cursor: pointer; text-transform: uppercase; box-shadow: 0 4px 15px rgba(0,255,102,0.2); transition: transform 0.1s; }
button:active { transform: scale(0.98); }
</style>
</head><body>
<h1 class="logo-title">Sugarota Config</h1>
<div class="subtitle">Wireless Device Configuration</div>
<form action="/save" method="POST" id="configForm">

<div class="section">
  <h3>WiFi Settings</h3>
  <label>Primary SSID</label><input type="text" name="primary_ssid" id="primary_ssid">
  <label>Primary Password</label>
  <div class="pwd-container">
    <input type="password" name="primary_pass" id="primary_pass">
    <span class="pwd-toggle" onclick="togglePwd('primary_pass', this)">SHOW</span>
  </div>
  <label>Secondary SSID</label><input type="text" name="secondary_ssid" id="secondary_ssid">
  <label>Secondary Password</label>
  <div class="pwd-container">
    <input type="password" name="secondary_pass" id="secondary_pass">
    <span class="pwd-toggle" onclick="togglePwd('secondary_pass', this)">SHOW</span>
  </div>
</div>

<div class="section">
  <h3>Provider</h3>
  <select name="provider" id="provider" onchange="updateProviderVisibility()">
    <option value="DEXCOM">Dexcom</option>
    <option value="NIGHTSCOUT">Nightscout</option>
  </select>
</div>

<div class="section" id="ns_section" style="display:none;">
  <h3>Nightscout</h3>
  <label>URL (https://...)</label><input type="text" name="ns_url" id="ns_url">
  <label>API Secret</label>
  <div class="pwd-container">
    <input type="password" name="ns_secret" id="ns_secret">
    <span class="pwd-toggle" onclick="togglePwd('ns_secret', this)">SHOW</span>
  </div>
</div>

<div class="section" id="dex_section" style="display:none;">
  <h3>Dexcom</h3>
  <label>Username</label><input type="text" name="dex_user" id="dex_user">
  <label>Password</label>
  <div class="pwd-container">
    <input type="password" name="dex_pass" id="dex_pass">
    <span class="pwd-toggle" onclick="togglePwd('dex_pass', this)">SHOW</span>
  </div>
  <label>Server</label>
  <select name="dex_server" id="dex_server">
    <option value="shareous1.dexcom.com">United States / Global (shareous1)</option>
    <option value="share2.dexcom.com">US Alternate (share2)</option>
    <option value="share1a.dexcom.com">International Region (share1a)</option>
  </select>
</div>

<div class="section">
  <h3>Connection & Update</h3>
  <label>Connection Mode</label>
  <select name="connection_mode" id="connection_mode">
    <option value="AUTO">Auto (BLE first, Wi-Fi fallback)</option>
    <option value="BLE_ONLY">BLE Only (Low power companion)</option>
    <option value="WIFI_ONLY">Wi-Fi Only (Direct polling)</option>
  </select>
  <label>Update Frequency (Poll Interval)</label>
  <select name="poll_interval_sec" id="poll_interval_sec">
    <option value="30">30 seconds</option>
    <option value="60">1 minute (60s)</option>
    <option value="120">2 minutes (120s)</option>
    <option value="300">5 minutes (300s)</option>
  </select>
</div>

<div class="section">
  <h3>System</h3>
  <label>Blood Glucose Units</label>
  <select name="units" id="units">
    <option value="mg/dL">mg/dL</option>
    <option value="mmol/L">mmol/L</option>
  </select>
  <label>NTP Server</label><input type="text" name="ntp" id="ntp">
  <label>GMT Offset (Minutes)</label><input type="number" name="gmt_offset" id="gmt_offset">
  <label>Daylight Offset (Minutes)</label><input type="number" name="dst_offset" id="dst_offset">
  <div class="checkbox-container">
    <input type="checkbox" name="debug" id="debug" value="1">
    <label style="margin-top:0;">Enable Debug Mode</label>
  </div>
</div>

<div class="bottom-bar">
  <button type="submit">SAVE & APPLY</button>
</div>
</form>

<script>
function togglePwd(id, btn) {
  var x = document.getElementById(id);
  if (x.type === "password") { x.type = "text"; btn.innerText = "HIDE"; } else { x.type = "password"; btn.innerText = "SHOW"; }
}
function updateProviderVisibility() {
  var p = document.getElementById('provider').value;
  document.getElementById('ns_section').style.display = (p === 'NIGHTSCOUT') ? 'block' : 'none';
  document.getElementById('dex_section').style.display = (p === 'DEXCOM') ? 'block' : 'none';
}
fetch('/api/config').then(function(r){return r.json();}).then(function(c){
  if(c.wifi) {
    document.getElementById('primary_ssid').value = c.wifi.primary_ssid || '';
    document.getElementById('primary_pass').value = c.wifi.primary_pass || '';
    document.getElementById('secondary_ssid').value = c.wifi.secondary_ssid || '';
    document.getElementById('secondary_pass').value = c.wifi.secondary_pass || '';
  }
  document.getElementById('provider').value = c.provider || 'DEXCOM';
  if(c.nightscout) {
    document.getElementById('ns_url').value = c.nightscout.url || '';
    document.getElementById('ns_secret').value = c.nightscout.secret || '';
  }
  if(c.dexcom) {
    document.getElementById('dex_user').value = c.dexcom.user || '';
    document.getElementById('dex_pass').value = c.dexcom.pass || '';
    document.getElementById('dex_server').value = c.dexcom.server || 'shareous1.dexcom.com';
  }
  if(c.connection_mode) {
    document.getElementById('connection_mode').value = c.connection_mode.toUpperCase();
  }
  if(c.poll_interval_sec) {
    document.getElementById('poll_interval_sec').value = String(c.poll_interval_sec);
  }
  document.getElementById('units').value = c.units || 'mg/dL';
  if(c.timezone) {
    document.getElementById('ntp').value = c.timezone.ntp || 'pool.ntp.org';
    document.getElementById('gmt_offset').value = c.timezone.offset !== undefined ? Math.round(c.timezone.offset / 60) : 0;
    document.getElementById('dst_offset').value = c.timezone.daylight !== undefined ? Math.round(c.timezone.daylight / 60) : 0;
  }
  document.getElementById('debug').checked = c.debug || false;
  updateProviderVisibility();
});
</script>
</body></html>
)rawliteral";

void handleConfigPage() { server.send(200, "text/html", config_html); }

void handleGetConfig() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(404, "text/plain", "No config"); return; }
  server.streamFile(f, "application/json"); 
  f.close();
}

void handleSaveConfig() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  primarySSID = server.arg("primary_ssid"); primaryPass = server.arg("primary_pass");
  secondarySSID = server.arg("secondary_ssid"); secondaryPass = server.arg("secondary_pass");
  String p = server.arg("provider");
  if (p == "DEXCOM") currentProvider = PROVIDER_DEXCOM; else if (p == "NIGHTSCOUT") currentProvider = PROVIDER_NIGHTSCOUT;
  nsUrl = server.arg("ns_url"); nsSecret = server.arg("ns_secret");
  dexUser = server.arg("dex_user"); dexPass = server.arg("dex_pass"); dexServer = server.arg("dex_server");
  
  if (server.hasArg("units")) {
    bgUnits = (server.arg("units") == "mmol/L") ? UNIT_MMOLL : UNIT_MGDL;
  }

  if (server.hasArg("connection_mode")) {
    connectionMode = server.arg("connection_mode");
    connectionMode.toUpperCase();
  }

  if (server.hasArg("poll_interval_sec")) {
    pollIntervalSec = server.arg("poll_interval_sec").toInt();
    if (pollIntervalSec < 30 || pollIntervalSec > 600) pollIntervalSec = 60;
  }
  
  ntpServer = server.arg("ntp");
  gmtOffset_sec = server.arg("gmt_offset").toInt() * 60;
  daylightOffset_sec = server.arg("dst_offset").toInt() * 60;
  debugMode = server.hasArg("debug");
  
  saveConfig();
  server.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head><body style='background:#121212;color:#FD20;font-family:Arial;text-align:center;padding:50px;'><h2>Configuration Saved!</h2><p style='color:#e0e0e0'>The device is now rebooting. You can safely close this page.</p></body></html>");
  delay(1000); 
  ESP.restart();
}

void setupWebPortal() {
  server.on("/", HTTP_GET, handleConfigPage);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/save", HTTP_POST, handleSaveConfig);
  server.begin();
}
