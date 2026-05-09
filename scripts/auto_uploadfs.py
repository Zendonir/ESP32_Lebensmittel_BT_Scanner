"""
auto_uploadfs.py – runs 'uploadfs' automatically after every 'upload'.

Add to platformio.ini:
    extra_scripts = post:scripts/auto_uploadfs.py
"""
Import("env")
import subprocess, sys

def upload_filesystem(source, target, env):
    print("\n[auto-uploadfs] Uploading LittleFS filesystem image…")
    result = subprocess.run(
        [sys.executable, "-m", "platformio", "run",
         "-t", "uploadfs",
         "-e", env["PIOENV"]],
        cwd=env["PROJECT_DIR"],
    )
    if result.returncode != 0:
        print("[auto-uploadfs] WARNING: filesystem upload failed (code %d)" % result.returncode)
    else:
        print("[auto-uploadfs] Filesystem uploaded successfully.")

env.AddPostAction("upload", upload_filesystem)
