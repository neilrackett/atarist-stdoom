# STDOOM Coprocessor — project context for Claude

## What this is

STDOOM is a port of Linux Doom to the Atari ST. This repo adds a **SidecarTridge
Multi-device coprocessor** path: when a SidecarTridge is present, the RP2040
handles the CPU-intensive chunky-to-planar (C2P) conversion, freeing the ST's
68000 for game logic.

Build targets:

- **`linuxdoom-1.10/`** — the ST-side `.TOS` binary (`make stdoom`, via `stcmd`)
- **`sidecart/rp/`** — the RP2040 firmware (`.uf2`, built with `bash sidecart/rp/build.sh pico_w debug`)
- **`sidecart/test/`** — standalone SidecarTridge detection test app (`make sctest`); use this first when validating new SidecarTridge features before wiring them into STDOOM

## Milestone plan

| #   | What                                                                                                                                 | Status                      |
| --- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------- |
| 1   | **Detection** — ST PINGs the SidecarTridge; firmware echoes token + version string; ST reports detected/not-detected at startup      | **Complete**                |
| 2   | **C2P offload** — ST uploads chunky framebuffer; RP2040 converts to planar and writes to shared ROM-in-RAM slot; ST blits from there | Not started                 |
| 3   | **Status bar offload**                                                                                                               | Not started                 |

@MILESTONES.md

## Milestone 1 — complete

The detection protocol (`sidecart_md_detect` in `linuxdoom-1.10/sidecart_md.c`) is complete:

1. Checks ready magic at `$FAF00A` (expect `$53`)
2. Checks the seed at `$FAF004` to guard against a dead cartridge
3. Sends `CMD_STDOOM_PING` (op `$0010`, d3=`'STDM'`) via `stdoom_send_sync_command`
4. Waits for the RP2040 to echo the random token back to `$FAF000`

On success the firmware writes `STDOOM Coprocessor/1.0` into the shared result
buffer and `sidecart_md_result()` returns it to the ST side.

Use `make sctest` and `sidecart/test/SCTEST.TOS` to validate new SidecarTridge
features in isolation before wiring them into `linuxdoom-1.10/`.

### Key files

| File                                        | Purpose                                           |
| ------------------------------------------- | ------------------------------------------------- |
| `linuxdoom-1.10/sidecart_md.c/.h`           | ST-side detect + PING client                      |
| `linuxdoom-1.10/sidecart_stubs.S`           | Low-level SidecarTridge bus protocol (asm)        |
| `linuxdoom-1.10/atari_c2p.c`                | `c2p_md_init()` — calls detect, prints diagnostic |
| `sidecart/rp/src/stdoom_worker.c`           | RP2040 worker: init, PING dispatch, token echo    |
| `sidecart/rp/src/include/stdoom_commands.h` | Shared memory map, command IDs, offsets           |
| `sidecart/test/sctest.c`                    | Standalone detection test app (`make sctest`)     |

### Diagnostic output (startup, before game loads)

```
MD ready=$XX(want $53) seed=$XXXXXXXX
MD stage=N ping=N
MD irq=%lX cmd=%lX ce=%lX lc=%lX
MD detected: STDOOM Coprocessor/1.0
MD not detected; SW C2P
```

- `stage=0, ping=0` → detected ✓
- `stage=1` → ready magic wrong (firmware not running)
- `stage=2` → seed looks dead (bus write not landing)
- `stage=3, ping=-1` → PING timed out (token echo failed)

## Build commands

```bash
# Standalone test app
make sctest   # builds sidecart/test/SCTEST.TOS

# ST side
STCMD_NO_TTY=1 stcmd make -C linuxdoom-1.10

# RP2040 (from repo root, SDK submodules already initialised)
touch sidecart/rp/src/stdoom_worker.c   # force rebuild if needed
make -j4 -C sidecart/rp/build
cp sidecart/rp/build/rp.uf2 sidecart/rp/dist/rp-pico_w-debug.uf2
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
