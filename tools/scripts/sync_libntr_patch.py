#!/usr/bin/env python3
"""Refresh known libntr patch versions without resetting the dependency checkout."""

import configparser
import hashlib
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY = ROOT / "subprojects/libntr"
PATCH = "subprojects/packagefiles/libntr-pokeplatinum.patch"
LEGACY_PATCHES = (
    "subprojects/packagefiles/libntr-performance.patch",
    "subprojects/packagefiles/libntr-osx.patch",
)


def git(directory, *args):
    return subprocess.check_output(["git", "-C", str(directory), *args])


def blob_hash(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def ensure_base_objects(originals):
    # Existing shallow checkouts may predate the revision now pinned by the
    # wrap. Fetch its objects without switching branches or resetting edits.
    missing = any(subprocess.run(
        ["git", "-C", str(DEPENDENCY), "cat-file", "-e", f"{original}^{{blob}}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode for original in originals)
    if not missing:
        return
    wrap = configparser.ConfigParser()
    wrap.read(ROOT / "subprojects/libntr.wrap")
    revision = wrap["wrap-git"]["revision"]
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("libntr.wrap must pin a commit to recover missing patch bases")
    print(f"[libntr] Fetching missing patch bases from {revision}", flush=True)
    git(DEPENDENCY, "fetch", "--no-tags", "--depth=1", "origin", revision)


def sections(patch):
    result = {}
    for section in patch.split(b"diff --git ")[1:]:
        header = re.match(rb"a/(\S+) b/(\S+)\n(?:index ([0-9a-f]+)\.\.([0-9a-f]+) 100[0-7]{3}\n)?", section)
        if header is None or header[1] != header[2]:
            raise ValueError("Unsupported libntr patch section; no files changed")
        path = header[2].decode()
        if Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError("Invalid libntr patch path")
        result[path] = (header[3].decode() if header[3] else None,
                        header[4].decode() if header[4] else None, section)
    if not result:
        raise ValueError("Empty libntr patch")
    return result


def apply_section(base, section):
    lines = base.splitlines(keepends=True)
    output = []
    cursor = 0
    hunks = re.split(rb"(?m)^@@ -([0-9]+)(?:,([0-9]+))? \+([0-9]+)(?:,([0-9]+))? @@[^\n]*\n", section)
    for i in range(1, len(hunks), 5):
        start = int(hunks[i]) - 1
        old_count = int(hunks[i + 1] or b"1")
        new_count = int(hunks[i + 3] or b"1")
        old, new = [], []
        previous = None
        for line in hunks[i + 4].splitlines(keepends=True):
            if line.startswith(b"\\ No newline at end of file"):
                if previous in (b" ", b"-"):
                    old[-1] = old[-1].removesuffix(b"\n")
                if previous in (b" ", b"+"):
                    new[-1] = new[-1].removesuffix(b"\n")
                continue
            if line[:1] in (b" ", b"-"):
                old.append(line[1:])
            if line[:1] in (b" ", b"+"):
                new.append(line[1:])
            previous = line[:1]
        if len(old) != old_count or len(new) != new_count or start < cursor or lines[start:start + old_count] != old:
            raise ValueError("libntr patch does not match its original source")
        output.extend(lines[cursor:start])
        output.extend(new)
        cursor = start + old_count
    output.extend(lines[cursor:])
    return b"".join(output)


def historical_hashes():
    known = {}
    # Older builds can contain a previous combined patch or the legacy split
    # patches. Recognize their exact blobs; never overwrite unknown local edits.
    for patch_path in (PATCH, *LEGACY_PATCHES):
        commits = git(ROOT, "log", "--format=%H", "--", patch_path).decode().splitlines()
        for commit in commits:
            spec = f"{commit}:{patch_path}"
            if subprocess.run(["git", "-C", str(ROOT), "cat-file", "-e", spec],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
                continue
            for path, (_, updated, section) in sections(git(ROOT, "show", spec)).items():
                if updated is None:
                    try:
                        base = git(DEPENDENCY, "show", f"HEAD:{path}")
                        updated = blob_hash(apply_section(base, section))
                    except (ValueError, subprocess.CalledProcessError):
                        continue
                known.setdefault(path, set()).add(updated)
    return known


def main():
    desired = sections((ROOT / PATCH).read_bytes())
    pending = []
    for path, (original, updated, section) in desired.items():
        if original is None or updated is None:
            raise ValueError(f"Missing verification hashes for {path}")
        target = DEPENDENCY / path
        if target.is_symlink() or not target.resolve().is_relative_to(DEPENDENCY.resolve()):
            raise ValueError(f"Unsafe dependency path: {path}")
        current = target.read_bytes()
        if not blob_hash(current).startswith(updated):
            pending.append((path, current, original, updated, section))
    if not pending:
        return

    ensure_base_objects(original for _, _, original, _, _ in pending)
    known = historical_hashes()
    changes = []
    for path, current, original, updated, section in pending:
        allowed = known.get(path, set()) | {original}
        if not any(blob_hash(current).startswith(value) for value in allowed):
            print(f"[libntr] Preserving local changes or unknown patch version: {path}", file=sys.stderr)
            continue
        base = git(DEPENDENCY, "cat-file", "blob", original)
        try:
            replacement = apply_section(base, section)
        except ValueError as error:
            raise ValueError(f"{path}: {error}; no files changed") from error
        if not blob_hash(replacement).startswith(updated):
            raise ValueError(f"Patch result verification failed for {path}; no files changed")
        changes.append((path, current, replacement))

    # Complete validation before writing anything. Only changed source files
    # are written, so the normal incremental build recompiles their users.
    for path, current, _ in changes:
        if (DEPENDENCY / path).read_bytes() != current:
            raise ValueError(f"Dependency changed during synchronization: {path}")
    for path, _, replacement in changes:
        (DEPENDENCY / path).write_bytes(replacement)
        print(f"[libntr] Updated bundled patch: {path}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"[libntr] {error}", file=sys.stderr)
        sys.exit(1)
