"""
Pre-build script: patches library dependencies so their internal FreeRTOS
tasks use PSRAM for their stacks instead of internal SRAM.

Applied patches:
  AsyncTCP/src/AsyncTCP.cpp – replaces xTaskCreate[PinnedToCore] with
      x...WithCaps(... MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) so the 16 KB
      async_tcp task stack lives in PSRAM instead of internal DRAM.

The script marks patched files with a sentinel comment so it only patches
once per library installation. Run `pio run --clean` to force re-patch.
"""

Import("env")  # noqa: F821  (SCons variable, injected by PlatformIO)
import os, re

SENTINEL = "// PSRAM_TASK_PATCHED"

def _patch(path: str, replacements: list[tuple[str, str]], extra_include: str = "") -> None:
    if not os.path.isfile(path):
        print(f"[patch_libdeps] SKIP (not found): {path}")
        return
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    if SENTINEL in src:
        print(f"[patch_libdeps] Already patched: {os.path.basename(path)}")
        return
    for old, new in replacements:
        if old not in src:
            print(f"[patch_libdeps] WARNING: pattern not found in {os.path.basename(path)}: {old[:60]!r}")
            return
        src = src.replace(old, new, 1)
    if extra_include:
        # Insert after the last #include line at the top
        lines = src.splitlines(keepends=True)
        insert_at = 0
        for i, line in enumerate(lines):
            if line.startswith("#include"):
                insert_at = i + 1
        lines.insert(insert_at, extra_include + "\n")
        src = "".join(lines)
    src = SENTINEL + "\n" + src
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print(f"[patch_libdeps] Patched: {os.path.basename(path)}")


libdeps_dir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))

# ── AsyncTCP: move 16 KB async_tcp task stack to PSRAM ─────────────────────
_patch(
    os.path.join(libdeps_dir, "AsyncTCP", "src", "AsyncTCP.cpp"),
    [
        (
            "xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, xCoreID)",
            "xTaskCreatePinnedToCoreWithCaps(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, xCoreID, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        ),
        (
            "xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask)",
            "xTaskCreateWithCaps(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        ),
    ],
    extra_include="#include <esp_heap_caps.h>",
)
