# Hotfix #5: define `main_lsf` on Linux without requiring the ARM toolchain

## Confirmed cause

GitHub Actions run 43471713573 stopped at:

```
meson.build:731:8: ERROR: Unknown variable name "main_lsf". Did you mean "main_lcf"?
```

Hotfix #4 accidentally placed `main_lsf = files('meson/overlays.lsf')`
inside the `if build_target == 'arm'` block, but the unconditional
`overlay.map` custom target still refers to this variable later in the
same `meson.build` file.

## What changed

- `main_lsf` is now defined unconditionally (this is only a reference to
  an existing file in the source tree and does not run ARM tools).
- Generating the Nintendo DS linker file `main.lcf` still happens only
  when `build_target == 'arm'`.
- The desktop build still uses `main_lcf = []`, preserving the intended
  ARM-toolchain skipping behavior from Hotfix #4.
- No other source, workflow, `libntr`, or mouse-event files are changed.

## Apply to your existing Windows 11 fork

Download `platinum-github-actions-hotfix-5.zip` to Downloads, then in
PowerShell:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-github-actions-hotfix-5.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
cd D:\direct\platinum-mouse-test
git --no-pager diff -- meson.build
git add meson.build STEAMDECK_MOUSE_FIX/BUILD_WORKFLOW_HOTFIX_5.md
git commit -m "Fix undefined main_lsf on Linux builds"
git push origin main
```

GitHub Actions > **Steam Deck mouse fix - manual AppImage build** >
**Run workflow**, select `main` and start a **new run**.

If Meson finds another error or the compilation fails, upload the new
`pokeplatinum-linux-build-log` artifact.

## Verification / limitations

Verified the change against the complete Hotfix #4 `meson.build` delivered
previously, checked all references to `main_lsf`, and checked that
`meson/overlays.lsf` exists in the repository. All other file contents
are preserved. A full x86_64 AppImage compilation and Steam Deck test
have **not** been performed in the packaging environment; the next
GitHub Actions run is still required.
