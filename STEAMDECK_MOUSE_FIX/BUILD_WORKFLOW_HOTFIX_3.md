# GitHub Actions hotfix 3: missing ENet library

## Confirmed failure from the user-supplied logs

The November 13, 2026 run failed during Meson setup, before compiling the game:

```
subprojects/libntr/libraries/sim/meson.build:18:35:
ERROR: C shared or static library 'enet' not found
/usr/bin/ld: cannot find -lenet: No such file or directory
```

The current workflow installs the SDL and Qt dependencies but omits the Arch Linux `enet` package. Arch's `extra/enet` includes `/usr/lib/libenet.so` and `/usr/lib/pkgconfig/libenet.pc`.

## Changes

- Adds `enet` to the Arch `pacman -Syu --noconfirm --needed` dependency list.
- Adds `libenet` to the pkg-config diagnostics.
- Adds a preflight compiler/linker test that invokes `gcc ... -lenet`, matching the exact library lookup failing in Meson.
- Leaves the Steam Deck mouse fix and source changes untouched.
- Keeps `actions/checkout@v5`, `actions/upload-artifact@v6`, and existing log artifacts.

## Apply from Windows 11

Download `platinum-github-actions-hotfix-3.zip` to Downloads. In PowerShell:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-github-actions-hotfix-3.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
cd D:\direct\platinum-mouse-test
git diff -- .github/workflows/build-steamdeck-mouse-test.yml
git add .github/workflows/build-steamdeck-mouse-test.yml STEAMDECK_MOUSE_FIX/BUILD_WORKFLOW_HOTFIX_3.md
git commit -m "Install ENet for Linux AppImage build"
git push origin main
```

On GitHub, go to your fork's **Actions** page and select **Steam Deck mouse fix - manual AppImage build**. Choose **Run workflow** using the updated `main` branch. Do not re-run the older failed job; it uses the old commit.

If successful, download the `pokeplatinum-steamdeck-mouse-test` artifact and test on Steam Deck. If the job fails, download `pokeplatinum-linux-build-log` and provide `build-linux.log` and `meson-log.txt` for analysis. For failures during dependency installation or checkout, capture the corresponding GitHub step log instead.

## Scope and verification

The missing ENet library is directly confirmed in the supplied build log. This patch fixes that prerequisite; it cannot guarantee later compilation/linking/packaging succeeds. The workflow syntax and ZIP contents were checked locally, but no complete GitHub Actions build was run here.
