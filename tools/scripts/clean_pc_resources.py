"""Remove generated ROM resources from a desktop release, preserving mods and user files."""
from pathlib import Path
import csv
import shutil
import sys


def clean(output, manifest):
    output = Path(output)
    roots = set()
    with Path(manifest).open(newline='') as source:
        for row in csv.DictReader(source):
            path = Path(row['Target File'].lstrip('/'))
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Invalid resource destination')
            if len(path.parts) > 1:
                roots.add(path.parts[0])
    roots.add(output.name + '.AppDir')
    roots.difference_update(('mods', 'assets'))
    for name in roots:
        path = output / name
        if path.is_symlink():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)
    for name in ('header.bin', 'sim_config_prj.ini'):
        obsolete = output / name
        if obsolete.is_file() or obsolete.is_symlink():
            obsolete.unlink()


if __name__ == '__main__':
    clean(sys.argv[1], sys.argv[2])
