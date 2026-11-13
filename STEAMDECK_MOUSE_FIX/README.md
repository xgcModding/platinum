# Steam Deck / Gamescope mouse-click test patch

**Status: candidate fix, source/patch checks performed; NOT compiled or tested on physical Steam Deck.**

This package came from the provided `platinum-main.zip`. No ROM, game assets,
keys, downloaded dependency trees, compiled binary, or user saves are included.

## What's broken and what changes

Linux (`src/port/native_menu_linux.cpp`) creates a **Qt** shell and **Qt-owned**
child `QWindow`, then attaches **SDL** to that foreign window. The Qt menubar
receives clicks, while the SDL input queue can fail to receive mouse clicks
from the foreign viewport under Gamescope. An ImGui input window *inside* the
viewport depends on those SDL button events, as does the DS touch panel.

The libntr simulator also reads `SDL_GetMouseState(&x, &y)` for touchscreen
coordinates. Simply calling `SDL_PushEvent` from Qt does **not** update SDL's
internal mouse position; therefore both changes below are needed:

- `src/port/native_menu_linux.cpp`: detect Qt mouse movement/buttons on the
  embedded viewport (and its QWidget container), push corresponding SDL mouse
  events, map Qt logical coordinates to the SDL window size, and release held
  buttons on focus loss and delay releases one simulation frame so brief
  tap-down/up pairs are observable by the DS touch sampler. Controlled by `POKEPLATINUM_QT_MOUSE_BRIDGE=0` to
  disable for an A/B test. Log buttons with `POKEPLATINUM_MOUSE_DEBUG=1`.
- `subprojects/packagefiles/libntr-steamdeck-mouse.patch`: patch the simulator
  to ask the Linux Qt bridge for mouse position after `SDL_GetMouseState`.
  The original combined libntr patch remains unchanged.
- `subprojects/libntr.wrap`: automatically applies the original patch first,
  then the small follow-up for *new* dependency checkouts.
- `tools/scripts/apply_steamdeck_mouse_fix.py` and `Makefile`: apply the same
  follow-up idempotently to an *existing* local libntr checkout when running
  `make linux`; no Git history rewriting or `git hash-object` steps.
- `tools/scripts/sync_libntr_patch.py`: accepts the recognized layered patch
  without trying to replace it with the original version.
- `.github/workflows/build-steamdeck-mouse-test.yml`: a **manual**, experimental
  cloud AppImage build that runs on Linux (the repo had no GitHub Actions
  workflow). Building this has not been tested in a GitHub runner.
- `tools/scripts/steamdeck_mouse_debug.sh`: optional Deck Gaming Mode log
  wrapper; copy it next to the generated AppImage if testing needs logs.

**Limitation:** No actual `subprojects/libntr/` is present in the uploaded zip;
Meson fetches the `libntr.wrap` pinned revision at build time and applies the
patches. The GitHub Actions runner must have network access. This sandbox has
no Qt/SDL build headers or Steam Deck Gamescope session, so a successful game
build and actual input behavior have not been verified.

## Easiest Windows 11 path: build in GitHub Actions

**You do not need to run `make` in Windows PowerShell or install MSYS2 to
produce the Linux AppImage with this method.**

1. On GitHub, **Fork** `https://github.com/xgcModding/platinum` into your own
   account. Do not make a pull request or push changes to the author's repo.
2. On Windows, install **Git for Windows** if you do not already have it.
   Open **PowerShell** (not MSYS2). Create a separate test clone of your fork:

   ```powershell
   cd D:\direct
   git clone https://github.com/YOUR_USERNAME/platinum.git platinum-mouse-test
   ```

3. Copy the **overlay ZIP** supplied with this response into your Downloads
   folder (leave its filename as `platinum-steamdeck-mouse-overlay.zip`).
   Expand it **into the root of your new clone**, not another `platinum-main`
   subfolder, with this PowerShell command:

   ```powershell
   Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-steamdeck-mouse-overlay.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
   ```

   The overlay contains only changed/additional source files, not a second
   full source project. The original upstream repo remains untouched. It
   assumes your fork is based on the same source version as the supplied ZIP;
   do not blindly overwrite files if GitHub has since changed those sections.
4. Still in PowerShell:

   ```powershell
   cd D:\direct\platinum-mouse-test
   git status --short
   git add Makefile src/port/native_menu_linux.cpp subprojects/libntr.wrap subprojects/packagefiles/libntr-steamdeck-mouse.patch tools/scripts/apply_steamdeck_mouse_fix.py tools/scripts/sync_libntr_patch.py tools/scripts/steamdeck_mouse_debug.sh .github/workflows/build-steamdeck-mouse-test.yml STEAMDECK_MOUSE_FIX/README.md
   git commit -m "Test Qt to SDL mouse forwarding on Steam Deck"
   git push origin main
   ```

   This modifies **only your own fork**. If Git asks for a username/password,
   sign into GitHub via your Git credential manager; normal GitHub account
   passwords are not used for Git HTTPS pushes.
5. Go to the **Actions** tab in **your fork** on GitHub. If asked, enable
   workflows for the fork. Select **Steam Deck mouse fix - manual AppImage
   build**, click **Run workflow**, choose `main`, and start it.
6. After a *successful* build, click the workflow run, scroll to **Artifacts**,
   and download `pokeplatinum-steamdeck-mouse-test`. The artifact ZIP contains
   `pokeplatinum.AppImage`. If the build fails, open its log. The CI recipe is
   untested and the original project does not document all build dependencies.

   Do not upload or commit a ROM, game assets, passwords, save files, or keys.

## Alternative: use Linux / WSL2 from Windows 11

Windows cannot execute a Linux AppImage or natively compile this target with
`make windows`. WSL2 Ubuntu, a Linux VM, or a Linux machine with all Qt6,
SDL2-compat, SDL3, Meson, compiler, and packaging dependencies can run
`make linux`. Run it **inside Linux** from an extracted patch-updated source
copy stored in the WSL Linux filesystem, not from PowerShell. The exact
package list varies by distro; the bundled GitHub workflow uses Arch Linux.

Existing local `subprojects/libntr` work is preserved: the script only changes
an exact known input pattern, aborting safely if that section has unexpected
edits. A clean Meson checkout receives both patches through `libntr.wrap`.

## Steam Deck Gaming Mode test

1. Keep your **existing** AppImage and save/config folder untouched. Copy the
   new `.AppImage` to a separate writable test folder on the Deck.
2. In Desktop Mode, right-click it > **Properties > Permissions > Is executable**
   or use the terminal: `chmod +x pokeplatinum.AppImage`.
3. Add that native **AppImage** as a non-Steam game. In **Properties >
   Compatibility**, leave "Force the use of a specific Steam Play compatibility
   tool" **unchecked**. Do not use Proton for this Linux AppImage.
4. In controller layout, map the right trackpad to **Mouse** and a button or
   right trigger to **Left Mouse Click**, or try the Deck touchscreen directly.
5. Switch to **Gaming Mode**, open the game, click **Input > Configure
   Controls**, and try clicking the actual controls inside the ImGui window.
   Next, import your own legally obtained `.nds` if needed and test clicking
   the lower DS screen and dragging across it. Test both Wayland and X11 only
   if your environment offers both; do not force a platform arbitrarily.
6. For a direct A/B comparison without recompiling, set Steam's **Launch
   Options** to `POKEPLATINUM_QT_MOUSE_BRIDGE=0 %command%` to disable the
   workaround, then remove it to re-enable the workaround.

### If clicks still fail: collect actual Gaming Mode diagnostics

Copy `tools/scripts/steamdeck_mouse_debug.sh` **next to** the test AppImage,
mark **both** executable, and add the **script** to Steam as a non-Steam game
instead of the AppImage. The log is saved at
`~/.local/state/pokeplatinum/steamdeck-mouse.log`. In the log:

- `[native-menu] ... Qt->SDL mouse bridge enabled` means the patched Linux
  code launched.
- `[qt-sdl-mouse] down ... queued=1` and matching `up` mean Qt received the
  click and enqueued SDL events; remaining problems could be in SDL/ImGui focus
  or touch coordinate handling.
- No `[qt-sdl-mouse]` events on a click **inside the viewport** mean the Qt
  viewport is not receiving the click; further investigation of the Qt
  embedded surface and Gamescope will be required.
- `queued=-1` means `SDL_PushEvent` failed and SDL's error and window-event
  routing need investigation.

This is a **proposed source-level correction**, not a verified claim that it
fixes every Gamescope or Steam Deck configuration.
