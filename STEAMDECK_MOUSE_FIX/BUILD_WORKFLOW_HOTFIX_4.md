# Hotfix 4 — skip Nintendo DS/ARM build tools when building a Linux AppImage

## Confirmed failure from the uploaded GitHub Actions artifact

`build-linux.log` line 493: `Program arm-none-eabi-gcc found: NO`
`build-linux.log` line 495: `tools/meson.build:25:24: ERROR: Program 'arm-none-eabi-gcc' not found or not executable`

Meson stopped during configuration. The earlier ENet preflight succeeded. Native GCC, SDL and Qt were found.

## Why this patch changes source instead of installing an ARM toolchain

`tools/meson.build` previously required `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`,
and `makelcf` even for a Linux x86_64 executable. In the game's root
`meson.build`, the `main.lcf` custom target was created on desktop builds even
though desktop linker flags do not use a Nintendo DS `.lcf` linker file.

This patch conditions the ARM tools on actual platform usage:

- `arm-none-eabi-gcc` and `arm-none-eabi-objcopy`: required for `arm` and
  `osx` (the latter uses a specific ARM assembly conversion helper).
- `makelcf`: required for `arm` only.
- `main_lcf`: `[]` on desktop targets. It is generated only on `arm`.

This avoids pulling in an approximately 2 GiB ARM cross-compiler for a Linux
x86_64 AppImage and avoids a potential subsequent missing-`makelcf` error.
The existing Steam Deck input fixes and your GitHub workflow are unchanged.

## Apply from Windows 11 to your existing test fork

1. Download `platinum-github-actions-hotfix-4.zip` to your Windows Downloads folder.
2. In PowerShell, run:

   ```powershell
   Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-github-actions-hotfix-4.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
   cd D:\direct\platinum-mouse-test
   git diff -- tools/meson.build meson.build
   git add tools/meson.build meson.build STEAMDECK_MOUSE_FIX/BUILD_WORKFLOW_HOTFIX_4.md
   git commit -m "Skip ARM-only tools for Linux AppImage build"
   git push origin main
   ```

3. GitHub: your fork > Actions > **Steam Deck mouse fix - manual AppImage build** >
   **Run workflow** > branch `main`. Start a NEW run, not `Re-run` on an old run.
4. In the new job's `Build using the project's own Linux target` step,
   Meson should no longer stop at `Program arm-none-eabi-gcc found: NO`.
5. If the build succeeds, download `pokeplatinum-steamdeck-mouse-test` from Artifacts.
   If it fails, download `pokeplatinum-linux-build-log` and share it. It will
   contain the next actual error, if one occurs.

## Verification status

- Source diff reviewed against the original patched repository archive.
- Checked that platform-dependent references remain inside the corresponding
  `arm` or `osx` branches, and desktop links do not use a `.lcf` linker script.
- This is an **untested build candidate**; a full GitHub Actions compilation and
  physical Steam Deck Gaming Mode test are still necessary.

## Scope

Only `meson.build`, `tools/meson.build`, and this explanatory note are in this
ZIP. `libntr` patch and mouse-event code are untouched. No game data included.
