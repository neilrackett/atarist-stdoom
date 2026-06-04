# STDOOM Coprocessor Microfirmware (in atarist-stdoom/sidecart) — progressive, simplest-first

## Context

STDOOM (Atari ST Doom port) on its `turbo` branch is CPU-bound on a stock 8 MHz
68000. A large fixed per-frame cost is **C2P (chunky-to-planar) conversion** —
turning the 320×200 8bpp chunky framebuffer into the ST's interleaved 4-plane
low-res format. We want to offload that (and, later, *anything in the render
pipeline*) to a **SidecarTridge Multi-device** RP2040 cartridge when present,
keeping the existing software path as a fallback.

This follows the proven workspace pattern: **md-sdl** firmware + **atarist-sdl**'s
`SDL_xbios_md.c` offload C2P to the sidecart; **md-js** is the cleanest plain-TOS
example of the detection/command transport (same `m68k-atari-mint-gcc` toolchain
STDOOM uses).

### Guiding principle (user): simplest thing that works, first
Get *something running* as early as possible, even if imperfect/slower, then
evolve. The decisions below deliberately take the leanest path and supersede the
earlier async/dynamic-dither choices:

- **Synchronous + a single planar slot.** No mailbox, no double-buffering. ST
  uploads chunky → triggers C2P → waits for the token → copies the one planar
  slot to screen. **Keeps md-js's token map at `$FAF000`/`$FAF004` untouched, so
  `sidecart_stubs.S` is copied verbatim with zero edits.**
- **Crude C2P first: 256→16 nearest colour, no dither.** DOOM's framebuffer
  bytes are full 0–255 palette indices, and the ST low-res shows only 16 colours,
  so a reduction is unavoidable. To keep the firmware trivial (no RGB-distance
  math on the RP), the **host builds a 256-byte `doom8_to_st4[256]` map at init**
  (the dominant entry of each `mix_weights_lorez[i]` row gives the nearest ST
  colour 0–15) and uploads it once. The firmware then just looks up
  `st4 = doom8_to_st4[chunky_byte]` and scatters its 4 bits into the ST 4-plane
  interleaved layout — no dither, no per-line phase. Output will look flat/banded
  and differ from the software renderer (which dithers); that is the accepted
  trade for the smallest first version. Evolve toward dither/fidelity later.
- **PING milestone first, C2P second.** Each milestone is independently runnable.
- **Scope: 1× low-res only.** Zoom/menu/automap/statusbar stay on the software
  fallback.
- **Single repo.** Everything new lives inside the existing `atarist-stdoom`
  repo — no separate `md-stdoom` project. The RP2040 firmware goes in a new
  `atarist-stdoom/sidecart/` subdirectory (copied & adapted from md-js); the
  host client files go in `linuxdoom-1.10/` with a `sidecart_` prefix.

Output will **not** be pixel-identical to STDOOM's software C2P yet — acceptable.

---

## Milestone 1 — implemented: PING detection + host fallback

Milestone 1 is complete. The code currently in the repo implements a synchronous
PING handshake plus a clean host-side fallback when the coprocessor is absent.

### 1A. Firmware implementation in `sidecart/`
- `sidecart/rp/src/stdoom_worker.c` publishes the ready magic at boot, seeds the
  token, answers `CMD_STDOOM_PING`, writes `STDOOM Coprocessor/1.0` into the
  shared result buffer, and echoes the token back to the ST.
- `sidecart/rp/src/stdoom_protocol.c` and
  `sidecart/rp/src/include/stdoom_protocol.h` provide the cartridge-bus protocol
  bridge.
- `sidecart/rp/src/include/stdoom_commands.h` defines the command IDs and shared
  memory offsets.
- `sidecart/target/atarist/src/main.s` is the cartridge loader stub.

### 1B. Shared memory map (ROM4 base `$FA0000`)
```
$FA0000  planar slot 0      32000 B   (reserved for Milestone 2)
$FAF000  random token        4 B       (echoed by the firmware)
$FAF004  random token seed   4 B
$FAF008  status word         2 B
$FAF00A  ready word          2 B       (worker-ready magic)
$FAF100  result buffer    <=2048 B     (PING version string)
```

### 1C. Host-side detection in `linuxdoom-1.10/`
- `sidecart_md_detect_verbose()` checks ready magic, reads the seed, then issues
  `CMD_STDOOM_PING`. It returns `1` on success and sets `stage` to `0/1/2/3` for
  success, ready mismatch, dead seed, or timeout.
- `sidecart_md_result()` copies the version string from `$FAF100`.
- `c2p_md_init()` in `atari_c2p.c` runs detection at startup and currently prints:
  - `MD ready=$XX(want $53) seed=$XXXXXXXX`
  - `MD stage=N ping=N`
  - `MD irq=%lX cmd=%lX ce=%lX lc=%lX`
  - `MD detected: STDOOM Coprocessor/1.0`
  - `MD not detected; SW C2P`

### 1D. Standalone test app
- `sidecart/test/sctest.c` builds to `sidecart/test/SCTEST.TOS` via `make sctest`.
- Use it to validate SidecarTridge protocol changes before wiring them into
  STDOOM.

### Milestone 1 exit
- With the sidecart firmware flashed, STDOOM reports
  `MD detected: STDOOM Coprocessor/1.0`.
- Without the firmware, STDOOM falls back to `MD not detected; SW C2P`.
- `make sctest` produces `sidecart/test/SCTEST.TOS`, which is the preferred
  place to exercise new SidecarTridge features before touching STDOOM proper.

---

## Milestone 2 — C2P offload (synchronous, single slot, simplest C2P)

### 2A. Firmware: add commands + a minimal Core 0 C2P
- New commands: `CMD_STDOOM_INIT 0x11` (d3=(w<<16|h)), `CMD_STDOOM_SET_MAP 0x12`
  (upload the 256-byte `doom8_to_st4` reduction map, once at init),
  `CMD_STDOOM_BLIT_ROWS 0x13` (chunky rows: d3=y, d4=(w<<16|rows),
  d5=(pitch<<16), buf=pixels — assemble the 64000 B chunky buffer in RP RAM),
  `CMD_STDOOM_C2P 0x14` (run C2P into slot 0, then echo token).
- **Synchronous, Core 0 is fine** for the first version (no FIFO/mailbox). On
  `C2P`, run the crude loop: for each pixel `st4 = doom8_to_st4[chunky_byte]`
  (0–15), scatter its 4 plane bits into the ST interleaved 16-pixel word group,
  write to slot 0 with the same big-endian word convention md-js/md-sdl use
  (`COPY_AND_CHANGE_ENDIANESS_BLOCK16` / equivalent) so the ST reads correct
  bytes. No dither, no per-line phase. Reference the *structure* of `sdl_c2p` in
  `/Users/neil/Projects/__OS/md-sdl/rp/src/emul.c` but keep it minimal.

### 2B. Host: reduction map + `c2p_screen_md` + planar copy
- Build `doom8_to_st4[256]` in `c2p_md_init` from `mix_weights_lorez` (index of
  the largest weight in each row = nearest ST colour). Send it once via
  `CMD_STDOOM_SET_MAP`. Rebuild/resend if the DOOM palette changes
  (`I_SetPalette`).
- `sidecart_md_init(320,200)` once (after detect, in `c2p_md_init`).
- New **`c2p_screen_md(out,in)`** in `atari_c2p.c`:
  - If the frame is zoom/menu/automap (derive exactly as `c2p_screen_lorez`,
    `atari_c2p.c:1865`), **fall through to `c2p_screen_lorez` (software)**.
  - Else: upload `in` (= `screens[0]`) in ~6-row chunks (≈1920 B, under the ~2096
    payload cap) via `send_sync_write`; send `CMD_STDOOM_C2P` and wait for the
    token (synchronous); then copy planar slot 0 → `out` (= `st_screen =
    Physbase()`).
  - **`sidecart_md_copy_planar_to_screen(PLANAR0, out)`** — port md-sdl's
    `md_copy_planar_to_screen` (`SDL_xbios_md.c`): STE blitter fast path when
    `_MCH` is STE/MegaSTE, else plain-ST CPU longword loop. This copy is what
    avoids the Shifter/68000 ROM4 bus contention (the Shifter then reads from
    screen RAM, not the cartridge).
- In `c2p_md_init` (Milestone 2): when `g_md_active`, set
  `c2p_screen_drawfunc = c2p_screen_md`. Leave `c2p_statusbar` software.

**Milestone 2 exit:** in a 1× low-res level the sidecart produces displayed
frames with correct geometry (flat/banded vs the software dither — fidelity
refined later).

---

## Verification
- **Build firmware:** `cd /Users/neil/Projects/__OS/atarist-stdoom/sidecart &&
  ./build.sh pico release <new-uuid>` → UF2 + JSON in `dist/`; `target/atarist`
  builds `BOOT.BIN` via its Makefile.
- **Build host:** `cd /Users/neil/Projects/__OS/atarist-stdoom/linuxdoom-1.10 &&
  make` → `atari/build/STDOOM.TOS` (+ `STDOOM20/2F.TOS`); confirm `sidecart_md.o`
  and `sidecart_stubs.o` link.
- **PING (M1):** run `make sctest` and exercise `sidecart/test/SCTEST.TOS` to
  validate detection and version reporting, then flash the UF2 and run STDOOM to
  confirm `MD detected: STDOOM Coprocessor/1.0`. Flash a different firmware to
  confirm software fallback (`MD not detected; SW C2P`).
- **C2P (M2):** in a 1× low-res level, confirm displayed frames have correct
  geometry/no garbage. Validate planar byte order by C2P-ing a known horizontal
  colour ramp and comparing slot bytes the ST reads against the expected
  interleaved layout. Toggle `g_md_active` off to A/B geometry against the
  software renderer.

## Critical files
- **Destination (single repo):** `atarist-stdoom/sidecart/` (firmware) +
  `atarist-stdoom/linuxdoom-1.10/` (host client).
- `/Users/neil/Projects/__OS/md-js/` — source to copy *from*;
  `rp/src/{mdjs_protocol.c, js_worker.c, emul.c}`,
  `target/atarist/src/{sidecart_stubs.S, mdjs.c, mdjs.h}`.
- `/Users/neil/Projects/__OS/md-sdl/rp/src/emul.c` — `sdl_c2p` structure +
  endianness helper to reference for the minimal C2P (and later dither).
- `/Users/neil/Projects/__OS/atarist-sdl/src/video/xbios/SDL_xbios_md.c` —
  `md_copy_planar_to_screen`, detection, chunk-upload reference.
- `/Users/neil/Projects/__OS/atarist-stdoom/linuxdoom-1.10/{i_video.c,
  atari_c2p.c, atari_c2p.h, Makefile}` — host integration points.

## Deferred (future phases, once the above works)
- md-sdl's dynamic palette reduction + Bayer dither (the user's flexibility goal).
- Async double-buffering (two slots + mailbox; would relocate the token block and
  edit `sidecart_stubs.S` constants).
- Offloading zoom (2×/4×) and the statusbar dirty-box path.
- Offloading further render-pipeline work (column/span drawing, palette FX).
