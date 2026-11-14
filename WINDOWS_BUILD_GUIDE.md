# Lake-Trio Windows x86_64 CI: branch-first setup

This workflow is additive and **does not modify** the working Linux AppImage workflow.

## Why this is different from the earlier ZIP

The previous version used only `workflow_dispatch`. GitHub requires a manual (`workflow_dispatch`) workflow to be present on the default branch before its **Run workflow** control is available. For a first-time workflow that must be verified **before merging**, this version additionally uses:

```yaml
on:
  workflow_dispatch:
  pull_request:
    branches:
      - main
```

Opening a PR from your test branch to your own `Lake-Trio` `main` branch runs Windows CI automatically. You can review the results and logs in the PR, fix problems on the same branch, and merge only when satisfied. Once merged, **Run workflow** can also be used manually.

## Install on Windows 11 (PowerShell)

```powershell
cd D:\direct\platinum-mouse-test
git status --short
# Stop if there are uncommitted changes; preserve them first.
git remote set-url origin https://github.com/Royal2-4/Lake-Trio.git
git switch main
git pull --ff-only origin main
git switch -c ci/windows-x86_64
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\Lake-Trio-Windows-GitHub-Actions-PR-Test.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
git add .github/workflows/build-windows-x86_64.yml WINDOWS_BUILD_GUIDE.md
git commit -m "ci: test Windows x86_64 build via pull request"
git push -u origin ci/windows-x86_64
```

Then open https://github.com/Royal2-4/Lake-Trio/pulls and create a PR:
- **base**: `Royal2-4/Lake-Trio:main`
- **compare/head**: `Royal2-4/Lake-Trio:ci/windows-x86_64`

Keep the PR as a **draft** until the workflow and a Windows smoke test pass. Review the **Checks** tab for the `Windows x86_64 (UCRT64) - PR and manual build` run.

If the build succeeds, download the `Lake-Trio-Windows-x86_64` Actions artifact (ZIP plus checksum); unzip the distribution ZIP and smoke-test `main.exe` on Windows 11. Only then mark PR ready and merge. If it fails, inspect the build logs artifact and commit the fix to the **same branch**, then push; the PR CI reruns.

After merging, update your local `main`:

```powershell
git switch main
git pull --ff-only origin main
```

## Notes

- This PR trigger runs when pull requests targeting `main` are opened, synchronized, or reopened. It is not restricted to this single branch.
- The manual trigger is retained for later builds once the workflow resides on `main`.
- Build artifacts are *not* automatically attached to GitHub Releases.
- A green CI result demonstrates a build, not full runtime compatibility; test the executable, shipped DLLs, startup, input and save/load paths.
- The original Windows build recipe is not guaranteed to pass without further changes; this workflow captures logs on failure.

## Original workflow details

# Lake-Trio — Windows x86_64 GitHub Actions build

## What this adds

- **One new file:** `.github/workflows/build-windows-x86_64.yml`.
- Leaves the existing Linux/Steam Deck Actions workflow and all game source unchanged.
- Runs **manually** on `windows-2022` using MSYS2 **UCRT64**.
- Calls the repository's existing `make windows` target. The Makefile and `tools/scripts/copy_pc_files.sh` handle building and bundling the output in `build_win64/`.
- Validates `main.exe`, config files and the DLLs expected by the existing packaging script.
- Uploads a distributable `Lake-Trio-Windows-x86_64.zip`, its SHA-256 checksum, and build logs even if compilation fails.

## Apply on Windows 11 (PowerShell)

1. Download `Lake-Trio-Windows-GitHub-Actions.zip` into your Downloads folder.
2. Make sure you're on the fork's `main` branch and have no uncommitted work before merging the new file. If your local clone is at `D:\direct\platinum-mouse-test`, run:

```powershell
cd D:\direct\platinum-mouse-test
git status --short
git switch main
git pull --ff-only origin main
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\Lake-Trio-Windows-GitHub-Actions.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
git status --short
git add .github/workflows/build-windows-x86_64.yml WINDOWS_BUILD_GUIDE.md
git commit -m "Add manual Windows x86_64 GitHub Actions build"
git push origin main
```

If `git status --short` shows unrelated changes, preserve them before switching branches. If you use a different local path, replace `D:\direct\platinum-mouse-test`. Updating the remote after rename, if needed:

```powershell
git remote set-url origin https://github.com/Royal2-4/Lake-Trio.git
```

3. Go to `https://github.com/Royal2-4/Lake-Trio/actions` and select **Windows x86_64 (UCRT64) - manual build**.
4. Select **Run workflow**, branch **main**, then start the run. Do not rerun an older commit.
5. On success, open the run's **Artifacts** and download `Lake-Trio-Windows-x86_64`.
6. Extract GitHub's artifact ZIP, then extract the contained `Lake-Trio-Windows-x86_64.zip`. Keep **all files and folders together**. Run `main.exe` on Windows 11 for a smoke test. Confirm launching, menus, input, save/load, quit/relaunch, and any installed mods. No ROMs/assets are included.
7. If it fails, download `Lake-Trio-Windows-x86_64-build-logs` and inspect the earliest compiler/Meson error in `build-windows.log` (or attach the artifact for analysis).

## Important limitations and notes

- This is an **untested Windows CI build recipe**. YAML syntax and package names have been reviewed; it has not yet compiled on a GitHub-hosted Windows runner. Do not advertise it as a verified Windows release until you run it and test the resulting executable.
- `GITHUB_ACTIONS` is set automatically on GitHub-hosted runners. The existing Makefile passes `-Dskip_anim_scripts=true` when configuring Windows under GitHub Actions. This workflow intentionally **keeps that existing upstream behavior**; check whether that option affects the features you intend to distribute.
- The Windows build is separate from the Linux Steam Deck mouse bridge. Windows should not be described as requiring or testing the Gamescope-specific workaround.
- The workflow does **not** create GitHub Releases or modify existing tags. Attach the verified Windows ZIP manually to the release built from the **same source commit/tag**, or create a new release if source differs.
- GitHub's action artifact is itself a ZIP, so there are two extraction steps. The inner ZIP is the user-facing release package.
- The SHA-256 file lets users verify the inner ZIP (PowerShell: `Get-FileHash .\Lake-Trio-Windows-x86_64.zip -Algorithm SHA256`).
- The workflow will not include non-repository proprietary game assets or ROMs. Distribute only materials you have permission to distribute.

## Workflow files

- `.github/workflows/build-windows-x86_64.yml` — **new**, Windows only.
- `.github/workflows/build-steamdeck-mouse-test.yml` — **existing**, unchanged.
