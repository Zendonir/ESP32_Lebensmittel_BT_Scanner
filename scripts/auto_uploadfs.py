"""
auto_uploadfs.py – uploads LittleFS ONLY when web files actually changed.

Compares a SHA-256 hash of every file in data/ to a stored hash in
.pio/web_hash.  If the hash matches the firmware upload is sufficient
and uploadfs is SKIPPED – user data stored in LittleFS is preserved.

If web files did change the filesystem is re-uploaded and the new hash
is stored.

To force a full re-upload (e.g. after factory-reset): delete .pio/web_hash
"""
Import("env")
import hashlib, os, subprocess, sys

def _hash_data_dir(data_dir):
    h = hashlib.sha256()
    for root, dirs, files in os.walk(data_dir):
        dirs.sort()
        for fname in sorted(files):
            path = os.path.join(root, fname)
            rel  = os.path.relpath(path, data_dir)
            h.update(rel.encode())
            with open(path, "rb") as f:
                h.update(f.read())
    return h.hexdigest()

def upload_filesystem_if_changed(source, target, env):
    project_dir = env["PROJECT_DIR"]
    data_dir    = os.path.join(project_dir, "data")
    hash_file   = os.path.join(project_dir, ".pio", "web_hash")

    if not os.path.isdir(data_dir):
        print("[auto-uploadfs] No data/ directory found, skipping.")
        return

    current_hash = _hash_data_dir(data_dir)

    stored_hash = ""
    if os.path.isfile(hash_file):
        with open(hash_file, "r") as f:
            stored_hash = f.read().strip()

    if current_hash == stored_hash:
        print("\n[auto-uploadfs] Web files unchanged – skipping uploadfs.")
        print("[auto-uploadfs] User data in LittleFS is preserved.")
        return

    print("\n[auto-uploadfs] Web files changed – uploading filesystem…")
    result = subprocess.run(
        [sys.executable, "-m", "platformio", "run",
         "-t", "uploadfs", "-e", env["PIOENV"]],
        cwd=project_dir,
    )
    if result.returncode != 0:
        print("[auto-uploadfs] WARNING: filesystem upload failed (code %d)" % result.returncode)
    else:
        os.makedirs(os.path.dirname(hash_file), exist_ok=True)
        with open(hash_file, "w") as f:
            f.write(current_hash)
        print("[auto-uploadfs] Filesystem uploaded and hash stored.")

env.AddPostAction("upload", upload_filesystem_if_changed)
