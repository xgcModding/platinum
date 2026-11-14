# Lake-Trio Windows build: libntr graphics patch preimage mismatch

## Confirmed failure

Meson checks out libntr at `6bee622411de6d998474f8193bbdcbcc0d14d768`, then `git apply` fails at `libraries/sim/src/g3_handler.cpp:432` while applying `libntr-pokeplatinum.patch`. This happens before C++ compilation. The patch's `G3SIM_BoxTest` preimage includes blank lines missing from the pinned source. It is **not** caused by GCC, SDL, Node.js, or an ENet dependency.

## What the test-branch fix does

- Adds a Windows-only CI pre-step before `make windows`.
- Downloads only the `g3_handler.cpp` source at the already-pinned libntr commit.
- Requires an exact match of every nonblank line of each original g3 hunk, ignoring only blank-line differences.
- Reconstructs the intended modified version of this file and regenerates just the `g3_handler.cpp` section of the large Meson patch, using proper line offsets.
- Runs `git apply --check`, applies the regenerated section to a temporary fixture, and verifies the result byte-for-byte.
- Writes the corrected patch into the ephemeral CI workspace. **The checked-in patch and Linux workflow stay unchanged.** If a nonblank line differs, the pre-step stops with a clear diagnostic.

This is a *targeted CI compatibility workaround*, not proof that the full Windows build will complete. Ideally, once the build works, generate and commit a canonical patch against the pinned upstream libntr tree instead of permanently keeping a dynamic rebasing step.

## Windows 11 PowerShell

Ensure you are on the existing `ci/windows-x86_64` branch and that `git status --short` is clean. (If not clean, save your changes first.)

```powershell
cd D:\direct\platinum-mouse-test
git switch ci/windows-x86_64
git status --short
```

Download `Lake-Trio-Windows-libntr-patch-fix.zip` to Downloads. Extract onto your local repo:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\Lake-Trio-Windows-libntr-patch-fix.zip" -DestinationPath "D:\direct\platinum-mouse-test" -Force
```

Then commit and update your **existing PR to `Royal2-4/Lake-Trio:main`**:

```powershell
git add .github/workflows/build-windows-x86_64.yml tools/scripts/rebase_libntr_g3_patch.py WINDOWS_BUILD_PATCH_FIX.md
git commit -m "ci: rebase libntr graphics patch against pinned source for Windows"
git push origin ci/windows-x86_64
```

The PR CI should run automatically. Inspect **Verify and rebase pinned libntr g3 graphics patch** first, followed by `make windows`. If anything fails, download `Lake-Trio-Windows-x86_64-build-logs` from the new run and share it for next diagnosis.

Do **not** merge the Windows PR into `main` until CI and a Windows runtime smoke test pass. Do not alter the already working Linux AppImage or the mouse fix to work around this issue.
