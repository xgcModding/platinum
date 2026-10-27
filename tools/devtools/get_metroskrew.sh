#!/usr/bin/env bash

# Usage: Run this script from the project root and provide the following environment variables:
#   - SKREW_SYS - controls the distribution downloaded; either `windows`, `linux`, or `wine`
#   - SKREW_VER - the release version of the metroskrew archive to be downloaded
#   - SKREW_DIR - the subproject directory in which metroskrew shall be stored

set -euxo pipefail

case "$SKREW_SYS" in
windows)
    SKREW_EXT=".7z"
    if command -v 7z >/dev/null 2>&1; then
        EXTRACTOR=(7z x -y)
    elif command -v 7zz >/dev/null 2>&1; then
        EXTRACTOR=(7zz x -y)
    elif command -v bsdtar >/dev/null 2>&1; then
        EXTRACTOR=(bsdtar -xf)
    elif command -v p7zip >/dev/null 2>&1; then
        EXTRACTOR=(p7zip -d)
    else
        echo "Install 7-Zip or bsdtar to extract Metroskrew." >&2
        exit 1
    fi
    ;;

linux | wine)
    SKREW_EXT=".tar.xz"
    EXTRACTOR=(tar xf)
    ;;

*)
    echo "Unrecognized platform $SKREW_SYS"
    exit 1
    ;;
esac

TMP="/tmp"

SKREW="metroskrew"
SKREW_GH="https://github.com/mid-kid/$SKREW"
SKREW_ARC="metroskrew-$SKREW_SYS$SKREW_EXT"
SKREW_URL="$SKREW_GH/releases/download/$SKREW_VER/$SKREW_ARC"
SKREW_TMP="$TMP/$SKREW"

rm -rf "$SKREW_DIR"

# We extract the archive to `/tmp` to ensure that Windows does not muddle with permissions.
pushd "$TMP" >/dev/null
wget --quiet --show-progress -O "$SKREW_ARC" "$SKREW_URL"
"${EXTRACTOR[@]}" "$SKREW_ARC"
popd >/dev/null

mv "$SKREW_TMP" "$SKREW_DIR"
