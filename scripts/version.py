"""
version.py – setzt FW_VERSION aus 'git describe --tags --always'.
Ergebnis:  v1.0.1  /  v1.0.1-3-gabcdef  /  latest-5-gabcdef  /  unknown
"""
Import("env")
import subprocess

def get_fw_version():
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty=-dev"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        # Strip leading 'v' so the web-UI shows "1.0.1" not "v1.0.1"
        return v.lstrip("v") if v else "unknown"
    except Exception:
        return "unknown"

version = get_fw_version()
env.Append(CPPDEFINES=[("FW_VERSION", f'\\"{version}\\"')])
print(f"[version] FW_VERSION = {version}")
