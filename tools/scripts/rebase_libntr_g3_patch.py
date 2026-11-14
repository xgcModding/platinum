#!/usr/bin/env python3
"""Rebase only the g3_handler.cpp section of the pinned libntr Meson patch.

The checked-in patch was generated against g3_handler.cpp text with extra blank
lines. On a clean upstream checkout Meson's `git apply` rejects its BoxTest hunk.
This CI pre-step verifies every NONBLANK preimage line before regenerating only
this one diff. Other patch sections and gameplay source remain untouched.
"""

import difflib
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'subprojects/packagefiles/libntr-pokeplatinum.patch'
WRAP = ROOT / 'subprojects/libntr.wrap'
PATH = 'libraries/sim/src/g3_handler.cpp'
HEADER = f'diff --git a/{PATH} b/{PATH}\n'


def blob(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()


def parse_hunks(section):
    pattern = re.compile(r'(?m)^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n')
    found = list(pattern.finditer(section))
    if not found:
        raise ValueError('No g3_handler hunks found')
    hunks = []
    for n, match in enumerate(found):
        end = found[n + 1].start() if n + 1 < len(found) else len(section)
        chunk = section[match.end():end]
        old, new = [], []
        for line in chunk.splitlines(keepends=True):
            if line.startswith('\\ No newline at end of file'):
                raise ValueError('Unsupported no-newline marker in g3_handler hunk')
            if not line or line[0] not in ' +-':
                raise ValueError('Unexpected line inside g3_handler hunk')
            if line[0] in ' -':
                old.append(line[1:])
            if line[0] in ' +':
                new.append(line[1:])
        expected_old = int(match[2] or '1')
        expected_new = int(match[4] or '1')
        if len(old) != expected_old or len(new) != expected_new:
            raise ValueError('Malformed g3_handler hunk length')
        hunks.append((int(match[1]), old, new))
    return hunks


def apply_whitespace_tolerant(original, hunks):
    lines = original.splitlines(keepends=True)
    out, cursor = [], 0
    for nominal, old, new in hunks:
        nonblank_old = [s for s in old if s.strip()]
        if not nonblank_old:
            raise ValueError('Refusing a blank-only hunk')
        # All nonblank preimage lines must match EXACTLY in source order.
        positions = [i for i in range(cursor, len(lines)) if lines[i].strip()]
        candidates = []
        for offset, source_pos in enumerate(positions):
            if positions[offset:offset + len(nonblank_old)] and len(positions[offset:offset + len(nonblank_old)]) == len(nonblank_old):
                source = [lines[pos] for pos in positions[offset:offset + len(nonblank_old)]]
                if source == nonblank_old:
                    candidates.append((abs(source_pos - (nominal - 1)), source_pos, positions[offset + len(nonblank_old) - 1] + 1))
        if not candidates:
            raise ValueError(f'g3_handler patch preimage mismatch near original line {nominal}; source changed; stopping safely')
        _, start, end = min(candidates)
        # If an old hunk starts with a blank line, anchor its leading blanks
        # only when the real file contains them; never cross nonblank lines.
        if start < cursor or end > len(lines):
            raise ValueError('Overlapping or invalid g3_handler hunks')
        out.extend(lines[cursor:start])
        out.extend(new)
        cursor = end
    out.extend(lines[cursor:])
    return ''.join(out).encode('utf-8')


def main():
    wrap = WRAP.read_text(encoding='utf-8')
    revision_match = re.search(r'(?m)^revision\s*=\s*([0-9a-f]{40})\s*$', wrap)
    if not revision_match:
        raise ValueError('libntr revision is not pinned to a SHA-1')
    revision = revision_match[1]
    url = f'https://raw.githubusercontent.com/cybervisi0n/libntr/{revision}/{PATH}'
    with urlopen(url, timeout=30) as resp:
        original = resp.read()
    if b'\r\n' in original or not original.endswith(b'\n'):
        raise ValueError('Unexpected pinned g3_handler source line endings')

    content = PATCH.read_text(encoding='utf-8')
    if content.count(HEADER) != 1:
        raise ValueError('Expected exactly one g3_handler patch section')
    before, remainder = content.split(HEADER, 1)
    match = re.search(r'(?m)^diff --git ', remainder)
    if match is None:
        raise ValueError('Expected patch sections after g3_handler')
    section, after = remainder[:match.start()], remainder[match.start():]
    hunks = parse_hunks(section)
    updated = apply_whitespace_tolerant(original.decode('utf-8'), hunks)
    if original == updated:
        raise ValueError('No changes generated')
    regenerated = list(difflib.unified_diff(original.decode('utf-8').splitlines(keepends=True),
                                            updated.decode('utf-8').splitlines(keepends=True),
                                            fromfile=f'a/{PATH}', tofile=f'b/{PATH}', n=3))
    patch_section = HEADER + f'index {blob(original)}..{blob(updated)} 100644\n' + ''.join(regenerated)

    # Validate that standard Git can apply the regenerated hunk exactly.
    with tempfile.TemporaryDirectory(prefix='lake-trio-g3-') as td:
        source = Path(td) / PATH
        source.parent.mkdir(parents=True)
        source.write_bytes(original)
        patchfile = Path(td) / 'verify.patch'
        patchfile.write_text(patch_section, encoding='utf-8', newline='\n')
        check = subprocess.run(['git', 'apply', '--check', str(patchfile)], cwd=td, capture_output=True, text=True)
        if check.returncode:
            raise ValueError(f'Regenerated g3 patch failed git apply --check: {check.stderr}')
        apply = subprocess.run(['git', 'apply', str(patchfile)], cwd=td, capture_output=True, text=True)
        if apply.returncode or source.read_bytes() != updated:
            raise ValueError('Regenerated g3 patch did not reproduce intended output')

    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', prefix='.libntr-g3-',
                                     suffix='.tmp', dir=PATCH.parent, delete=False) as f:
        tmp = Path(f.name)
        f.write(before + patch_section + after)
    tmp.replace(PATCH)
    print(f'[libntr] Regenerated g3_handler patch against pinned {revision[:12]}')
    print(f'[libntr] Verified {len(hunks)} hunks against exact nonblank source and git apply --check')
    print('[libntr] Only subprojects/packagefiles/libntr-pokeplatinum.patch changed in CI workspace')


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print(f'[libntr] Refusing unsafe patch regeneration: {e}', file=sys.stderr)
        sys.exit(1)
