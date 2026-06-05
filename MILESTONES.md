# STDOOM Accelerator Microfirmware (in atarist-stdoom/sidecart) — progressive, simplest-first

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
PING handshake plus a clean host-side fallback when the accelerator is absent.

### 1A. Firmware implementation in `sidecart/`
- `sidecart/rp/src/stdoom_worker.c` publishes the ready magic at boot, seeds the
  token, answers `CMD_STDOOM_PING`, writes `STDOOM Accelerator/1.0` into the
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
  - `MD detected: STDOOM Accelerator/1.0`
  - `MD not detected; SW C2P`

### 1D. Standalone test app
- `sidecart/tests/pingtest.c` builds to `sidecart/tests/PINGTEST.TOS` via `make -C sidecart pingtest`.
- Use it to validate SidecarTridge protocol changes before wiring them into
  STDOOM.

### Milestone 1 exit
- With the sidecart firmware flashed, STDOOM reports
  `MD detected: STDOOM Accelerator/1.0`.
- Without the firmware, STDOOM falls back to `MD not detected; SW C2P`.
- `make -C sidecart pingtest` produces `sidecart/tests/PINGTEST.TOS`, which is the preferred
  place to exercise new SidecarTridge features before touching STDOOM proper.

---

## Milestone 2 — C2P offload (synchronous, single slot, simplest C2P)

### Current status: complete

C2P offload works end-to-end. STDOOM proper renders gameplay frames through the
accelerator on a Mega STE at **16 MHz + cache**, with correct geometry.

- The RP2040 firmware accepts `INIT`, `SET_MAP`, `BLIT_ROWS`, and `C2P`.
- The ST host uploads the full 320x200 chunky frame in 6-row chunks, triggers
  C2P, then copies planar slot 0 back to the ST display.
- `sidecart/tests/C2PTEST.TOS` is the canonical standalone validation app. It
  prints timings for detect/init/map/upload/C2P/copy plus a planar checksum.
- STE blitter copy is used for the planar-to-screen copy on STE/Mega STE, CPU
  longword fallback on plain ST.
- Mega STE cache is temporarily disabled around SidecarTridge protocol traffic
  and restored afterwards, because the protocol depends on uncached ROM3 reads.

#### Hard-won fixes (do not regress)

- **Mega STE cache register `$FFFF8E21`** uses canonical MSTE_CC values
  `$FF`/`$FE`/`$F4` (cache = bit0), not the naive `$01`/`$03` that corrupts the
  must-be-set bits and destabilises the machine.
- **STE blitter registers start at `$FFFF8A20`** ($8A00 = 16 words of halftone
  RAM). The old `$8A02..$8A1E` offsets wrote into halftone RAM, so the blit
  never ran (black screen).
- **`Super()` is mode-aware** in `sidecart_md.c`: STDOOM is always in supervisor
  mode after `I_Init`, and the naive `Super(0L)/Super(ssp)` pattern crashes
  (2 bombs) when already supervisor.
- **No planar byte-swap on the RP2040**: `stdoom_pack_to_planar` `memcpy`s plane
  words to ROM-in-RAM (the ST reads uint16 values directly). Byte-swapping
  scrambled the 8-pixel halves of every 16-pixel group.
- **C2PTEST's synthetic pattern hides pixel-order bugs** — validate C2P pixel
  order against real Doom output, not the smooth gradient.

### 2A. Firmware: add commands + a minimal Core 0 C2P
- New commands: `CMD_STDOOM_INIT 0x11` (d3=(w<<16|h)), `CMD_STDOOM_SET_MAP 0x12`
  (upload the 256-byte `doom8_to_st4` reduction map, once at init),
  `CMD_STDOOM_BLIT_ROWS 0x13` (chunky rows: d3=y, d4=(w<<16|rows),
  d5=(pitch<<16), buf=pixels — assemble the 64000 B chunky buffer in RP RAM),
  `CMD_STDOOM_C2P 0x14` (run C2P into slot 0, then echo token).
- **Synchronous, Core 0 is fine** for the first version (no FIFO/mailbox). On
  `C2P`, run the crude loop: for each pixel `st4 = doom8_to_st4[chunky_byte]`
  (0–15), scatter its 4 plane bits into the ST interleaved 16-pixel word group
  (px0 → bit15), then `memcpy` the plane words to slot 0. **Do NOT byte-swap**
  the plane words (the original `COPY_AND_CHANGE_ENDIANESS_BLOCK16` was wrong):
  the ST reads ROM-in-RAM uint16 values directly, so the values the RP computes
  are already what the shifter needs. No dither, no per-line phase.

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

### Implemented so far

- `sidecart/tests/C2PTEST.TOS` now covers the full visible round trip and is the
  preferred place to verify new changes before touching STDOOM proper.
- `sidecart/tests/PINGTEST.TOS` remains the preferred transport/detection smoke
  test.
- The current `C2PTEST.TOS` image and checksum output are treated as the
  expected Milestone 2 test oracle.

### Milestone 2 exit criteria — met

- ✅ In a 1x low-res level, STDOOM proper produces displayed frames through the
  sidecart path with correct geometry and without crashing.
- ✅ The software fallback (`c2p_screen_lorez`) is only used when intentionally
  falling back (non-gameplay frames or a failed sidecart call), not because the
  accelerator path failed silently.
- ✅ Mega STE cache/turbo interaction is stable at 16 MHz + cache during play.

---

## Milestone 3 — full C2P replacement (all screens via the accelerator)

### Goal

When the accelerator is present, route **all** graphics/C2P through it, not just
1× low-res gameplay: the splash screen, menus, intermission/finale, automap, and
status bar. Effectively, the sidecart C2P *replaces* the software C2P entirely
for the duration of the session (with software remaining the fallback only when
no accelerator is detected). Note the gameplay status bar is already accelerated
in M2 (the full 320×200 `screens[0]`, including the bottom 32 rows, goes through
`c2p_screen_md`); M3 is about the non-gameplay screens and a clean architecture.

### Dirty-rect rendering (required)

M3 must support **dirty-rectangle** C2P, not just full-frame:

- **Status bar:** only the cells that actually change (ammo/health digits, face,
  arms, keys) are C2P'd and copied, mirroring the software `c2p_statusbar`
  dirty-box path `(y_begin, y_end, x_begin, x_end)`.
- **Zoomed/small view:** when the playfield is scaled to a small window, only the
  active view rectangle is uploaded/converted/copied, not the full 320×200.

This means the sidecart path needs **arbitrary `(x, y, w, h)` region** support
end-to-end:

- ST uploads only the dirty rows/columns for that region.
- The RP2040 C2P converts just that region into the correct planar slot offset
  (16-pixel x-alignment for plane words; clamp/extend x to word boundaries as the
  software path does — see `c2p_statusbar`, `atari_c2p.c`).
- The planar→screen copy blits only that region to the matching screen offset
  (the blitter's `X_COUNT`/`Y_COUNT`/`DST_*` line stepping handles a sub-rect;
  the M2 copy used a single linear `X_COUNT=16000, Y_COUNT=1` full-frame blit, so
  a rect copy needs proper per-line `Y_COUNT` + destination/source line
  increments).
- Keep full-frame as the path for screens that change wholesale (splash, menu,
  scene changes).

### Planned architecture: `atari_c2p.c` (software) + `sidecart_c2p.c` (accelerated)

- **Revert `atari_c2p.c` to its original, software-only form** (the pre-sidecart
  version in git history): software drawfuncs and the
  `c2p_screen_drawfunc`/`c2p_statusbar_drawfunc`/`set_doom_palette`/… function
  pointers, with no `c2p_md_*` code interleaved.
- **New `sidecart_c2p.c`** holds everything sidecart-specific currently living in
  `atari_c2p.c` (reduction map build, `c2p_screen_md`, the blitter planar→screen
  copy, the md palette hook). When the accelerator is detected, a single
  `sidecart_c2p_install()` overrides the software function pointers with the
  accelerated versions.
- Expose the function pointers and the software drawfuncs/palette helpers via
  `atari_c2p.h` so `sidecart_c2p.c` can install over them and fall back to them.
- This mirrors how a clean SDL XBIOS driver would swap implementations, which is
  the longer-term motivation (see "Why" below).

### Open questions for M3

- **Region command shape.** Define the dirty-rect protocol: either extend
  `BLIT_ROWS`/`C2P` with `(x, w)` (they already carry `y`, `rows`, `width`,
  `pitch`), or add explicit region commands. Keep x word-aligned (×16) for plane
  packing.
- **Per-screen routing.** Decide which screens use full-frame vs dirty-rect
  (gameplay status bar + zoomed view = dirty-rect; splash/menu/scene change =
  full-frame). The software path's existing dirty-box logic (`c2p_statusbar`,
  zoom view dimensions) is the reference for the rects.
- **Cadence.** Splash/menu frames are infrequent and dirty-rects are small, so
  the synchronous single-slot model is almost certainly still fine (no
  double-buffering needed yet).

### M3 exit criteria

- With the accelerator present, every STDOOM screen from the splash onwards is
  rendered through the sidecart C2P with correct geometry.
- Dirty-rect updates work: the status bar updates only changed cells, and a
  small/zoomed view uploads/converts/copies only the active view rectangle.
- With no accelerator, behaviour is identical to the reverted software
  `atari_c2p.c` (clean fallback, no regressions).
- `atari_c2p.c` contains no sidecart-specific code; all of it lives in
  `sidecart_c2p.c`.

---

## Milestone 4 — dynamic palette + dither modes

### Goal

Replace the crude fixed nearest-colour reduction with a **dynamic 16-colour
palette** and **selectable dither modes**, primarily as a testbed for image
quality.

- **Dynamic palette generation:** build the 16-colour ST palette from the active
  DOOM palette per frame/scene — e.g. median cut for colour, or a luminance ramp
  for greyscale — instead of mapping to a fixed ST palette.
- **Dither / colour modes (for testing):**
  - none (flat nearest-colour, as M2)
  - greyscale
  - 2×2 Bayer dithering
  - 4×4 Bayer dithering
- **Mode selection:** runtime switching is the goal; compile-time selection is an
  acceptable first step.

### Notes

- The RP2040 already receives a 256→16 reduction map (`SET_MAP`); M4 generalises
  how that map (and the ST palette) is computed, and adds the dither stage in the
  RP2040 C2P (`stdoom_pack_to_planar`).
- `atari_c2p.c`'s existing `mix_weights_*` / Bayer tables are a useful reference
  for the dither maths.

### M4 exit criteria

- The ST palette is generated dynamically (colour and greyscale) rather than
  fixed.
- All four dither/colour modes are selectable (runtime or compile-time) and
  visibly distinct.

---

## Why this approach (longer-term motivation)

If the STDOOM accelerator path works well, it becomes the **model for a new Atari
ST SDL XBIOS driver**. The current `atarist-sdl` `SDL_xbios_md.c` driver is
experimental, unstable, and not really usable; a clean, proven C2P-replacement +
palette/dither design here is intended to be ported into a solid SDL driver. This
is also why M3 favours a clean `atari_c2p.c` (software) / `sidecart_c2p.c`
(accelerated) split — it maps directly onto an SDL driver swapping
implementations.

---

## Verification
- **Build firmware:** `cd /Users/neil/Projects/__OS/atarist-stdoom/sidecart &&
  ./build.sh pico release <new-uuid>` → UF2 + JSON in `dist/`; `target/atarist`
  builds `BOOT.BIN` via its Makefile.
- **Build host:** `cd /Users/neil/Projects/__OS/atarist-stdoom/linuxdoom-1.10 &&
  make` → `atari/build/STDOOM.TOS` (+ `STDOOM20/2F.TOS`); confirm `sidecart_md.o`
  and `sidecart_stubs.o` link.
- **PING (M1):** run `make -C sidecart pingtest` and exercise `sidecart/tests/PINGTEST.TOS` to
  validate detection and version reporting, then flash the UF2 and run STDOOM to
  confirm `MD detected: STDOOM Accelerator/1.0`. Flash a different firmware to
  confirm software fallback (`MD not detected; SW C2P`).
- **C2P (M2):** in a 1× low-res level, confirm displayed frames have correct
  geometry/no garbage. Validate planar byte order by C2P-ing a known horizontal
  colour ramp and comparing slot bytes the ST reads against the expected
  interleaved layout. Toggle `g_md_active` off to A/B geometry against the
  software renderer.

### Status note

Milestones 1 and 2 are complete and confirmed on hardware (Mega STE, 16 MHz +
cache). Next up is Milestone 3 (status bar offload).

## Critical files
- **Destination (single repo):** `atarist-stdoom/sidecart/` (firmware) +
  `atarist-stdoom/linuxdoom-1.10/` (host client).
- `/Users/neil/Projects/__OS/md-js/` — source to copy *from*;
  `rp/src/{mdjs_protocol.c, js_worker.c, emul.c}`,
  `target/atarist/src/{sidecart_stubs.S, mdjs.c, mdjs.h}`.
- `/Users/neil/Projects/__OS/md-sprites-demo/target/atarist/src/main.s` — the
  validated reference for STE blitter copy-to-screen (correct `$FFFF8A20`
  register offsets). Prefer this for copy/blitter questions.
- `/Users/neil/Projects/__OS/md-sdl/` — EXPERIMENTAL, not a source of truth.
  `rp/src/emul.c` (`sdl_c2p`) and `src/video/xbios/SDL_xbios_md.c` are useful for
  *structure/ideas* (and the documented Mega STE `$FF`/`$FE`/`$F4` register
  values in `SDL_megaste.c`), but verify anything before relying on it.
- `/Users/neil/Projects/__OS/atarist-stdoom/linuxdoom-1.10/{i_video.c,
  atari_c2p.c, atari_c2p.h, Makefile}` — host integration points.

## Deferred (future phases, beyond M3/M4)
- Async double-buffering (two slots + mailbox; would relocate the token block and
  edit `sidecart_stubs.S` constants).
- Offloading further render-pipeline work (column/span drawing, palette FX).

(Dynamic palette + Bayer dither is now **Milestone 4**; full-pipeline / all-screen
offload is now **Milestone 3**.)
