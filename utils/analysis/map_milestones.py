import json
from datetime import datetime

with open('battery_data.json', 'r', encoding='utf-8') as f:
    raw = json.load(f)

data = [d for d in raw if not d['timestamp'].startswith('up_')]
data.sort(key=lambda x: datetime.strptime(x['timestamp'], '%Y-%m-%d %H:%M:%S'))

peak_idx = next(i for i, d in enumerate(data) if d['timestamp'] == '2026-10-09 22:17:37')
discharge = data[peak_idx:]

t0 = datetime.strptime(discharge[0]['timestamp'], '%Y-%m-%d %H:%M:%S')
t_end = datetime.strptime(discharge[-1]['timestamp'], '%Y-%m-%d %H:%M:%S')
total_sec = (t_end - t0).total_seconds()

# Target milestones
target_milestones = [100, 95, 90, 85, 80, 70, 60, 50, 40, 30, 20, 15, 10, 5, 2, 0]

print(f"{'Target Pct':10} | {'Voltage (measured)':18} | {'Elapsed Time':12} | {'Current Map Reported':20}")
print("-" * 65)

new_map = []
for p in target_milestones:
    # elapsed time ratio for this percentage:
    target_elapsed_sec = (1.0 - (p / 100.0)) * total_sec
    target_time = t0.timestamp() + target_elapsed_sec
    
    # find closest point in discharge
    closest = min(discharge, key=lambda d: abs(datetime.strptime(d['timestamp'], '%Y-%m-%d %H:%M:%S').timestamp() - target_time))
    v = closest['voltage']
    old_p = closest['pct']
    elapsed_h = target_elapsed_sec / 3600.0
    new_map.append((p, round(v, 2)))
    print(f"{p:10d}% | {v:.3f} V ({round(v, 2):.2f}V)    | +{elapsed_h:5.2f} h     | {old_p:3d}%")

# Dedup voltages and ensure strictly monotonic increasing for C array
# Sorted from 0% (lowest voltage) to 100% (highest voltage)
new_map.sort(key=lambda x: x[0])
print("\nSuggested Table (Pct -> Voltage):")
for p, v in new_map:
    print(f"  {p:3d}% -> {v:.2f}V")
