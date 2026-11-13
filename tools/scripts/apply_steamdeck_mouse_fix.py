#!/usr/bin/env python3
"""Apply the small Linux-only libntr follow-up to an existing Meson checkout.

Fresh Meson subprojects receive the same patch automatically through the wrap.
This is for checkouts created *before* the wrap listed its second patch.
No Git history manipulation, hash-object commands, resets, or cherry-picks.
"""
from pathlib import Path
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'subprojects/libntr/libraries/sim/src/sim_main.cpp'

ORIGINAL_HEADER = '''#ifdef SDK_BUILD_WIN64
extern "C" void PlatformWindow_ConfigureWindows(void *);
#endif
extern "C" void GameRestart_Update(void) __attribute__((weak));'''
UPDATED_HEADER = '''#ifdef SDK_BUILD_WIN64
extern "C" void PlatformWindow_ConfigureWindows(void *);
#endif
#if defined(SDK_BUILD_LINUX) && !defined(SDK_BUILD_OSX) && !defined(SDK_BUILD_WIN64)
extern "C" bool PlatformWindow_GetMousePosition(int *, int *) __attribute__((weak));
#endif

extern "C" void GameRestart_Update(void) __attribute__((weak));'''
ORIGINAL_POSITION = '''    SDL_GetMouseState(&x, &y);

    // Convert touch coordinate to DS screen coordinates'''
UPDATED_POSITION = '''    SDL_GetMouseState(&x, &y);
#if defined(SDK_BUILD_LINUX) && !defined(SDK_BUILD_OSX) && !defined(SDK_BUILD_WIN64)
    if (PlatformWindow_GetMousePosition) PlatformWindow_GetMousePosition(&x, &y);
#endif

    // Convert touch coordinate to DS screen coordinates'''


def main():
    if not TARGET.is_file() or TARGET.is_symlink():
        sys.exit('[mouse-bridge] libntr has not been checked out by Meson yet. '
                 'Run make linux to let Meson fetch and patch it.')
    before = TARGET.read_text()
    if UPDATED_HEADER in before and UPDATED_POSITION in before:
        print('[mouse-bridge] libntr mouse follow-up is already applied')
        return
    if UPDATED_HEADER in before or UPDATED_POSITION in before:
        sys.exit('[mouse-bridge] Only part of the fix is present; preserving the checkout')
    if before.count(ORIGINAL_HEADER) != 1 or before.count(ORIGINAL_POSITION) != 1:
        sys.exit('[mouse-bridge] Unexpected libntr source revision or local edits. '
                 'Preserving the checkout; do not reset it automatically.')
    after = before.replace(ORIGINAL_HEADER, UPDATED_HEADER).replace(ORIGINAL_POSITION, UPDATED_POSITION)
    # Write atomically; neither the upstream repository nor Git history changes.
    handle, name = tempfile.mkstemp(prefix='.sim_main-mouse-', dir=TARGET.parent)
    try:
        with os.fdopen(handle, 'w') as file:
            file.write(after)
        os.chmod(name, TARGET.stat().st_mode)
        os.replace(name, TARGET)
    finally:
        if os.path.exists(name):
            os.unlink(name)
    print('[mouse-bridge] Applied mouse-position follow-up to local libntr checkout')


if __name__ == '__main__':
    main()
