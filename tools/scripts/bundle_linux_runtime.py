#!/usr/bin/env python3
"""Complete an AppDir, including dlopen dependencies and its matching C runtime."""

from pathlib import Path
import re
import shutil
import subprocess
import sys


def output(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=True)
    return result.stdout


def bundle(appdir, builddir, projectdir):
    libdir = appdir / "usr/lib"
    manifest = {}
    pending = []
    scanned = set()

    def add(source, name=None):
        source = Path(source)
        name = name or source.name
        target = libdir / name
        if not target.exists():
            shutil.copy2(source, target)
        origin = source.resolve()
        if origin.is_relative_to(appdir) and name in libraries:
            origin = Path(libraries[name]).resolve()
        manifest[name] = str(origin)
        pending.append(target)

    # These libraries are dlopen'ed by SDL and are invisible to ELF NEEDED.
    # GPU vendor drivers remain supplied by the machine driving the display.
    libraries = {}
    for line in output("ldconfig", "-p").splitlines():
        match = re.match(r"\s*(\S+) \([^)]*x86-64[^)]*\) => (.+)", line)
        if match:
            libraries.setdefault(match[1], match[2])
    required = ["libSDL3.so.0", "libwayland-client.so.0", "libwayland-cursor.so.0",
                "libwayland-egl.so.1", "libxkbcommon.so.0", "libX11.so.6",
                "libXext.so.6", "libXcursor.so.1", "libXi.so.6", "libXfixes.so.3",
                "libXrandr.so.2", "libXrender.so.1", "libXss.so.1",
                "libpulse.so.0", "libasound.so.2", "libGL.so.1", "libEGL.so.1"]
    for name in required:
        if name not in libraries:
            raise RuntimeError(f"Required runtime dependency is unavailable: {name}")
        add(libraries[name], name)
    for name in ["libresolv.so.2", "libnss_dns.so.2", "libnss_files.so.2"]:
        if name in libraries:
            add(libraries[name], name)

    pending += [p for p in appdir.rglob("*") if p.is_file() and not p.is_symlink()
                and p.open("rb").read(4) == b"\x7fELF"]
    while pending:
        binary = pending.pop()
        if binary in scanned:
            continue
        scanned.add(binary)
        dependencies = output("ldd", str(binary))
        if "not found" in dependencies:
            raise RuntimeError(f"Unresolved dependencies for {binary}:\n{dependencies}")
        for line in dependencies.splitlines():
            match = re.match(r"\s*(\S+) => (/.*?) \(0x", line)
            if match:
                name, source = match.groups()
                add(source, name)
            else:
                match = re.match(r"\s*(/\S+) \(0x", line)
                if match:
                    add(match[1])

    # The loader must be from the same installation as the bundled libc.
    loader = libdir / "ld-linux-x86-64.so.2"
    if not loader.is_file() or not (libdir / "libc.so.6").is_file():
        raise RuntimeError("The matching glibc and ELF loader were not bundled")

    defaults = appdir / "usr/share/pokeplatinum/defaults"
    defaults.mkdir(parents=True)
    shutil.copy2(projectdir / "icon.png", defaults / "icon.png")
    retired = {"quicksave_f5", "restart_f9", "bag_sort", "poketch_keyboard",
               "speed", "camera_distance", "autohm"}
    for source in (builddir / "mods").glob("*"):
        if source.suffix not in {".lua", ".so"} or source.stem in retired:
            continue
        if source.suffix == ".so" and source.with_suffix(".lua").exists():
            continue
        target = defaults / "mods" / source.stem
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target / source.name)
    for source in (projectdir / "mods").glob("*/*.ini"):
        target = defaults / "mods" / source.parent.name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target / source.name)
    for name in ["fast_ball_battle_frame.ncgr", "fast_ball_battle_frame.nclr",
                 "fast_ball_battle_cell.ncer", "fast_ball_battle_anim.nanr"]:
        source = builddir / "mods" / name
        if source.exists():
            target = defaults / "mods/fast_ball_battle"
            target.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target / name)
    target = defaults / "mods/level_cap"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(projectdir / "mods/level_cap/level_caps.json", target / "level_caps.json")
    licenses = appdir / "usr/share/licenses/pokeplatinum"
    licenses.mkdir(parents=True, exist_ok=True)
    for source in (builddir / "mods").glob("*-license.txt"):
        shutil.copy2(source, licenses / source.name)
    # Preserve distribution license notices for the actual bundled libraries.
    if shutil.which("pacman"):
        for source in sorted(set(manifest.values())):
            result = subprocess.run(["pacman", "-Qqo", source], text=True,
                                    capture_output=True)
            if result.returncode == 0:
                for package in result.stdout.splitlines():
                    notices = Path("/usr/share/licenses") / package
                    if notices.is_dir():
                        shutil.copytree(notices, licenses / package, dirs_exist_ok=True)
        common = Path("/usr/share/licenses/common")
        if common.is_dir():
            shutil.copytree(common, licenses / "common", dirs_exist_ok=True)
    (appdir / "usr/share/pokeplatinum/runtime-libraries.txt").write_text(
        "\n".join(sorted(p.name for p in libdir.iterdir())) + "\n")
    print(f"Bundled runtime: {len(list(libdir.iterdir()))} libraries, including Qt, SDL3 and glibc")


if __name__ == "__main__":
    bundle(*(Path(arg).resolve() for arg in sys.argv[1:]))
