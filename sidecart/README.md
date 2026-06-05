# STDOOM Accelerator

Microfirmware for the [SidecarTridge Multi-device](https://sidecartridge.com)
by [Neil Rackett](https://x.com/neilrackett)

## Introduction

At the heart of your SidecarTridge Multi-device is an RP2040 with the same raw
processing power as a 386DX or 486SX CPU.

The STDOOM Accelerator is designed to harness that power into a coprocessor that
can make DOOM truly playable on a stock Atari ST.

The ST host side lives in [`../linuxdoom-1.10`](../linuxdoom-1.10) (`sidecart_md.c/.h`, `sidecart_stubs.S`);
this directory is the RP2040 firmware.

## Status

- **Milestone 1 (done):** PING detection. The firmware publishes a ready magic,
  answers `CMD_STDOOM_PING` with a version string, and echoes the protocol
  token. STDOOM reports "accelerator detected" at startup.
- **Milestone 2 (done):** C2P (chunky-to-planar) offload. STDOOM uploads the
  chunky framebuffer, the RP2040 converts it to planar slot 0, and the ST blits
  it to screen. Gameplay frames render through the accelerator end-to-end on a
  Mega STE at 16 MHz + cache, with correct geometry. `sidecart/tests/C2PTEST.TOS`
  remains the preferred standalone hardware validation target.
- **Milestone 3 (next):** full C2P replacement. Route _all_ rendering (splash,
  menus, intermission, automap, status bar, gameplay) through the accelerator
  when present, with dirty-rect support (only changed status-bar cells / the
  active zoomed-view rectangle are updated). Refactor the sidecart path into a
  separate `sidecart_c2p.c` that overrides the software `atari_c2p.c`.
- **Milestone 4:** dynamic 16-colour palette (median cut / greyscale) and
  selectable dither modes (none, greyscale, 2×2/4×4 Bayer).

Longer term, this accelerator design is intended as the model for a new (clean)
Atari ST SDL XBIOS driver.

## Building

The firmware builds against the Raspberry Pi Pico SDK + Pico Extras, registered
as git submodules of the `atarist-stdoom` repo at `sidecart/pico-sdk` and
`sidecart/pico-extras`.

From the repo root:

```sh
git submodule update --init --recursive sidecart/pico-sdk sidecart/pico-extras
```

(`rp/build.sh` also runs this automatically and pins the SDK tags.)

You also need:

- the ARM cross-toolchain, CMake, and picotool on your PATH.
- [`atarist-toolkit-docker`](https://github.com/sidecartridge/atarist-toolkit-docker) to compile the ST host side.

## Build

In this folder, run:

```sh
make build        # release build
make build debug  # debug build and test apps
```

Flash the resulting `.uf2` to the SidecarT or copy both the `.uf2` (with the version
number removed) and `.json` to the `apps` folder on your SidecarT's SD card.

## Layout

```
rp/src/                RP2040 firmware
  stdoom_commands.h    command IDs + shared ROM-in-RAM memory map
  stdoom_protocol.c    cartridge-bus protocol DMA bridge
  stdoom_worker.c      command dispatch (PING/INIT/SET_MAP/BLIT_ROWS/C2P) + token handshake
  emul.c               bootstrap (ROM emulation + worker loop)
target/atarist/src/    m68k cartridge stub (main.s) → target_firmware.h
desc/app.json          SidecarT app manifest
```
