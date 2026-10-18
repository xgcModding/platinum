#!/bin/bash
set -eo pipefail
BUILDDIR=$1
OUTPUTDIR=$3
PROJECTDIR=$4
mkdir -p "$OUTPUTDIR"

python3 "$PROJECTDIR/tools/scripts/clean_pc_resources.py" "$OUTPUTDIR" "$2"
python3 "$PROJECTDIR/tools/scripts/install_pc_config.py" "$OUTPUTDIR" "$PROJECTDIR/tools/config"
# The simulator needs writable settings storage, not a bundled firmware dump.
if [ ! -e "$OUTPUTDIR/firmware.bin" ]; then
  dd if=/dev/zero of="$OUTPUTDIR/firmware.bin" bs=1584 count=1 2>/dev/null
  printf '\300\000' | dd of="$OUTPUTDIR/firmware.bin" bs=1 seek=32 conv=notrunc 2>/dev/null
fi
# Replace the directory entry so an open game can keep its current executable
# while the next launch uses the new build. Never package a failed copy.
MainStaging=$(mktemp "$OUTPUTDIR/.main.XXXXXX")
trap 'rm -f "$MainStaging"' EXIT
Executable=main
[ ! -f "$BUILDDIR/main.exe" ] || Executable=main.exe
cp -p "$BUILDDIR/$Executable" "$MainStaging"
mv -f "$MainStaging" "$OUTPUTDIR/$Executable"

mkdir -p "$OUTPUTDIR/mods"
# Item icons now come from the user's imported item archive.
rm -f "$OUTPUTDIR/mods/fast_ball_battle/fast_ball_battle_ball.ncgr" \
      "$OUTPUTDIR/mods/fast_ball_battle/fast_ball_battle_ball.nclr"

# These shortcuts are built into the game; discard only their retired modules.
for Builtin in quicksave_f5 restart_f9 bag_sort poketch_keyboard speed camera_distance autohm; do
  for ModExtension in so dylib dll; do
    rm -f "$OUTPUTDIR/mods/$Builtin.$ModExtension" "$OUTPUTDIR/mods/$Builtin/$Builtin.$ModExtension"
  done
done
for ModExtension in so dylib dll; do
  if ! compgen -G "$BUILDDIR/mods/*.$ModExtension" > /dev/null; then
    continue
  fi
  for ModFile in "$BUILDDIR"/mods/*.$ModExtension; do
    ModName=$(basename "$ModFile" .$ModExtension)
    case "$ModName" in quicksave_f5|restart_f9|bag_sort|poketch_keyboard|speed|camera_distance|autohm) continue ;; esac
    [ ! -f "$BUILDDIR/mods/$ModName.lua" ] || continue
    mkdir -p "$OUTPUTDIR/mods/$ModName"
    ModStaging=$(mktemp "$OUTPUTDIR/mods/$ModName/.module.XXXXXX")
    if ! cp -p "$ModFile" "$ModStaging" || ! mv -f "$ModStaging" "$OUTPUTDIR/mods/$ModName/$ModName.$ModExtension"; then
      rm -f "$ModStaging"
      exit 1
    fi
    rm -f "$OUTPUTDIR/mods/$ModName.$ModExtension"
  done
done

# Every platform keeps configuration beside its module. Never overwrite an
# existing configuration; preserve differing flat copies before removing them.
for LegacyConfig in "$OUTPUTDIR"/mods/*.ini; do
  [ -f "$LegacyConfig" ] || continue
  ModName=$(basename "$LegacyConfig" .ini)
  mkdir -p "$OUTPUTDIR/mods/$ModName"
  ModConfig="$OUTPUTDIR/mods/$ModName/$ModName.ini"
  if [ ! -e "$ModConfig" ]; then
    mv "$LegacyConfig" "$ModConfig" || exit 1
  elif cmp -s "$LegacyConfig" "$ModConfig"; then
    rm -f "$LegacyConfig"
  else
    PreservedConfig="$ModConfig.previous-flat"
    while [ -e "$PreservedConfig" ]; do
      PreservedConfig="$PreservedConfig.bak"
    done
    mv "$LegacyConfig" "$PreservedConfig" || exit 1
  fi
done

# Lua modules carry no platform-specific native libraries.
for LuaFile in "$BUILDDIR"/mods/*.lua; do
  [ -f "$LuaFile" ] || continue
  ModName=$(basename "$LuaFile" .lua)
  mkdir -p "$OUTPUTDIR/mods/$ModName"
  cp "$LuaFile" "$OUTPUTDIR/mods/$ModName/$ModName.lua"
  for ModExtension in so dylib dll; do
    rm -f "$OUTPUTDIR/mods/$ModName.$ModExtension" "$OUTPUTDIR/mods/$ModName/$ModName.$ModExtension"
  done
done
if [ -f "$BUILDDIR/mods/lua-license.txt" ]; then
  mkdir -p "$OUTPUTDIR/licenses"
  cp "$BUILDDIR/mods/lua-license.txt" "$OUTPUTDIR/licenses/Lua.txt"
  cp "$BUILDDIR/mods/libffi-license.txt" "$OUTPUTDIR/licenses/libffi.txt"
fi

# Install shipped mod defaults on a fresh build, preserving existing settings.
for DefaultConfig in "$PROJECTDIR"/mods/*/*.ini; do
  [ -f "$DefaultConfig" ] || continue
  ModName=$(basename "$(dirname "$DefaultConfig")")
  mkdir -p "$OUTPUTDIR/mods/$ModName"
  if [ ! -e "$OUTPUTDIR/mods/$ModName/$(basename "$DefaultConfig")" ]; then
    cp "$DefaultConfig" "$OUTPUTDIR/mods/$ModName/" || exit 1
  fi
done

for ResourceFile in \
  fast_ball_battle_frame.ncgr \
  fast_ball_battle_frame.nclr \
  fast_ball_battle_cell.ncer \
  fast_ball_battle_anim.nanr; do
  if [ -f "$BUILDDIR/mods/$ResourceFile" ]; then
    mkdir -p "$OUTPUTDIR/mods/fast_ball_battle"
    cp "$BUILDDIR/mods/$ResourceFile" "$OUTPUTDIR/mods/fast_ball_battle/$ResourceFile" || exit 1
    rm -f "$OUTPUTDIR/mods/$ResourceFile"
  fi
done

mkdir -p "$OUTPUTDIR/mods/level_cap"
if [ ! -e "$OUTPUTDIR/mods/level_cap/level_caps.json" ]; then
  if [ -e "$OUTPUTDIR/mods/level_caps.json" ]; then
    mv "$OUTPUTDIR/mods/level_caps.json" "$OUTPUTDIR/mods/level_cap/level_caps.json" || exit 1
  else
    cp "$PROJECTDIR/mods/level_cap/level_caps.json" "$OUTPUTDIR/mods/level_cap/level_caps.json" || exit 1
  fi
elif [ -e "$OUTPUTDIR/mods/level_caps.json" ]; then
  if cmp -s "$OUTPUTDIR/mods/level_caps.json" "$OUTPUTDIR/mods/level_cap/level_caps.json"; then
    rm -f "$OUTPUTDIR/mods/level_caps.json"
  else
    PreservedConfig="$OUTPUTDIR/mods/level_cap/level_caps.json.previous-flat"
    while [ -e "$PreservedConfig" ]; do
      PreservedConfig="$PreservedConfig.bak"
    done
    mv "$OUTPUTDIR/mods/level_caps.json" "$PreservedConfig" || exit 1
  fi
fi

if [[ "$(uname -s)" =~ ^(MSYS|MINGW64|UCRT64)_NT.* ]]; then
  for Library in SDL2 libwinpthread-1 libenet-7 libgcc_s_seh-1 libstdc++-6 libffi-8; do
    # An unchanged DLL may be in use by the running game.
    if ! python3 -c 'import filecmp, os, sys; sys.exit(0 if os.path.isfile(sys.argv[2]) and filecmp.cmp(*sys.argv[1:], shallow=False) else 1)' "$(cygpath -m "/ucrt64/bin/$Library.dll")" "$(cygpath -m "$OUTPUTDIR/$Library.dll")"; then
      cp "/ucrt64/bin/$Library.dll" "$OUTPUTDIR/$Library.dll"
    fi
  done
elif [[ "$(uname -s)" == "Darwin" && "${SKIP_MACOS_APP:-0}" != "1" ]]; then
  python3 "$PROJECTDIR/tools/scripts/create_macos_app.py" "$BUILDDIR/main" "$OUTPUTDIR"
  # Keep the compiler executable; the release launches through Finder.
  rm -f "$OUTPUTDIR/main"
elif [[ "$(uname -s)" == "Linux" && "${SKIP_APPIMAGE:-0}" != "1" ]]; then
  AppDir="$BUILDDIR/.build-temp/pokeplatinum.AppDir"
  LinuxDeploy="$PROJECTDIR/tools/linuxdeploy/linuxdeploy-x86_64.AppImage"
  # Never retain Qt libraries from an earlier build after system updates.
  rm -rf "$AppDir"
  mkdir -p "$AppDir/usr/bin" "$AppDir/usr/lib"
  cp "$BUILDDIR/main" "$AppDir/usr/bin/app"
  cp "$PROJECTDIR/icon.png" "$OUTPUTDIR/icon.png"
  cp "$PROJECTDIR/icon.png" "$BUILDDIR/icon.png"
  cp "$PROJECTDIR/icon.png" "$AppDir/pokeplatinum.png"
  # Remove stale metadata from earlier packages with the generic app identity.
  rm -f "$AppDir/App.desktop" "$AppDir/usr/share/applications/App.desktop" "$AppDir/.DirIcon"
  cp "$PROJECTDIR/tools/linuxdeploy/App.desktop" "$BUILDDIR/pokeplatinum.desktop"
  if [ ! -f "$LinuxDeploy" ]; then
    echo "Downloading linuxdeploy..."
    wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O "$LinuxDeploy"
    chmod +x "$LinuxDeploy"
  fi
  # Qt platform plugins are loaded dynamically, so ELF dependency scanning
  # alone cannot discover them. Deploy their dependencies in place so
  # linuxdeploy sets the RUNPATH on the actual plugins Qt will load.
  QtPlugins=$(qtpaths6 --query QT_INSTALL_PLUGINS 2>/dev/null || /usr/lib/qt6/bin/qtpaths --query QT_INSTALL_PLUGINS)
  QtLibraries=()
  for Plugin in platforms/libqwayland.so platforms/libqxcb.so \
      wayland-shell-integration/libxdg-shell.so \
      wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
      platformthemes/libqxdgdesktopportal.so \
      platformthemes/KDEPlasmaPlatformTheme6.so; do
    mkdir -p "$AppDir/usr/plugins/$(dirname "$Plugin")"
    cp "$QtPlugins/$Plugin" "$AppDir/usr/plugins/$Plugin"
    QtLibraries+=("--deploy-deps-only=$AppDir/usr/plugins/$Plugin")
  done
  # Plasma's platform theme reads the user's palette and selected widget
  # style. These plugins are dynamically loaded and invisible to ldd.
  # Bundle the available styles so the selected one is not replaced by Fusion.
  for Style in "$QtPlugins"/styles/*.so; do
    [ -f "$Style" ] || continue
    mkdir -p "$AppDir/usr/plugins/styles"
    cp "$Style" "$AppDir/usr/plugins/styles/"
    QtLibraries+=("--deploy-deps-only=$AppDir/usr/plugins/styles/$(basename "$Style")")
  done
  for Category in xcbglintegrations; do
    for PluginFile in "$QtPlugins/$Category"/*.so; do
      [ -f "$PluginFile" ] || continue
      mkdir -p "$AppDir/usr/plugins/$Category"
      cp "$PluginFile" "$AppDir/usr/plugins/$Category/"
      QtLibraries+=("--deploy-deps-only=$AppDir/usr/plugins/$Category/$(basename "$PluginFile")")
    done
  done
  for Plugin in imageformats/libqsvg.so imageformats/libqico.so \
      platforminputcontexts/libcomposeplatforminputcontextplugin.so \
      platforminputcontexts/libibusplatforminputcontextplugin.so; do
    [ -f "$QtPlugins/$Plugin" ] || continue
    mkdir -p "$AppDir/usr/plugins/$(dirname "$Plugin")"
    cp "$QtPlugins/$Plugin" "$AppDir/usr/plugins/$Plugin"
    QtLibraries+=("--deploy-deps-only=$AppDir/usr/plugins/$Plugin")
  done
  printf '[Paths]\nPrefix=..\nPlugins=plugins\n' > "$AppDir/usr/bin/qt.conf"
  (cd "$BUILDDIR" && NO_STRIP=1 "$LinuxDeploy" --appdir "$AppDir" \
    --executable="$OUTPUTDIR/main" --desktop-file="$BUILDDIR/pokeplatinum.desktop" \
    --icon-file="$AppDir/pokeplatinum.png" "${QtLibraries[@]}")
  python3 "$PROJECTDIR/tools/scripts/bundle_linux_runtime.py" "$AppDir" "$BUILDDIR" "$PROJECTDIR"
  # Replace linuxdeploy's executable symlink with the private-runtime entry point.
  rm -f "$AppDir/AppRun"
  cp "$PROJECTDIR/tools/linuxdeploy/AppRun" "$AppDir/AppRun"
  chmod +x "$AppDir/AppRun"
  # Package only after the runtime is complete. Running the deployment pass
  # again could strip or rewrite the bundled libc/loader.
  DeployTools="$BUILDDIR/.build-temp/linuxdeploy-tools"
  mkdir -p "$DeployTools"
  (cd "$DeployTools" && "$LinuxDeploy" --appimage-extract > /dev/null)
  (cd "$BUILDDIR" && "$DeployTools/squashfs-root/usr/bin/linuxdeploy-plugin-appimage" --appdir "$AppDir")
  mv "$BUILDDIR/pokeplatinum-x86_64.AppImage" "$OUTPUTDIR/pokeplatinum.AppImage"
  # Keep the compiler's executable; remove only the loose release copy.
  rm -f "$OUTPUTDIR/main"
fi
