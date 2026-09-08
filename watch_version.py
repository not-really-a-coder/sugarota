import os
import time
import json
import re
import datetime
import sys

INO_PATH = os.path.join("firmware", "sugarota", "sugarota.ino")
STATE_PATH = os.path.join("data", "version_state.json")

def get_calver_segments():
    now = datetime.datetime.now()
    year_offset = now.year - 2026
    month_str = f"{now.month:02d}"
    day_str = f"{now.day:02d}"
    date_key = now.strftime("%Y-%m-%d")
    return year_offset, month_str, day_str, date_key

def load_state():
    if os.path.exists(STATE_PATH):
        try:
            with open(STATE_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {"last_date": "", "build_increment": -1}

def save_state(state):
    try:
        os.makedirs(os.path.dirname(STATE_PATH), exist_ok=True)
        with open(STATE_PATH, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2)
    except Exception as e:
        print(f"[VERSION WATCHER ERROR] Could not save state: {e}")

def update_version(is_startup=False):
    if not os.path.exists(INO_PATH):
        print(f"[VERSION WATCHER ERROR] {INO_PATH} not found!")
        return None

    year_offset, month_str, day_str, date_key = get_calver_segments()
    state = load_state()

    with open(INO_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    # Look for '#define SUGAROTA_VERSION "..."'
    pattern = r'#define\s+SUGAROTA_VERSION\s+"([^"]+)"'
    match = re.search(pattern, content)

    file_build = None
    if match:
        current_version_in_file = match.group(1)
        ver_match = re.match(r'^v(\d+)\.(\d+)\.(\d+)\.(\d+)$', current_version_in_file)
        if ver_match:
            y, m, d, b = ver_match.groups()
            if int(y) == year_offset and m == month_str and d == day_str:
                file_build = int(b)

    if is_startup:
        # On startup, sync internal state with the file's current version if it matches today's date
        if file_build is not None:
            state["last_date"] = date_key
            state["build_increment"] = file_build
            save_state(state)
            print(f"[VERSION WATCHER] Synced startup build version to: v{year_offset}.{month_str}.{day_str}.{file_build}")
        return None

    # Determine daily build revision
    if file_build is not None:
        build_revision = file_build + 1
    elif state["last_date"] == date_key:
        build_revision = state["build_increment"] + 1
    else:
        build_revision = 0  # New day, reset revision index

    new_version = f"v{year_offset}.{month_str}.{day_str}.{build_revision}"

    if match:
        current_version_in_file = match.group(1)
        # Avoid redundant edits if version matches exactly
        if current_version_in_file == new_version:
            return None
        
        replacement = f'#define SUGAROTA_VERSION "{new_version}"'
        updated_content = re.sub(pattern, replacement, content)
    else:
        # Prepend to the top of the file if macro doesn't exist
        updated_content = f'// --- Version Control ---\n#define SUGAROTA_VERSION "{new_version}"\n\n' + content

    # Write modifications back to the sketch
    with open(INO_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated_content)

    # Save state back
    state["last_date"] = date_key
    state["build_increment"] = build_revision
    save_state(state)

    print(f"[VERSION WATCHER] Sugarota version updated: {new_version}")
    return new_version

def watch():
    print(f"\n[VERSION WATCHER] Monitoring {INO_PATH} for modifications...")
    print(f"==================================================")
    print(f"Auto-Increment Format: v{{YearOffset}}.{{Month:02d}}.{{Day:02d}}.{{Build}}")
    print(f"Press Ctrl+C to terminate the watcher thread.\n")

    # Initial check on startup (sync without incrementing)
    update_version(is_startup=True)
    
    last_mtime = os.path.getmtime(INO_PATH) if os.path.exists(INO_PATH) else 0

    try:
        while True:
            time.sleep(0.5)
            if os.path.exists(INO_PATH):
                current_mtime = os.path.getmtime(INO_PATH)
                if current_mtime != last_mtime:
                    # Let file writes settle for a brief moment
                    time.sleep(0.1)
                    update_version()
                    # Sleep slightly and capture the new mtime produced by our own write
                    # so the watcher does not re-trigger on its own change
                    time.sleep(0.1)
                    last_mtime = os.path.getmtime(INO_PATH)
    except KeyboardInterrupt:
        print("\nStopping version watcher. Goodbye!")

if __name__ == "__main__":
    watch()
