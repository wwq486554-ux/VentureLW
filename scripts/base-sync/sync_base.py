#!/usr/bin/env python3
"""Semi-automatic base sync for the VentureLW platform.

VentureLW was bootstrapped by copying the MIT-licensed VentureCore starter base
and re-applying the platform identity (method A: copy + rename).  This helper
keeps that relationship reviewable; it is deliberately *not* a vendoring tool.

  * ``scripts/base-sync/BASELINE`` records the base release that was copied;
  * ``--to TAG`` reports what changed upstream between BASELINE and TAG and
    classifies every changed file:

      SAFE    this repository has no local edit relative to BASELINE, so the
              file can be copied verbatim (``--apply PATH``);
      MANUAL  this repository edited/renamed the file (platform identity,
              feature work), so the upstream change has to be replayed by hand;
      NEW     new upstream file (safe to add unless we already have it);
      DEAD    upstream deleted a file we still have.

It also lists local edits under base-owned paths (everything except
``src/Features/``), so an unexpected divergence is caught during the sync.

Usage
-----
    scripts/base-sync/sync_base.py --from ../VentureCore --to v2.2.0
    scripts/base-sync/sync_base.py --from ../VentureCore --to v2.2.0 --apply src/Engine/Hooks.Save.cpp
    scripts/base-sync/sync_base.py --from ../VentureCore --to v2.1.0 --list

The report is written to ``Release/IntDir/linux/base-sync/`` (build output,
git-ignored).  ``--apply`` only accepts paths classified SAFE/NEW; after
copying it runs ``scripts/linux/check_eol.py --fix`` for the copied files and
prints the review steps (git diff -> build -> the five acceptance checks ->
commit).

Identity-owned files are annotated in the report; they always need a manual
replay because their content carries the VentureLW rename.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tarfile
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_FILE = os.path.join(REPO_ROOT, "scripts", "base-sync", "BASELINE")
REPORT_DIR = os.path.join(REPO_ROOT, "Release", "IntDir", "linux", "base-sync")

# Paths that may legitimately carry the platform identity or the platform's own
# feature modules.  A base file under these paths always needs a manual replay.
IDENTITY_FILES = {
    "README.md", "NOTICE.md", "CHANGELOG.md", "LICENSE.md", "COPYING", "CREDITS.md",
    "docs/Licensing.md", "docs/Lineage.md", "docs/Renaming.md",
    "src/Core/PlatformConfig.h", "src/Core/PlatformVersion.h", "src/version.rc",
    "scripts/verify_build.py", "scripts/linux/load_probe.sh",
    "VentureCore.vcxproj", "VentureCore.props", "VentureCore.sln",
    "VentureCore.ini.example",
}
# Base paths whose files are compared.  Feature modules are ours by definition.
BASE_PREFIXES = (
    "src/Core/", "src/Engine/", "src/Command/", "src/Event/", "src/Ext/",
    "include/platform/", "tests/", "scripts/", "docs/", "external/",
)
BASE_ROOT_FILES = {
    ".editorconfig", ".gitattributes", ".gitignore",
    "VentureCore.vcxproj", "VentureCore.props", "VentureCore.sln",
    "VentureCore.ini.example",
}

SKIP_DIRS = {".git", "__pycache__", "Release", "Debug"}


def is_base_path(path: str) -> bool:
    if path in BASE_ROOT_FILES:
        return True
    return path.startswith(BASE_PREFIXES)


def run_git(args: list[str], cwd: str) -> str:
    result = subprocess.run(["git", "-C", cwd, *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("git %s failed in %s:\n%s" % (" ".join(args), cwd, result.stderr.strip()))
    return result.stdout.strip()


def resolve_ref(repo: str, ref: str) -> str:
    return run_git(["rev-parse", "--verify", ref + "^{commit}"], repo)


def extract_archive(repo: str, ref: str, destination: str) -> None:
    os.makedirs(destination, exist_ok=True)
    archive = os.path.join(destination, ".base.tar")
    with open(archive, "wb") as handle:
        result = subprocess.run(["git", "-C", repo, "archive", "--format=tar", ref],
                                stdout=handle, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.exit("git archive %s failed in %s:\n%s"
                 % (ref, repo, result.stderr.decode("utf-8", "replace").strip()))
    with tarfile.open(archive) as tar:
        tar.extractall(destination)
    os.unlink(archive)


def read_tree(root: str) -> dict[str, str]:
    """Returns relative-path -> text content for the base-owned text files."""
    tree: dict[str, str] = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for filename in sorted(filenames):
            full = os.path.join(dirpath, filename)
            relative = os.path.relpath(full, root).replace(os.sep, "/")
            if not is_base_path(relative):
                continue
            try:
                with open(full, "rb") as handle:
                    data = handle.read()
            except OSError:
                continue
            tree[relative] = data.decode("utf-8", "replace").replace("\r\n", "\n")
    return tree


def changed_files(old: dict[str, str], new: dict[str, str]) -> list[str]:
    names = set(old) | set(new)
    return sorted(name for name in names if old.get(name) != new.get(name))


def classify(local: dict[str, str], baseline: dict[str, str], target: dict[str, str]) -> dict[str, str]:
    """Maps every upstream-changed path to SAFE / MANUAL / NEW / DEAD."""
    result: dict[str, str] = {}
    for path in changed_files(baseline, target):
        if path not in target:                      # deleted upstream
            result[path] = "DEAD"
        elif path not in baseline:                  # added upstream
            if path not in local:
                result[path] = "NEW"
            elif local[path] == target[path]:
                result[path] = "SYNCED"
            else:
                result[path] = "MANUAL"
        elif path not in local:                     # we removed it locally
            result[path] = "MANUAL"
        elif local[path] == baseline[path]:
            result[path] = "SAFE"
        elif local[path] == target[path]:
            result[path] = "SYNCED"
        else:
            result[path] = "MANUAL"
    return result


def unified_diff(path: str, baseline: dict[str, str], target: dict[str, str],
                 local: dict[str, str] | None, limit: int = 200) -> list[str]:
    import difflib
    lines = list(difflib.unified_diff(
        baseline.get(path, "").splitlines(), target.get(path, "").splitlines(),
        fromfile="base:" + path, tofile="new-base:" + path, lineterm=""))
    if local is not None and path in local:
        lines += ["", "--- local edit relative to the bootstrap baseline ---"]
        lines += list(difflib.unified_diff(
            baseline.get(path, "").splitlines(), local[path].splitlines(),
            fromfile="base:" + path, tofile="local:" + path, lineterm=""))
    if len(lines) > limit:
        lines = lines[:limit] + ["... (%d more diff line(s), see the archives under %s)"
                                 % (len(lines) - limit, REPORT_DIR)]
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Semi-automatic VentureCore base sync (see the module docstring).")
    parser.add_argument("--from", dest="source", default=os.environ.get("VCS_BASE_SRC"),
                        help="VentureCore checkout (git repository) to sync from")
    parser.add_argument("--baseline", default=None,
                        help="base tag/commit this platform was bootstrapped from (default: scripts/base-sync/BASELINE)")
    parser.add_argument("--to", default="HEAD",
                        help="base tag/commit to compare against (default: HEAD)")
    parser.add_argument("--apply", action="append", default=[], metavar="PATH",
                        help="copy this SAFE/NEW file from the target base commit (repeatable)")
    parser.add_argument("--list", action="store_true",
                        help="only list the upstream-changed base files and the classification")
    parser.add_argument("--fail-on-diff", action="store_true",
                        help="exit 1 when the target has base changes to review")
    args = parser.parse_args()

    if not args.source:
        parser.error("--from is required (or set VCS_BASE_SRC)")

    baseline_ref = args.baseline
    if not baseline_ref:
        with open(BASELINE_FILE, encoding="utf-8") as handle:
            baseline_ref = handle.read().split()[0]

    source = os.path.abspath(args.source)
    baseline_sha = resolve_ref(source, baseline_ref)
    target_sha = resolve_ref(source, args.to)
    if baseline_sha == target_sha:
        print("base sync: %s and %s are the same commit (%s) - nothing to review"
              % (baseline_ref, args.to, baseline_sha[:12]))

    os.makedirs(REPORT_DIR, exist_ok=True)
    work_root = tempfile.mkdtemp(prefix="base-", dir=REPORT_DIR)
    baseline_dir = os.path.join(work_root, "baseline")
    target_dir = os.path.join(work_root, "target")
    extract_archive(source, baseline_sha, baseline_dir)
    extract_archive(source, target_sha, target_dir)

    baseline = read_tree(baseline_dir)
    target = read_tree(target_dir)
    local = read_tree(REPO_ROOT)
    if local == {}:
        sys.exit("no base-owned files found under %s - wrong directory?" % REPO_ROOT)

    classification = classify(local, baseline, target)
    upstream_changed = changed_files(baseline, target)
    local_edits = sorted(path for path in baseline
                         if path in local and local[path] != baseline[path])

    groups: dict[str, list[str]] = {}
    for path in upstream_changed:
        groups.setdefault(classification[path], []).append(path)
    safe = sorted(groups.get("SAFE", []) + groups.get("NEW", []))

    print("base sync")
    print("  source    : %s" % source)
    print("  baseline  : %s (%s)" % (baseline_ref, baseline_sha[:12]))
    print("  target    : %s (%s)" % (args.to, target_sha[:12]))
    print("  upstream changed base files : %d" % len(upstream_changed))
    for key in ("SAFE", "NEW", "MANUAL", "DEAD", "SYNCED"):
        if key in groups:
            print("    %-7s %d" % (key, len(groups[key])))
    print("  local edits under base paths: %d" % len(local_edits))
    if args.list or not upstream_changed:
        for path in upstream_changed:
            print("    [%s] %s" % (classification[path], path))

    if args.apply:
        for path in args.apply:
            state = classification.get(path)
            if state not in ("SAFE", "NEW"):
                sys.exit("cannot --apply %s: classification is %s (only SAFE/NEW are copied; "
                         "MANUAL means the change has to be replayed by hand)" % (path, state))
            source_file = os.path.join(target_dir, path.replace("/", os.sep))
            destination = os.path.join(REPO_ROOT, path.replace("/", os.sep))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with open(source_file, "rb") as handle:
                data = handle.read()
            with open(destination, "wb") as handle:
                handle.write(data)
            print("applied: %s" % path)
        subprocess.run([sys.executable, os.path.join(REPO_ROOT, "scripts", "linux", "check_eol.py"),
                        "--fix"] + [os.path.join(REPO_ROOT, p.replace("/", os.sep)) for p in args.apply],
                       check=False)
        print("\nreview steps: git diff -> rebuild -> the five acceptance checks -> commit")

    if not args.list:
        report_path = os.path.join(REPORT_DIR, "report-%s.md" % target_sha[:12])
        with open(report_path, "w", encoding="utf-8", newline="\n") as report:
            report.write("# VentureCore base sync report\n\n")
            report.write("* source: `%s`\n* baseline: `%s` (`%s`)\n* target: `%s` (`%s`)\n\n"
                         % (source, baseline_ref, baseline_sha, args.to, target_sha))
            report.write("## Upstream base changes\n\n")
            if not upstream_changed:
                report.write("No base files changed between the baseline and the target.\n\n")
            for key, title in (("SAFE", "SAFE - copy with --apply"),
                               ("NEW", "NEW - safe to add"),
                               ("MANUAL", "MANUAL - replay by hand (locally edited, renamed or removed)"),
                               ("SYNCED", "SYNCED - already identical"),
                               ("DEAD", "DEAD - deleted upstream")):
                if key not in groups:
                    continue
                report.write("### %s\n\n" % title)
                for path in groups[key]:
                    note = " (identity-owned)" if path in IDENTITY_FILES else ""
                    report.write("* `%s`%s\n" % (path, note))
                report.write("\n")
            manual = groups.get("MANUAL", [])
            if manual:
                report.write("## Diffs for MANUAL files\n\n")
                for path in manual:
                    report.write("### `%s`\n\n```diff\n" % path)
                    report.write("\n".join(unified_diff(path, baseline, target, local)) + "\n```\n\n")
            report.write("## Local edits under base-owned paths\n\n")
            report.write("These files differ from the bootstrap baseline in this repository "
                         "(identity, tests, docs); they are expected but the list is the audit "
                         "trail for the sync.\n\n")
            for path in local_edits:
                note = " (identity-owned)" if path in IDENTITY_FILES else ""
                report.write("* `%s`%s\n" % (path, note))
        print("  report    : %s" % os.path.relpath(report_path, REPO_ROOT))

    if args.fail_on_diff and upstream_changed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
