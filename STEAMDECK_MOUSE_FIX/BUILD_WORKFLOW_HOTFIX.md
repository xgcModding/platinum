# November 2026 GitHub Actions build hotfix

This package updates only the GitHub Actions workflow and the optional Qt Plasma
plugin packaging rule. It is intended to be applied over the **already patched**
`Royal2-4/platinum-mouse-test` fork (or another fork with the earlier mouse fix).
It is not a standalone copy of the game and does not include the mouse bridge.

## Verified conclusions from run 43464699397

- `actions/checkout@v4` Node.js 20 warning was nonfatal; checkout succeeded.
- Ubuntu runner migration was informational, not the failed build command.
- Step "Build using the project's own Linux target" failed with make exit code 2.
- Full build logs were not in the three annotations. The exact failing command
  cannot be determined from those annotations alone.
- Independent source inspection found a packaging hazard: the original script
  unconditionally copies `platformthemes/KDEPlasmaPlatformTheme6.so`, provided
  by Arch's `plasma-integration` package, whereas the CI dependencies do not
  install that package. The theme is optional, so this hotfix skips it if absent.
  This is a *potential subsequent cause*, not a verified explanation for run #1.

## Changes

- `runs-on: ubuntu-24.04` pins the host runner version.
- `actions/checkout@v5` uses Node.js 24.
- The build step retains all output in `build-linux.log`, displays the final
  160 lines and a filtered error list on failure, and preserves `make` status.
- A `pokeplatinum-linux-build-log` artifact is uploaded even if the build fails.
- The successful AppImage artifact retains the name
  `pokeplatinum-steamdeck-mouse-test`.

## Windows 11: apply to the existing clone

From Windows PowerShell, assuming the clone is at `D:\direct\platinum-mouse-test`:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-github-actions-hotfix.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
cd D:\direct\platinum-mouse-test
git status --short
git add .github/workflows/build-steamdeck-mouse-test.yml tools/scripts/copy_pc_files.sh STEAMDECK_MOUSE_FIX/BUILD_WORKFLOW_HOTFIX.md
git commit -m "Improve Linux AppImage CI diagnostics and optional Qt plugin packaging"
git push origin main
```

Start a **new** manual workflow run via GitHub Actions > Steam Deck mouse fix
- manual AppImage build > Run workflow (main). Do NOT select "Re-run jobs" on
run #1; that reruns the old version.

On success, download `pokeplatinum-steamdeck-mouse-test` from Artifacts, unzip,
and copy the AppImage onto the Steam Deck for testing as before.

On failure, download `pokeplatinum-linux-build-log`, unzip, and upload
`build-linux.log` to the chat. Alternatively in the new job's "Build using
..." step, copy the FIRST `error:`, `FAILED:`, or `Traceback` and 20-40 lines
around it. The annotations alone only say that `make linux` exited nonzero.

No ROM, assets, saves, or modified original upstream repositories are needed.
