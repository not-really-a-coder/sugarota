import os
import time
import json
import re
import datetime
import sys
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

STATE_PATH = os.path.join(REPO_ROOT, "data", "version_state.json")

# Target file paths
INO_PATH = os.path.join(REPO_ROOT, "firmware", "sugarota", "sugarota.ino")
CONFIG_H_PATH = os.path.join(REPO_ROOT, "firmware", "sugarota", "config.h")
INSTALLER_PATH = os.path.join(REPO_ROOT, "installer.html")
GRADLE_PATH = os.path.join(REPO_ROOT, "android-app", "app", "build.gradle.kts")

TARGETS = ["firmware", "installer", "android"]

def get_calver_segments():
    now = datetime.datetime.now()
    year_offset = now.year - 2026
    month_str = f"{now.month:02d}"
    day_str = f"{now.day:02d}"
    date_key = now.strftime("%Y-%m-%d")
    return year_offset, month_str, day_str, date_key

def load_state():
    state = {}
    if os.path.exists(STATE_PATH):
        try:
            with open(STATE_PATH, "r", encoding="utf-8") as f:
                state = json.load(f)
        except Exception:
            state = {}

    # Migration / Backward Compatibility:
    # If legacy flat state exists {"last_date": ..., "build_increment": ...}, nest under "firmware"
    if "last_date" in state and "firmware" not in state:
        state = {
            "firmware": {
                "last_date": state.get("last_date", ""),
                "build_increment": state.get("build_increment", -1),
                "version": f"v0.09.09.{state.get('build_increment', 0)}"
            }
        }

    # Ensure all target blocks exist
    for target in TARGETS:
        if target not in state or not isinstance(state[target], dict):
            state[target] = {
                "last_date": "",
                "build_increment": -1,
                "version": ""
            }

    return state

def save_state(state):
    try:
        os.makedirs(os.path.dirname(STATE_PATH), exist_ok=True)
        with open(STATE_PATH, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2)
    except Exception as e:
        print(f"[VERSION ERROR] Could not save state: {e}")

def parse_calver(version_str, year_offset, month_str, day_str):
    """Returns build int if version matches current day's CalVer prefix, else None."""
    if not version_str:
        return None
    match = re.match(r'^v?(\d+)\.(\d+)\.(\d+)\.(\d+)$', version_str.strip())
    if match:
        y, m, d, b = match.groups()
        if int(y) == year_offset and m == month_str and d == day_str:
            return int(b)
    return None

def compute_next_build(file_build, target_state, date_key):
    """
    Computes the next build number.
    If today's date matches state and build_increment is known:
    - If never incremented before (build_increment == -1), start at 0.
    - Otherwise increment by 1.
    If new day, reset to 0.
    """
    if file_build is not None:
        return file_build + 1
    if target_state.get("last_date") == date_key:
        curr_inc = target_state.get("build_increment", -1)
        return 0 if curr_inc < 0 else curr_inc + 1
    return 0

# --- Target Handlers ---

def update_firmware(is_startup=False, bump=False):
    if not os.path.exists(INO_PATH):
        return None

    year_offset, month_str, day_str, date_key = get_calver_segments()
    state = load_state()
    tgt_state = state["firmware"]

    with open(INO_PATH, "r", encoding="utf-8") as f:
        ino_content = f.read()

    pattern = r'#define\s+SUGAROTA_VERSION\s+"([^"]+)"'
    match = re.search(pattern, ino_content)
    current_in_file = match.group(1) if match else None
    file_build = parse_calver(current_in_file, year_offset, month_str, day_str)

    if is_startup:
        if file_build is not None:
            tgt_state["last_date"] = date_key
            tgt_state["build_increment"] = file_build
            tgt_state["version"] = f"v{year_offset}.{month_str}.{day_str}.{file_build}"
            save_state(state)
            print(f"[VERSION] Firmware startup sync: {tgt_state['version']}")
        return None

    if not bump and file_build is not None and tgt_state.get("version") == current_in_file:
        return None

    next_build = compute_next_build(file_build if bump else None, tgt_state, date_key)
    new_version = f"v{year_offset}.{month_str}.{day_str}.{next_build}"

    if current_in_file == new_version:
        return None

    # Update sugarota.ino
    if match:
        replacement = f'#define SUGAROTA_VERSION "{new_version}"'
        updated_ino = re.sub(pattern, replacement, ino_content)
    else:
        updated_ino = f'// --- Version Control ---\n#define SUGAROTA_VERSION "{new_version}"\n\n' + ino_content

    with open(INO_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated_ino)

    # Update config.h if present
    if os.path.exists(CONFIG_H_PATH):
        try:
            with open(CONFIG_H_PATH, "r", encoding="utf-8") as f:
                cfg_content = f.read()
            if re.search(pattern, cfg_content):
                updated_cfg = re.sub(pattern, f'#define SUGAROTA_VERSION "{new_version}"', cfg_content)
                with open(CONFIG_H_PATH, "w", encoding="utf-8", newline="\n") as f:
                    f.write(updated_cfg)
        except Exception as e:
            print(f"[VERSION WARNING] Could not update config.h: {e}")

    tgt_state["last_date"] = date_key
    tgt_state["build_increment"] = next_build
    tgt_state["version"] = new_version
    save_state(state)

    print(f"[VERSION] Firmware updated to: {new_version}")
    return new_version

def update_installer(is_startup=False, bump=False):
    if not os.path.exists(INSTALLER_PATH):
        return None

    year_offset, month_str, day_str, date_key = get_calver_segments()
    state = load_state()
    tgt_state = state["installer"]

    with open(INSTALLER_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    js_pattern = r'const\s+INSTALLER_VERSION\s*=\s*"([^"]+)";'
    badge_pattern = r'(<span[^>]*id=["\']installer-version-badge["\'][^>]*>)([^<]*)(</span>)'

    js_match = re.search(js_pattern, content)
    current_in_file = js_match.group(1) if js_match else None
    file_build = parse_calver(current_in_file, year_offset, month_str, day_str)

    if is_startup:
        if file_build is not None:
            tgt_state["last_date"] = date_key
            tgt_state["build_increment"] = file_build
            tgt_state["version"] = f"v{year_offset}.{month_str}.{day_str}.{file_build}"
            save_state(state)
            print(f"[VERSION] Installer startup sync: {tgt_state['version']}")
        return None

    if not bump and file_build is not None and tgt_state.get("version") == current_in_file:
        return None

    next_build = compute_next_build(file_build if bump else None, tgt_state, date_key)
    new_version = f"v{year_offset}.{month_str}.{day_str}.{next_build}"

    if current_in_file == new_version:
        return None

    updated_content = content
    if js_match:
        updated_content = re.sub(js_pattern, f'const INSTALLER_VERSION = "{new_version}";', updated_content)
    if re.search(badge_pattern, updated_content):
        updated_content = re.sub(badge_pattern, rf'\g<1>Installer {new_version}\g<3>', updated_content)

    with open(INSTALLER_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated_content)

    tgt_state["last_date"] = date_key
    tgt_state["build_increment"] = next_build
    tgt_state["version"] = new_version
    save_state(state)

    print(f"[VERSION] Installer updated to: {new_version}")
    return new_version

def update_android(is_startup=False, bump=False):
    if not os.path.exists(GRADLE_PATH):
        return None

    year_offset, month_str, day_str, date_key = get_calver_segments()
    state = load_state()
    tgt_state = state["android"]

    with open(GRADLE_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    name_pattern = r'versionName\s*=\s*"([^"]+)"'
    match = re.search(name_pattern, content)
    current_in_file = match.group(1) if match else None
    file_build = parse_calver(current_in_file, year_offset, month_str, day_str)

    if is_startup:
        if file_build is not None:
            tgt_state["last_date"] = date_key
            tgt_state["build_increment"] = file_build
            tgt_state["version"] = f"v{year_offset}.{month_str}.{day_str}.{file_build}"
            save_state(state)
            print(f"[VERSION] Android startup sync: {tgt_state['version']}")
        return None

    if not bump and file_build is not None and tgt_state.get("version") == current_in_file:
        return None

    next_build = compute_next_build(file_build if bump else None, tgt_state, date_key)
    new_version = f"v{year_offset}.{month_str}.{day_str}.{next_build}"

    if current_in_file == new_version:
        return None

    updated_content = re.sub(name_pattern, f'versionName = "{new_version}"', content)

    # Deterministic integer versionCode: YYMMDD(Build) e.g., 26090900
    try:
        ver_code_int = int(f"{year_offset + 26:02d}{month_str}{day_str}{next_build:02d}")
        code_pattern = r'versionCode\s*=\s*(\d+)'
        updated_content = re.sub(code_pattern, f'versionCode = {ver_code_int}', updated_content)
    except Exception:
        pass

    with open(GRADLE_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated_content)

    tgt_state["last_date"] = date_key
    tgt_state["build_increment"] = next_build
    tgt_state["version"] = new_version
    save_state(state)

    print(f"[VERSION] Android updated to: {new_version} (versionCode={ver_code_int if 'ver_code_int' in locals() else 'kept'})")
    return new_version

def update_target(target, is_startup=False, bump=False):
    if target == "firmware":
        return update_firmware(is_startup=is_startup, bump=bump)
    elif target == "installer":
        return update_installer(is_startup=is_startup, bump=bump)
    elif target == "android":
        return update_android(is_startup=is_startup, bump=bump)
    elif target == "all":
        res = {}
        for t in TARGETS:
            res[t] = update_target(t, is_startup=is_startup, bump=bump)
        return res
    else:
        print(f"[VERSION ERROR] Unknown target: {target}")
        return None

def get_dir_mtime(directory, extensions):
    max_mtime = 0
    if not os.path.exists(directory):
        return 0
    for root, _, files in os.walk(directory):
        for f in files:
            if any(f.endswith(ext) for ext in extensions):
                p = os.path.join(root, f)
                try:
                    mt = os.path.getmtime(p)
                    if mt > max_mtime:
                        max_mtime = mt
                except OSError:
                    pass
    return max_mtime

def watch():
    print(f"\n[VERSION WATCHER] Monitoring Sugarota targets for modifications...")
    print(f"==================================================")
    print(f"Auto-Increment Format: v{{YearOffset}}.{{Month:02d}}.{{Day:02d}}.{{Build}}")
    print(f"Targets: firmware, installer, android (Independent Build Numbers)")
    print(f"Press Ctrl+C to terminate the watcher thread.\n")

    # Initial startup sync across all targets without incrementing
    for t in TARGETS:
        update_target(t, is_startup=True)

    firmware_dir = os.path.join(REPO_ROOT, "firmware", "sugarota")
    android_src_dir = os.path.join(REPO_ROOT, "android-app", "app", "src")

    last_fw_mtime = get_dir_mtime(firmware_dir, (".ino", ".cpp", ".h", ".c"))
    last_installer_mtime = os.path.getmtime(INSTALLER_PATH) if os.path.exists(INSTALLER_PATH) else 0
    last_android_mtime = get_dir_mtime(android_src_dir, (".kt", ".xml", ".kts"))

    try:
        while True:
            time.sleep(0.5)

            # Check firmware modifications
            fw_mtime = get_dir_mtime(firmware_dir, (".ino", ".cpp", ".h", ".c"))
            if fw_mtime != last_fw_mtime:
                time.sleep(0.1)
                update_target("firmware", bump=True)
                time.sleep(0.1)
                last_fw_mtime = get_dir_mtime(firmware_dir, (".ino", ".cpp", ".h", ".c"))

            # Check installer modifications
            if os.path.exists(INSTALLER_PATH):
                inst_mtime = os.path.getmtime(INSTALLER_PATH)
                if inst_mtime != last_installer_mtime:
                    time.sleep(0.1)
                    update_target("installer", bump=True)
                    time.sleep(0.1)
                    last_installer_mtime = os.path.getmtime(INSTALLER_PATH)

            # Check android app modifications
            and_mtime = get_dir_mtime(android_src_dir, (".kt", ".xml", ".kts"))
            if and_mtime != last_android_mtime:
                time.sleep(0.1)
                update_target("android", bump=True)
                time.sleep(0.1)
                last_android_mtime = get_dir_mtime(android_src_dir, (".kt", ".xml", ".kts"))

    except KeyboardInterrupt:
        print("\nStopping version watcher. Goodbye!")

def main():
    parser = argparse.ArgumentParser(description="Sugarota Unified Multi-Target CalVer Manager")
    parser.add_argument("--target", choices=["firmware", "installer", "android", "all"], default=None,
                        help="Target component to manage/bump")
    parser.add_argument("--bump", action="store_true", help="Bump build revision for the target")
    parser.add_argument("--sync", action="store_true", help="Sync state file from source files without bumping")
    parser.add_argument("--watch", action="store_true", help="Start background file monitoring loop")

    args = parser.parse_args()

    if args.watch or (args.target is None and not args.bump and not args.sync):
        watch()
    elif args.sync:
        tgt = args.target or "all"
        update_target(tgt, is_startup=True)
    elif args.bump:
        tgt = args.target or "all"
        update_target(tgt, bump=True)
    elif args.target:
        update_target(args.target, is_startup=True)

if __name__ == "__main__":
    main()
