# STDOOM Accelerator — Doom render accelerator (SidecarTridge microfirmware)

STDOOM Accelerator offloads CPU-intensive STDOOM rendering to a
[SidecarTridge Multi-device](https://sidecartridge.com) RP2040 cartridge so Doom
runs faster on a stock Atari ST. The ST host side lives in
[`../linuxdoom-1.10`](../linuxdoom-1.10) (`sidecart_md.c/.h`, `sidecart_stubs.S`);
this directory is the RP2040 firmware.

It is derived from the [md-js](https://github.com/neilrackett/md-js)
microfirmware (transport + protocol bridge), with the JavaScript/network stack
removed.

## Status

- **Milestone 1 (done):** PING detection. The firmware publishes a ready magic,
  answers `CMD_STDOOM_PING` with a version string, and echoes the protocol
  token. STDOOM reports "accelerator detected" at startup.
- **Milestone 2 (done):** C2P (chunky-to-planar) offload. STDOOM uploads the
  chunky framebuffer, the RP2040 converts it to planar slot 0, and the ST blits
  it to screen. Gameplay frames render through the accelerator end-to-end on a
  Mega STE at 16 MHz + cache, with correct geometry. `sidecart/tests/C2PTEST.TOS`
  remains the preferred standalone hardware validation target.
- **Milestone 3 (next):** status bar offload.

## One-time setup: Pico SDK submodules

The firmware builds against the Raspberry Pi Pico SDK + Pico Extras, registered
as git submodules of the `atarist-stdoom` repo at `sidecart/pico-sdk` and
`sidecart/pico-extras`. From the repo root:

```sh
git submodule update --init --recursive sidecart/pico-sdk sidecart/pico-extras
```

(`rp/build.sh` also runs this automatically and pins the SDK tags.)

You also need the ARM cross-toolchain, CMake, and picotool on your PATH, plus
`vasm`/`vlink` and `stcmd` for the m68k cartridge stub.

## Build

```sh
# From this directory. Builds the m68k cartridge stub + the RP2040 firmware,
# producing dist/<uuid>-<version>.uf2 and dist/<uuid>.json.
./build.sh pico release "$(cat uuid.txt)"
# or simply:
make build
```

Flash the resulting `.uf2` to the SidecarTridge.

## Layout

```
rp/src/                RP2040 firmware
  stdoom_commands.h    command IDs + shared ROM-in-RAM memory map
  stdoom_protocol.c    cartridge-bus protocol DMA bridge
  stdoom_worker.c      command dispatch (PING/INIT/SET_MAP/BLIT_ROWS/C2P) + token handshake
  emul.c               bootstrap (ROM emulation + worker loop)
target/atarist/src/    m68k cartridge stub (main.s) → target_firmware.h
desc/app.json          SidecarTridge app manifest
```
