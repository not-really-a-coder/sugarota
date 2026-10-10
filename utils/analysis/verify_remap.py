import json

with open('battery_data.json', 'r', encoding='utf-8') as f:
    raw = json.load(f)

# Old mapping
def old_pct(v):
    vMap = [3.10, 3.35, 3.45, 3.55, 3.62, 3.68, 3.74, 3.80, 3.88, 3.98, 4.12]
    pMap = [   1,    5,   10,   20,   30,   40,   50,   60,   70,   85,  100]
    if v <= vMap[0]: return 1
    if v >= vMap[-1]: return 100
    for i in range(len(vMap) - 1):
        if vMap[i] <= v <= vMap[i+1]:
            ratio = (v - vMap[i]) / (vMap[i+1] - vMap[i])
            return round(pMap[i] + ratio * (pMap[i+1] - pMap[i]))
    return 100

# New mapping
def new_pct(v):
    vMap = [3.02, 3.05, 3.10, 3.14, 3.33, 3.45, 3.59, 3.72, 3.82, 3.92, 3.99, 4.03, 4.15]
    pMap = [   0,    2,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100]
    if v <= vMap[0]: return 0
    if v >= vMap[-1]: return 100
    for i in range(len(vMap) - 1):
        if vMap[i] <= v <= vMap[i+1]:
            ratio = (v - vMap[i]) / (vMap[i+1] - vMap[i])
            return round(pMap[i] + ratio * (pMap[i+1] - pMap[i]))
    return 100

test_voltages = [4.15, 4.03, 3.99, 3.92, 3.82, 3.72, 3.59, 3.45, 3.33, 3.14, 3.10, 3.05, 3.02, 3.00]
print(f"{'Voltage':8} | {'Old Map':8} | {'New Map':8} | Delta")
print("-" * 38)
for v in test_voltages:
    o = old_pct(v)
    n = new_pct(v)
    print(f"{v:6.2f} V | {o:6d}%  | {n:6d}%  | {n - o:+d}%")
