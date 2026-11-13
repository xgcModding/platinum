# Steam Deck mouse test: GitHub Actions exit-127 hotfix

## What this fixes

The previous workflow ran `qtpaths6 --query QT_INSTALL_PLUGINS` as though `qtpaths6` were in the default PATH. Arch Linux's `qt6-base` package places the utility at `/usr/lib/qt6/bin/qtpaths6`. The check can therefore fail with exit code 127 (`command not found`) **before** `make linux` starts and before `build-linux.log` is created.

Changes:

1. Adds `/usr/lib/qt6/bin` to PATH during the toolchain diagnostics and to `GITHUB_PATH` for later steps.
2. Initializes `build-linux.log` *before* the diagnostic commands. Standard output and error from toolchain checks are recorded even when they fail.
3. Appends build output to that same log without losing the compiler's exit status.
4. Upgrades `actions/upload-artifact` from v4 to v6; v6 uses Node.js 24, eliminating its Node 20 warning.
5. Keeps the previously applied hotfix to `tools/scripts/copy_pc_files.sh` unchanged.

This hotfix addresses a confirmed workflow defect. The available annotations do not disclose the exact failing command, because GitHub requires a sign-in to read the detailed job output. It also does not prove that the rest of the project successfully compiles.

## Apply from Windows 11 PowerShell

You should already have `D:\direct\platinum-mouse-test` cloned and pushed to your fork. Do not download or replace the large original source ZIP again.

1. Download `platinum-github-actions-hotfix-2.zip` into Downloads.
2. Run:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\platinum-github-actions-hotfix-2.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
cd D:\direct\platinum-mouse-test
git diff -- .github/workflows/build-steamdeck-mouse-test.yml
git add .github/workflows/build-steamdeck-mouse-test.yml STEAMDECK_MOUSE_FIX/BUILD_WORKFLOW_HOTFIX_2.md
git commit -m "Fix Arch Qt tool lookup and retain CI diagnostics"
git push origin main
```

3. Visit https://github.com/Royal2-4/platinum-mouse-test/actions and run **Steam Deck mouse fix - manual AppImage build** on `main`. Do **not** select Re-run jobs for the older failed run: it would run the old commit again.
4. When it completes, check the run's **Artifacts** section:
   - On success, download `pokeplatinum-steamdeck-mouse-test`, extract the AppImage, and test on your Steam Deck.
   - On failure, download `pokeplatinum-linux-build-log`, extract `build-linux.log`, and upload the log to ChatGPT for examination. The workflow now creates this file *before* preliminary diagnostics.

If the failure happens during dependency installation or checkout (before the diagnostics step), GitHub's step log is the authoritative source; the artifact may not exist. You can copy the first error line and the surrounding lines from that step.

## Notes

`actions/upload-artifact@v6` uses Node.js 24 according to its upstream `action.yml`. The old Node warning was not the cause of exit code 127. No Linux AppImage or full Steam Deck runtime test was performed in this environment.
