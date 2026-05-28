"""
Pre-build script: reverts the AsyncTCP PSRAM-stack patch if it is still present
from a previous build.

Background: an earlier version of this script replaced xTaskCreatePinnedToCore
with xTaskCreatePinnedToCoreWithCaps(... MALLOC_CAP_SPIRAM) to move the async_tcp
task stack into PSRAM. This caused a hard crash on ESP32-S3:

  assert failed: spi_flash_disable_interrupts_caches_and_other_cpu
  cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())

PSRAM is accessed via the same SPI cache controller as flash.  During any
NVS / LittleFS write the cache is briefly disabled, making PSRAM inaccessible.
FreeRTOS always needs the current task's stack to be reachable, so a PSRAM stack
causes an immediate panic.

This script detects and removes the old patch so existing local builds are fixed
without requiring the user to delete .pio/libdeps manually.
"""

Import("env")  # noqa: F821  (SCons variable, injected by PlatformIO)
import os

OLD_SENTINEL    = "// PSRAM_TASK_PATCHED"
REVERTED_MARKER = "// PSRAM_PATCH_REVERTED"


def _revert_async_tcp(path: str) -> None:
    if not os.path.isfile(path):
        return
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    if REVERTED_MARKER in src:
        # Already clean
        return

    if OLD_SENTINEL not in src:
        # Was never patched — nothing to do
        return

    # Remove sentinel line
    src = src.replace(OLD_SENTINEL + "\n", "")

    # Undo the PSRAM function replacements
    src = src.replace(
        "xTaskCreatePinnedToCoreWithCaps(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, xCoreID, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        "xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, xCoreID)",
    )
    src = src.replace(
        "xTaskCreateWithCaps(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
        "xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask)",
    )

    # Remove the esp_heap_caps.h include we injected
    src = src.replace("#include <esp_heap_caps.h>\n", "")

    # Mark as reverted so we don't process it again
    src = REVERTED_MARKER + "\n" + src

    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print("[patch_libdeps] Reverted stale PSRAM patch in AsyncTCP.cpp")


libdeps_dir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
_revert_async_tcp(
    os.path.join(libdeps_dir, "AsyncTCP", "src", "AsyncTCP.cpp")
)
