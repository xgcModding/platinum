#!/bin/bash
BUILDDIR=build_linux

cd $BUILDDIR
mkdir -p release_linux
cp pokeplatinum.AppImage release_linux/pokeplatinum.AppImage
