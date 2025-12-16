#!/bin/bash
# Build script for STDOOM using atarist-toolkit-docket

clear

cd linuxdoom-1.10
stcmd make clean
stcmd make
cd ..

echo
echo "🥳 Build complete: Executables are in linuxdoom-1.10/atari"
