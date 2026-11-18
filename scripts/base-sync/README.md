# Base sync (VentureCore -> VentureLW)

VentureLW was bootstrapped by **copying** the MIT-licensed
[VentureCore](https://github.com/wwq486554-ux/VentureCore) starter base and
re-applying the platform identity.  The base stays a separate repository; this
directory keeps the copy relationship reviewable.

## Files

| File | Purpose |
| --- | --- |
| `BASELINE` | the VentureCore release the copy was made from (one line, e.g. `v2.1.0`) |
| `sync_base.py` | compare / report / copy helper |

## Workflow

1. Fetch the newer base release into a local checkout of VentureCore.
2. Run the report:

   ```bash
   scripts/base-sync/sync_base.py --from ../VentureCore --to v2.2.0
   ```

   The report (`Release/IntDir/linux/base-sync/report-<sha>.md`, build output,
   not committed) lists every base file that changed upstream and classifies it:

   * **SAFE** - this repository has no local edit relative to `BASELINE`; the
     file can be copied verbatim with `--apply`;
   * **NEW** - new upstream file, safe to add;
   * **MANUAL** - this repository edited, renamed or removed the file (platform
     identity, project files, tests, docs), so the upstream change has to be
     replayed by hand; the report includes both diffs (upstream and local);
   * **SYNCED** - already identical, nothing to do;
   * **DEAD** - upstream deleted a file that still exists here.

   Identity-owned files are annotated; they are always MANUAL by nature.

3. Apply the safe files (one path per `--apply`, repeatable):

   ```bash
   scripts/base-sync/sync_base.py --from ../VentureCore --to v2.2.0 \
       --apply src/Engine/Hooks.Save.cpp
   ```

   The helper refuses everything that is not SAFE/NEW, copies the file from the
   target archive and runs `scripts/linux/check_eol.py --fix` on it.

4. Replay the MANUAL files by hand, then review and accept:

   ```bash
   git diff
   scripts/linux/build_linux.sh Release --build-type RELEASE -j 8
   VCS_GAMEMD_EXE=<gamemd.exe> python3 scripts/verify_build.py
   scripts/linux/run_tests.sh && scripts/linux/load_probe.sh
   python3 scripts/linux/check_eol.py
   git commit -m "chore(base-sync): port <base change>"
   ```

   Do not bump `BASELINE` until the whole target release has been replayed and
   verified; bump it in the same commit that completes the sync.

## What the helper never does

* it never copies a file classified MANUAL and never resolves a conflict;
* it never rewrites the platform identity (`VentureLW`, savegame id, project
  file names) - that is the manual replay part;
* it never touches `src/Features/` (feature modules are ours, not base code).

## Why copy instead of submodule

The base is a template meant to be copied (`docs/MakeYourOwnPlatform.md` in the
base repository): the derived platform renames the identity, adds its own
features and owns its own release line.  A submodule would keep the base's
identity and build files, so the copy + explicit sync is the chosen model.
