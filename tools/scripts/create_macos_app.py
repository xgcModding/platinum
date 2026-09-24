#!/usr/bin/env python3
"""Package a relocatable macOS app; user data stays beside the .app.

Only build products and their dynamic dependencies enter the bundle. Never
copy an entire release directory: it can contain the user's ROM and saves.
"""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

APP_NAME = "Pokémon Platinum.app"
SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def dependencies(binary):
    return [line.strip().split(" (compatibility version", 1)[0]
            for line in run("otool", "-L", str(binary)).splitlines()[1:]]


def rpaths(binary):
    lines = run("otool", "-l", str(binary)).splitlines()
    return [lines[i+2].strip().split(" (offset", 1)[0].removeprefix("path ")
            for i, line in enumerate(lines) if line.strip() == "cmd LC_RPATH"]


def expand(path, source, executable):
    return Path(path.replace("@loader_path", str(source.parent))
                    .replace("@executable_path", str(executable.parent)))


def resolve(name, source, executable, executable_rpaths):
    if name.startswith("@rpath/"):
        suffix = name[len("@rpath/"):]
        candidates = [expand(path, source, executable)/suffix for path in rpaths(source)]
        candidates += [expand(path, executable, executable)/suffix for path in executable_rpaths]
    else:
        candidates = [expand(name, source, executable)]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise RuntimeError(f"Cannot locate {name}, required by {source}")


def create_icon(source, destination, temporary):
    # Keep icon.png untouched. Render the ICNS on the macOS icon canvas:
    # a fully filled rounded silhouette avoids Tahoe's gray backplate.
    # AppKit is built into macOS; no image library or Xcode is required.
    padded = Path(temporary)/"macos-icon.png"
    subprocess.run(["osascript", "-l", "JavaScript", "-", str(source), str(padded)],
                   input=r"""
ObjC.import('AppKit');
function run(args) {
    const source = $.NSImage.alloc.initWithContentsOfFile(args[0]);
    if (!source) throw Error('Cannot load app icon: ' + args[0]);
    const icon = $.NSImage.alloc.initWithSize($.NSMakeSize(1024, 1024));
    icon.lockFocus;
    $.NSColor.clearColor.set;
    $.NSRectFill($.NSMakeRect(0, 0, 1024, 1024));
    $.NSBezierPath.bezierPathWithRoundedRectXRadiusYRadius(
        $.NSMakeRect(100, 100, 824, 824), 185, 185).addClip;
    // Slight bleed fills the silhouette despite the source's transparent edge.
    source.drawInRectFromRectOperationFraction(
        $.NSMakeRect(80, 80, 864, 864), $.NSZeroRect,
        $.NSCompositingOperationSourceOver, 1);
    icon.unlockFocus;
    const bitmap = $.NSBitmapImageRep.imageRepWithData(icon.TIFFRepresentation);
    if (!bitmap.representationUsingTypeProperties($.NSPNGFileType, $({}))
        .writeToFileAtomically(args[1], true)) throw Error('Cannot write app icon');
}
""", text=True, check=True, stdout=subprocess.DEVNULL)
    iconset = Path(temporary)/"app.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
            subprocess.run(["sips", "-z", str(size*scale), str(size*scale),
                            str(padded), "--out", str(iconset/name)],
                           check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(destination)], check=True)


def package(executable, output, icon):
    executable = executable.resolve(strict=True)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    destination = output/APP_NAME
    executable_rpaths = rpaths(executable)
    with tempfile.TemporaryDirectory(prefix=".macos-app-", dir=output) as temporary:
        app = Path(temporary)/APP_NAME
        macos, frameworks, resources = (app/"Contents"/name for name in ("MacOS", "Frameworks", "Resources"))
        for folder in (macos, frameworks, resources):
            folder.mkdir(parents=True)
        shutil.copy2(executable, macos/"main")
        create_icon(icon.resolve(strict=True), resources/"app-icon.icns", temporary)
        pending = [(executable, macos/"main")]
        copied, filenames = {}, {}

        def include(source, filename=None):
            source = source.resolve(strict=True)
            if source in copied:
                return copied[source]
            filename = filename or source.name
            if filename in filenames and filenames[filename] != source:
                raise RuntimeError(f"Conflicting libraries named {filename}")
            target = frameworks/filename
            shutil.copy2(source, target)
            target.chmod(target.stat().st_mode | 0o200)
            copied[source] = target
            filenames[filename] = source
            pending.append((source, target))
            return target

        processed = []
        while pending:
            source, target = pending.pop(0)
            command = ["install_name_tool"]
            if target.parent == frameworks:
                command += ["-id", "@loader_path/"+target.name]
            for dependency in dependencies(source):
                if dependency.startswith(SYSTEM_PREFIXES):
                    continue
                library = resolve(dependency, source, executable, executable_rpaths)
                # A dylib's LC_ID_DYLIB is also printed by otool -L.
                if library == source:
                    continue
                bundled = include(library)
                prefix = "@executable_path/../Frameworks/" if target.parent == macos else "@loader_path/"
                command += ["-change", dependency, prefix+bundled.name]
            # sdl2-compat dlopens SDL3, so that dependency is absent from otool.
            if b"SDL2COMPAT_" in source.read_bytes() and b"libSDL3.dylib" in source.read_bytes():
                sdl3 = Path(run("pkg-config", "--variable=libdir", "sdl3"))/"libSDL3.dylib"
                include(sdl3, "libSDL3.dylib")
            for path in rpaths(source):
                command += ["-delete_rpath", path]
            if len(command) > 1:
                subprocess.run(command+[str(target)], check=True)
            processed.append(target)

        info = {
            "CFBundleExecutable": "main", "CFBundleName": "Pokémon Platinum",
            "CFBundleDisplayName": "Pokémon Platinum", "CFBundleIdentifier": "org.pokeplatinum.pcport",
            "CFBundlePackageType": "APPL", "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleVersion": "2", "CFBundleShortVersionString": "1.0",
            "CFBundleIconFile": "app-icon.icns", "NSHighResolutionCapable": True,
            "NSPrincipalClass": "NSApplication",
        }
        with (app/"Contents/Info.plist").open("wb") as stream:
            plistlib.dump(info, stream)
        (app/"Contents/PkgInfo").write_bytes(b"APPL????")
        # Sign after all Mach-O edits. Ad-hoc signing supports local execution;
        # release notarization requires the distributor's Developer ID.
        for target in processed:
            if target.parent == frameworks:
                subprocess.run(["codesign", "--force", "--sign", "-", str(target)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        # Preserve the previous working app if packaging/signing fails.
        previous = Path(temporary)/"previous.app"
        if destination.exists():
            destination.rename(previous)
        try:
            app.rename(destination)
        except BaseException:
            if previous.exists():
                previous.rename(destination)
            raise
    # The bundle is replaced atomically above. Refresh LaunchServices so Finder
    # does not retain the previous bundle's file identity after a rebuild.
    lsregister = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                      "LaunchServices.framework/Support/lsregister")
    subprocess.run([str(lsregister), "-f", str(destination)], check=True)
    print(f"Created {destination}")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--icon", type=Path, default=Path(__file__).resolve().parents[2]/"icon.png")
    args = parser.parse_args()
    package(args.executable, args.output, args.icon)
