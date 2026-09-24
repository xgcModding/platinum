#!/bin/bash
set -e

echo Setting up MSys2 environment!

pacman -S --noconfirm make p7zip git python3 flex bison ninja mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-enet mingw-w64-ucrt-x86_64-arm-none-eabi-gcc zip mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-pkg-config
# Meson must run with native Python in the UCRT64 shell. libffi powers Lua hooks.
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-libffi
