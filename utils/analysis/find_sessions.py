import json
from datetime import datetime

with open("battery_data.json") as f:
    d = json.load(f)

d = [x for x in d if not x["timestamp"].startswith("up_")]
d.sort(key=lambda x: datetime.strptime(x["timestamp"], "%Y-%m-%d %H:%M:%S"))

sessions = []
curr = []
for pt in d:
    # A point is charging if flag is set or voltage jumped significantly upwards
    is_chg = (pt.get("charging", 0) == 1)
    if is_chg:
        if curr:
            sessions.append(curr)
            curr = []
    else:
        if curr and (pt["voltage"] - curr[-1]["voltage"] > 0.12):
            sessions.append(curr)
            curr = []
        curr.append(pt)
if curr:
    sessions.append(curr)

print(f"Total sessions found: {len(sessions)}")
for i, s in enumerate(sessions):
    t0 = datetime.strptime(s[0]["timestamp"], "%Y-%m-%d %H:%M:%S")
    t1 = datetime.strptime(s[-1]["timestamp"], "%Y-%m-%d %H:%M:%S")
    dur = (t1 - t0).total_seconds() / 3600.0
    print(f"Session {i}: {len(s)} pts, {s[0]['timestamp']} -> {s[-1]['timestamp']} ({dur:.2f}h) | V: {s[0]['voltage']} -> {s[-1]['voltage']} | P: {s[0]['pct']} -> {s[-1]['pct']}")

# Find the biggest session by duration
biggest = max(sessions, key=lambda s: (datetime.strptime(s[-1]["timestamp"], "%Y-%m-%d %H:%M:%S") - datetime.strptime(s[0]["timestamp"], "%Y-%m-%d %H:%M:%S")).total_seconds())
t0 = datetime.strptime(biggest[0]["timestamp"], "%Y-%m-%d %H:%M:%S")
t1 = datetime.strptime(biggest[-1]["timestamp"], "%Y-%m-%d %H:%M:%S")
print(f"\nBiggest Discharge Session: {len(biggest)} pts, {biggest[0]['timestamp']} -> {biggest[-1]['timestamp']} ({(t1-t0).total_seconds()/3600.0:.2f}h)")
