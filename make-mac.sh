#!/bin/bash
# Build script for STDOOM on macOS (and maybe Linux)

export CC=m68k-atari-mint-gcc

clear

cd linuxdoom-1.10
make clean
make
cd ..

echo
echo "🥳 Build complete: Executables are in linuxdoom-1.10/atari"
