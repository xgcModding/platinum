# Lake-Trio Windows x86_64 — distribution verification hotfix

## Why the last GitHub Actions run failed

The supplied artifact log shows `make windows` completed all 1,916 Ninja tasks, including linking `main.exe` and generating the `desktop-release` custom target. The subsequent `Verify the packaged Windows distribution` CI step exited early without identifying which `test` failed.

The original repository includes an intentionally empty `tools/config/sim_config.ini` (0 bytes). The earlier workflow used `test -s build_win64/sim_config.ini`, which fails for any zero-byte file even when it exists and is valid. This likely explains the reported failure. The log alone cannot exclude another failed assertion because the old checks did not print file-specific errors.

## Changes included

- Only `.github/workflows/build-windows-x86_64.yml` is changed; the Linux workflow, game source, Makefile and `libntr` patches are not touched.
- Validate `sim_config.ini` using `-f` (must exist as a regular file) rather than `-s` (must have content).
- Keep `-s` for files that must be nonempty, including `main.exe`, `game.ini`, `firmware.bin` and runtime DLLs.
- List `build_win64` before file checks and emit explicit GitHub error annotations naming the failing file.
- Check that `mods` exists before creating the release ZIP.

## Apply from Windows 11

Use the *same branch* and existing draft PR. Do not merge it or create another PR for the hotfix.

```powershell
cd D:\direct\platinum-mouse-test
git switch ci/windows-x86_64
git status --short
```

If status shows local changes, preserve them before extracting. Download this ZIP into your Downloads folder, then run:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\Lake-Trio-Windows-verify-hotfix.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force

git --no-pager diff -- .github/workflows/build-windows-x86_64.yml
git add .github/workflows/build-windows-x86_64.yml WINDOWS_DISTRIBUTION_HOTFIX.md
git commit -m "ci: accept empty Windows sim_config.ini in release verification"
git push origin ci/windows-x86_64
```

GitHub Actions should run automatically for the existing PR targeting `Royal2-4/Lake-Trio:main`. Review the **Verify the packaged Windows distribution** step. If successful, download the Windows distribution artifact, extract it, and launch `main.exe` on Windows 11 before merging.

If it still fails, the new step prints the directory listing and a named error; download `Lake-Trio-Windows-x86_64-build-logs` and share it. This fix has been syntax-checked and tested against a local mock package, but a full GitHub-hosted Windows build has not yet been rerun.
