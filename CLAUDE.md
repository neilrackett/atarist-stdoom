# STDOOM Accelerator — project context for Claude

## What this is

STDOOM is a port of Linux Doom to the Atari ST. This repo adds a **SidecarTridge
Multi-device accelerator** path: when a SidecarTridge is present, the RP2040
handles the CPU-intensive chunky-to-planar (C2P) conversion, freeing the ST's
68000 for game logic.

Build targets:

- **`linuxdoom-1.10/`** — the ST-side `.TOS` binary (`make stdoom`, via `stcmd`)
- **`sidecart/rp/`** — the RP2040 firmware (`.uf2`, built with `bash sidecart/rp/build.sh pico_w debug`)
- **`sidecart/tests/`** — standalone SidecarTridge test apps:
  - `make -C sidecart tests` to build both test apps at once
  - `make -C sidecart pingtest` for detection + transport smoke tests
  - `make -C sidecart c2ptest` for the full 320x200 C2P round trip and screen copy
  Use these first when validating new SidecarTridge features before wiring them into STDOOM

@sidecart/README.md

## Milestone plan

| #   | What                                                                                                                                 | Status                      |
| --- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------- |
| 1   | **Detection** — ST PINGs the SidecarTridge; firmware echoes token + version string; ST reports detected/not-detected at startup      | **Complete**                |
| 2   | **C2P offload** — ST uploads chunky framebuffer; RP2040 converts to planar and writes to shared ROM-in-RAM slot; ST blits from there | **Complete**                |
| 3   | **Status bar offload**                                                                                                               | Not started                 |

@MILESTONES.md

## Milestone 1 — complete

The detection protocol (`sidecart_md_detect` in `linuxdoom-1.10/sidecart_md.c`) is complete:

1. Checks ready magic at `$FAF00A` (expect `$53`)
2. Checks the seed at `$FAF004` to guard against a dead cartridge
3. Sends `CMD_STDOOM_PING` (op `$0010`, d3=`'STDM'`) via `stdoom_send_sync_command`
4. Waits for the RP2040 to echo the random token back to `$FAF000`

On success the firmware writes `STDOOM Accelerator/1.0` into the shared result
buffer and `sidecart_md_result()` returns it to the ST side.

Use `make -C sidecart pingtest` and `sidecart/tests/PINGTEST.TOS` to validate detection and
transport in isolation, and `make -C sidecart c2ptest` / `sidecart/tests/C2PTEST.TOS` for
the full visible round trip, before wiring new SidecarTridge features into
`linuxdoom-1.10/`.

## Milestone 2 — complete

C2P offload works end-to-end. STDOOM proper renders gameplay frames through the
accelerator on a Mega STE at **16 MHz + cache**, with correct geometry.

- `sidecart/tests/C2PTEST.TOS` runs the full synchronous round trip: detect,
  `INIT`, `SET_MAP`, `BLIT_ROWS`, `C2P`, then planar copy to the ST screen. It
  prints per-phase timings and a planar checksum, and is the preferred benchmark
  and regression tool.
- STDOOM's `c2p_screen_md` (in `atari_c2p.c`) uploads `screens[0]` in 6-row
  chunks, triggers `C2P`, and copies planar slot 0 to the screen, with a clean
  fall-through to software `c2p_screen_lorez` on any failure or non-gameplay
  frame.
- STE blitter copy is used for the planar-to-screen copy on STE/Mega STE, with a
  CPU longword fallback on plain ST.

### Hard-won fixes that made it work (do not regress)

- **Mega STE cache register (`$FFFF8E21`)** uses canonical MSTE_CC values
  `$FF`/`$FE`/`$F4` (cache = bit0). Naive `$01`/`$03` writes corrupt the
  must-be-set upper bits and destabilise the machine. Kept in sync between
  `atari_checkcpu.c` and `sidecart_md.c`'s cache guard.
- **STE blitter registers start at `$FFFF8A20`** (the first 16 words at
  `$8A00` are halftone RAM). The old `$8A02..$8A1E` offsets wrote into halftone
  RAM, so the blitter never ran and the screen stayed black. Fixed in
  `atari_c2p.c` and `c2ptest.c`.
- **`Super()` must be mode-aware.** STDOOM runs the whole game loop in
  supervisor mode (`I_Init` enters super and stays), so the cache guard uses
  SUP_INQUIRE and only toggles mode when entered from user mode. The naive
  `Super(0L)/Super(ssp)` pattern crashes (2 bombs) when already supervisor.
- **The RP2040 must NOT byte-swap the planar output.** `stdoom_pack_to_planar`
  `memcpy`s the plane words to ROM-in-RAM; the ST reads uint16 values directly.
  Byte-swapping exchanges the two 8-pixel halves of every 16-pixel group and
  scrambles columns.
- **C2PTEST's synthetic pattern hides pixel-order bugs** (adjacent pixels map to
  the same ST colour). Validate C2P pixel ORDER against real Doom output or a
  per-pixel-varying pattern, not the smooth gradient.

### Cache guard note

Mega STE cache is temporarily disabled around SidecarTridge command traffic in
`sidecart_md.c`, then restored afterwards, because cache can hide the ROM3 bus
reads the protocol relies on.

### Key files

| File                                        | Purpose                                           |
| ------------------------------------------- | ------------------------------------------------- |
| `linuxdoom-1.10/sidecart_md.c/.h`           | ST-side client: detect/PING + INIT/SET_MAP/BLIT_ROWS/C2P, mode-aware cache guard |
| `linuxdoom-1.10/sidecart_stubs.S`           | Low-level SidecarTridge bus protocol (asm)        |
| `linuxdoom-1.10/atari_c2p.c`                | `c2p_md_init()`, `c2p_screen_md()`, blitter planar→screen copy |
| `linuxdoom-1.10/atari_checkcpu.c`           | Mega STE turbo/cache register (canonical `$FF`/`$FE`/`$F4`) |
| `sidecart/rp/src/stdoom_worker.c`           | RP2040 worker: PING/INIT/SET_MAP/BLIT_ROWS/C2P dispatch, pack-to-planar, token echo |
| `sidecart/rp/src/include/stdoom_commands.h` | Shared memory map, command IDs, offsets           |
| `sidecart/tests/pingtest.c`                 | Standalone detection + transport smoke test       |
| `sidecart/tests/c2ptest.c`                  | Standalone full C2P round-trip test (`make -C sidecart c2ptest`) |

### Diagnostic output (startup, before game loads)

```
MD ready=$XX(want $53) seed=$XXXXXXXX
MD stage=N ping=N
MD irq=%lX cmd=%lX ce=%lX lc=%lX
MD detected: STDOOM Accelerator/1.0
MD not detected; SW C2P
```

- `stage=0, ping=0` → detected ✓
- `stage=1` → ready magic wrong (firmware not running)
- `stage=2` → seed looks dead (bus write not landing)
- `stage=3, ping=-1` → PING timed out (token echo failed)

## Build commands

```bash
# All commands run from repo root

# Standalone test app
make -C sidecart tests    # builds both test apps
make -C sidecart pingtest   # builds sidecart/tests/PINGTEST.TOS
make -C sidecart c2ptest   # builds sidecart/tests/C2PTEST.TOS

# ST side
make stdoom           # builds linuxdoom-1.10/dist/build/STDOOM.TOS

# RP2040 microfirmware for SidecarTridge Multi-device
make sidecart debug   # builds both test apps plus sidecart/dist/[UUID]-[VERSION].uf2
```

## Shared memory map (ROM4 base `$FA0000`)

| Offset  | ST address | Size    | Purpose                                         |
| ------- | ---------- | ------- | ----------------------------------------------- |
| `$0000` | `$FA0000`  | 32000 B | Planar slot 0 (Milestone 2)                     |
| `$F000` | `$FAF000`  | 4 B     | Random token (RP2040 writes to unblock ST)      |
| `$F004` | `$FAF004`  | 4 B     | Random token seed                               |
| `$F008` | `$FAF008`  | 2 B     | Status (`$00`=idle, `$01`=busy, `$02`=done)     |
| `$F00A` | `$FAF00A`  | 2 B     | Ready word (both bytes = `$53` when worker up)  |
| `$F100` | `$FAF100`  | 2048 B  | Result buffer (PING writes version string here) |
