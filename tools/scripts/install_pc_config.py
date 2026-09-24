"""Create missing desktop configuration files without changing existing settings."""
from pathlib import Path
import sys


def prepare(output, cache):
    output, cache = Path(output).resolve(), Path(cache).resolve()
    legacy = output / 'pokeplatinum'
    if legacy.is_dir():
        # Preserve the entire old layout before moving the actual release out.
        # This also keeps any files the user stored among compiler outputs.
        backup = cache.with_name(cache.name + '.previous')
        while backup.exists() or backup.is_symlink():
            backup = backup.with_name(backup.name + '.previous')
        links = [(path.relative_to(legacy), path.resolve(), path.is_dir())
                 for path in legacy.rglob('*') if path.is_symlink()]
        backup.parent.mkdir(parents=True, exist_ok=True)
        output.rename(backup)
        (backup / 'pokeplatinum').rename(output)
        for relative, target, directory in links:
            if target.is_relative_to(legacy):
                target = output / target.relative_to(legacy)
            elif target.is_relative_to(output):
                target = backup / target.relative_to(output)
            link = output / relative
            link.unlink()
            link.symlink_to(target, target_is_directory=directory)
    output.mkdir(parents=True, exist_ok=True)


def install(output, defaults):
    output, defaults = Path(output), Path(defaults)
    for name in ('game.ini', 'sim_config.ini'):
        destination = output / name
        # Exclusive creation also preserves a file created concurrently.
        try:
            with destination.open('xb') as target:
                target.write((defaults / name).read_bytes())
        except FileExistsError:
            pass


if __name__ == '__main__':
    if len(sys.argv) == 4:
        prepare(sys.argv[1], sys.argv[3])
    install(sys.argv[1], sys.argv[2])
