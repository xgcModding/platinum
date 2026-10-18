#!/usr/bin/env python3
"""Check the final AppImage, including startup without host Qt/SDL libraries.

Run with a desktop session available. All writable game data stays in a new
directory under .build/build_linux/.build-temp; no existing save is loaded.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time


def check(image, work):
    work.mkdir(parents=True, exist_ok=True)
    probe = Path(tempfile.mkdtemp(prefix="appimage-check-", dir=work)).resolve()
    copied = probe / "pokeplatinum.AppImage"
    shutil.copy2(image, copied)
    print(f"Diagnostic files: {probe}", flush=True)
    with (probe / "extraction.log").open("w") as log:
        subprocess.run([str(copied), "--appimage-extract"], cwd=probe,
                       stdout=log, stderr=log, check=True)
    appdir = probe / "squashfs-root"
    libdir = appdir / "usr/lib"

    # Every direct dependency of every bundled ELF must be in the image.
    binaries = []
    for path in appdir.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as file:
            if file.read(4) != b"\x7fELF":
                continue
        binaries.append(path)
        dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
        for name in re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic):
            if not (libdir / name).is_file():
                raise RuntimeError(f"Unbundled dependency: {path}: {name}")
    print(f"ELF dependency closure: {len(binaries)} binaries passed", flush=True)

    if not shutil.which("bwrap"):
        raise RuntimeError("bubblewrap is required for the isolated dependency test")
    # The bundled loader must resolve the executable even with the entire
    # host library directory hidden, including its libc and C++ runtime.
    library_roots = sorted({str(Path(p).resolve()) for p in
                            ["/lib", "/lib64", "/usr/lib", "/usr/lib64"] if Path(p).is_dir()})
    no_host = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/", "--proc", "/proc"]
    for root in library_roots:
        no_host += ["--tmpfs", root]
    resolved = subprocess.check_output(no_host + [str(libdir / "ld-linux-x86-64.so.2"),
        "--inhibit-cache", "--library-path", str(libdir), "--list", str(appdir / "usr/bin/app")],
        text=True)
    (probe / "no-host-libraries.log").write_text(resolved)
    if "not found" in resolved:
        raise RuntimeError("The executable still requires host runtime libraries")
    print("Private C/C++ runtime: passed with all host library directories hidden", flush=True)

    env = os.environ.copy()
    env.update(APPIMAGE=str(copied), APPDIR=str(appdir), TMPDIR=str(probe),
               QT_FORCE_STDERR_LOGGING="1", QT_DEBUG_PLUGINS="1")
    env.pop("APPIMAGE_EXTRACT_AND_RUN", None)
    env.pop("POKEPLATINUM_MOD_DIR", None)
    env.pop("POKEPLATINUM_VALIDATE_MODS", None)
    expected = {p.stem for p in (appdir / "usr/share/pokeplatinum/defaults/mods").glob("*/*.lua")}

    def startup(name, command, settings):
        logpath = probe / (name + ".log")
        with logpath.open("w") as log:
            process = subprocess.Popen(command, env=env | settings, cwd=probe,
                                       stdout=log, stderr=log, start_new_session=True)
            try:
                deadline = time.monotonic() + 12
                ready = False
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"{name}: exited {process.returncode}; see {logpath}")
                    contents = logpath.read_text(errors="replace")
                    if "[display] Independent presentation active" in contents:
                        ready = True
                        time.sleep(2)
                        break
                    time.sleep(0.2)
                if not ready or process.poll() is not None:
                    raise RuntimeError(f"{name}: rendering did not stay active; see {logpath}")
                contents = logpath.read_text(errors="replace")
                loaded = set(re.findall(r"\[mods\] ([^:]+): loaded", contents))
                if not expected <= loaded or "onLoad rejected" in contents:
                    raise RuntimeError(f"{name}: mods failed: {expected - loaded}; see {logpath}")
                if "cannot load:" in contents or "QLibraryPrivate::loadPlugin failed" in contents:
                    raise RuntimeError(f"{name}: plugin load failed; see {logpath}")
                print(f"{name}: renderer active, {len(loaded)} mods loaded", flush=True)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()

    # This runs the actual final file, including its FUSE runtime and AppRun.
    startup("appimage-wayland", [str(copied)], {"QT_QPA_PLATFORM": "wayland"})

    isolated = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
                "--dev-bind", "/dev", "/dev", "--proc", "/proc",
                "--bind", str(probe), str(probe)]
    hidden = set()
    for name in [p.name for p in libdir.iterdir()
                 if p.name.startswith(("libQt6", "libSDL"))]:
        for directory in [Path("/usr/lib"), Path("/usr/lib/x86_64-linux-gnu")]:
            host = directory / name
            if host.exists():
                hidden.add(str(host.resolve()))
    for path in sorted(hidden):
        isolated += ["--ro-bind", "/dev/null", path]
    for directory in ["/usr/lib/qt6", "/usr/lib/x86_64-linux-gnu/qt6"]:
        if Path(directory).exists():
            isolated += ["--tmpfs", directory]
    # Deliberately pass host Qt paths: AppRun must discard them.
    poison = {"QT_PLUGIN_PATH": "/usr/lib/qt6/plugins",
              "QT_QPA_PLATFORM_PLUGIN_PATH": "/usr/lib/qt6/plugins/platforms"}
    for backend in ["wayland", "xcb"]:
        startup(f"isolated-{backend}", isolated + [str(appdir / "AppRun")],
                poison | {"QT_QPA_PLATFORM": backend})
    print("PASS: packaged startup and Qt/SDL isolation (Wayland and X11)", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--work", type=Path, default=Path(".build/build_linux/.build-temp"))
    args = parser.parse_args()
    check(args.image.resolve(), args.work.resolve())
