import json
from datetime import datetime

with open('battery_data.json', 'r', encoding='utf-8') as f:
    raw = json.load(f)

# Discharge window: from 2026-10-09 22:17:37 (peak V: 4.153V) to 2026-10-10 18:57:52 (3.018V)
data = [d for d in raw if not d['timestamp'].startswith('up_')]
data.sort(key=lambda x: datetime.strptime(x['timestamp'], '%Y-%m-%d %H:%M:%S'))

peak_idx = next(i for i, d in enumerate(data) if d['timestamp'] == '2026-10-09 22:17:37')
discharge = data[peak_idx:]

t0 = datetime.strptime(discharge[0]['timestamp'], '%Y-%m-%d %H:%M:%S')
t_end = datetime.strptime(discharge[-1]['timestamp'], '%Y-%m-%d %H:%M:%S')
total_sec = (t_end - t0).total_seconds()

print(f"Total discharge points: {len(discharge)}, Duration: {total_sec/3600:.2f} h")

# For each point, calculate real elapsed percentage:
# Real battery capacity percentage = (1.0 - elapsed_time / total_time) * 100
for d in discharge:
    t = datetime.strptime(d['timestamp'], '%Y-%m-%d %H:%M:%S')
    elapsed = (t - t0).total_seconds()
    real_pct = round((1.0 - (elapsed / total_sec)) * 100)
    d['real_pct'] = max(0, min(100, real_pct))
    d['elapsed_h'] = elapsed / 3600.0

print(f"{'Timestamp':19} | {'V':6} | {'Logged%':7} | {'RealElapsed%':12} | {'Elapsed_h':9}")
print("-" * 65)
for i in range(0, len(discharge), 8):
    d = discharge[i]
    print(f"{d['timestamp']} | {d['voltage']:.3f}V | {d['pct']:6d}% | {d['real_pct']:11d}% | {d['elapsed_h']:8.2f}h")
last = discharge[-1]
print(f"{last['timestamp']} | {last['voltage']:.3f}V | {last['pct']:6d}% | {last['real_pct']:11d}% | {last['elapsed_h']:8.2f}h")
