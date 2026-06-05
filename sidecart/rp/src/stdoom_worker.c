/**
 * File: stdoom_worker.c
 * Description: STDOOM Accelerator worker (Core 0).
 *
 * Milestone 2: synchronous, single-slot C2P. The worker publishes a ready
 * magic at boot, seeds the random token, answers CMD_STDOOM_PING by writing a
 * version string into the result buffer and echoing the random token, accepts
 * a 256-byte chunky-to-ST4 map, assembles chunky rows into a single 320x200
 * staging buffer, and converts that buffer into planar slot 0 on CMD_C2P.
 */

#include "stdoom_commands.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "debug.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "stdoom_protocol.h"

/* ── Linker symbol for ROM-in-RAM base (defined in memmap_rp.ld) ─────────── */
extern unsigned int __rom_in_ram_start__;

/* Version string returned by PING. */
#define STDOOM_VERSION_STRING "STDOOM Accelerator/1.0"

/* Cached addresses (set in stdoom_worker_init, read-only thereafter). */
static uint32_t s_rom_base;
static uint32_t s_token_addr;
static uint32_t s_token_seed_addr;
static volatile char *s_result_mem;
static volatile uint16_t *s_status_mem;
static volatile uint16_t *s_ready_mem;
static volatile uint16_t *s_planar_mem;

/* Milestone 2 render state. */
static uint16_t s_frame_width = STDOOM_FRAME_WIDTH;
static uint16_t s_frame_height = STDOOM_FRAME_HEIGHT;
static uint16_t s_chunky_pitch = STDOOM_FRAME_WIDTH;
static uint8_t s_doom8_to_st4[256];
static uint8_t s_chunky_frame[STDOOM_CHUNKY_SIZE];
static uint16_t s_planar_words[STDOOM_PLANAR_WORDS];

/* Drain spurious commands the PIO/parser may latch during cold start. */
static volatile bool s_dispatch_armed = false;
static TransmissionProtocol s_loop_proto;

/* Sparse UART diagnostics so we can prove which ST-side path is active without
 * flooding the console every frame. */
static uint32_t s_diag_ping_count;
static uint32_t s_diag_init_count;
static uint32_t s_diag_set_map_count;
static uint32_t s_diag_blit_rows_count;
static uint32_t s_diag_c2p_count;

#define STDOOM_BUS_BYTE_WORD(value) ((uint16_t)((uint16_t)(value) << 8))

/* Ready flag: write the magic to BOTH bytes of the bus word so the ST sees the
 * same byte regardless of which half of the 16-bit word is at the even
 * address. */
#define STDOOM_READY_WORD \
  ((uint16_t)((STDOOM_READY_MAGIC << 8) | STDOOM_READY_MAGIC))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Copy a byte buffer while swapping adjacent bytes.
 * This avoids any halfword-alignment requirement on the RP2040. */
static void stdoom_copy_swap_bytes(uint8_t *dst, const uint8_t *src,
                                   uint32_t byte_count) {
  uint32_t i = 0;

  for (; i + 1u < byte_count; i += 2u) {
    dst[i] = src[i + 1u];
    dst[i + 1u] = src[i];
  }
  if (i < byte_count) {
    dst[i] = src[i];
  }
}

/* Rolling non-zero seed source. rand() must NOT be used: on the RP2040 there
 * is no RTC, so time()==0 -> srand(0) -> newlib rand() returns 0 on its first
 * call, producing a zero token/seed that breaks the ST handshake (the ST waits
 * for seed_addr != token, which can never be true when both are 0). A simple
 * LCG seeded non-zero guarantees a changing, always-non-zero value. */
static uint32_t s_seed_state = 0xABCD1234u;

static uint32_t stdoom_next_seed(void) {
  s_seed_state = s_seed_state * 1103515245u + 12345u;
  return s_seed_state | 1u;  /* force non-zero */
}

/* Write the random token back to shared memory to unblock the ST and seed the
 * next token value. */
static void stdoom_send_response(uint32_t random_token) {
  TPROTO_SET_RANDOM_TOKEN(s_token_addr, random_token);
  TPROTO_SET_RANDOM_TOKEN(s_token_seed_addr, stdoom_next_seed());
}

/* Copy a NUL-terminated C string into the result buffer with the m68k
 * big-endian byte order the ST expects (the parser/ST read pairs of bytes from
 * each 16-bit bus word high-byte-first). */
static void stdoom_write_result(const char *str) {
  char tmp[STDOOM_RESULT_MAX_SIZE];
  size_t len = strlen(str);
  if (len >= sizeof(tmp)) {
    len = sizeof(tmp) - 1;
  }
  memset(tmp, 0, sizeof(tmp));
  memcpy(tmp, str, len);
  stdoom_copy_swap_bytes((uint8_t *)s_result_mem, (const uint8_t *)tmp,
                         (uint32_t)((len + 2u) & ~(size_t)1u));
}

static void stdoom_reset_map(void) {
  for (unsigned int i = 0; i < 256u; i++) {
    s_doom8_to_st4[i] = (uint8_t)(i & 0x0Fu);
  }
}

static void stdoom_reset_buffers(void) {
  memset(s_chunky_frame, 0, sizeof(s_chunky_frame));
  memset(s_planar_words, 0, sizeof(s_planar_words));
}

static void stdoom_pack_to_planar(void) {
  uint16_t blocks_per_row = (uint16_t)(s_frame_width / 16u);
  uint16_t words_per_row = (uint16_t)(s_frame_width / 4u);
  uint16_t rows = s_frame_height;

  memset(s_planar_words, 0, sizeof(s_planar_words));

  for (uint16_t y = 0; y < rows; y++) {
    const uint8_t *row = &s_chunky_frame[(uint32_t)y * s_chunky_pitch];
    uint16_t *dst_row = &s_planar_words[(uint32_t)y * words_per_row];

    for (uint16_t blk = 0; blk < blocks_per_row; blk++) {
      uint16_t plane0 = 0;
      uint16_t plane1 = 0;
      uint16_t plane2 = 0;
      uint16_t plane3 = 0;
      const uint8_t *src = row + (blk * 16u);

      for (uint16_t px = 0; px < 16u; px++) {
        uint8_t mapped = s_doom8_to_st4[src[px]] & 0x0Fu;
        uint16_t bit = (uint16_t)(1u << (15u - px));
        if (mapped & 0x1u) plane0 |= bit;
        if (mapped & 0x2u) plane1 |= bit;
        if (mapped & 0x4u) plane2 |= bit;
        if (mapped & 0x8u) plane3 |= bit;
      }

      dst_row[(blk * 4u) + 0u] = plane0;
      dst_row[(blk * 4u) + 1u] = plane1;
      dst_row[(blk * 4u) + 2u] = plane2;
      dst_row[(blk * 4u) + 3u] = plane3;
    }
  }

  /* Copy the packed plane words straight through WITHOUT a byte swap. The plane
   * words are already the exact 16-bit values the ST shifter needs (px0 at
   * bit15), and the ST CPU reads ROM-in-RAM uint16 values directly (same as the
   * result-string path). Byte-swapping here would exchange the two 8-pixel
   * halves of every 16-pixel group, scrambling columns. */
  memcpy((void *)s_planar_mem, s_planar_words, STDOOM_PLANAR_SIZE);
}

static bool stdoom_diag_should_log(uint32_t count) {
  return count < 4u || (count % 120u) == 0u;
}

static void stdoom_init_frame_state(uint16_t width, uint16_t height) {
  if (width == 0) {
    width = STDOOM_FRAME_WIDTH;
  }
  if (height == 0) {
    height = STDOOM_FRAME_HEIGHT;
  }
  if (width > STDOOM_FRAME_WIDTH) {
    width = STDOOM_FRAME_WIDTH;
  }
  if (height > STDOOM_FRAME_HEIGHT) {
    height = STDOOM_FRAME_HEIGHT;
  }

  s_frame_width = width;
  s_frame_height = height;
  s_chunky_pitch = width;
  stdoom_reset_map();
  stdoom_reset_buffers();
}

static void stdoom_store_chunky_rows(uint16_t y, uint16_t rows,
                                     uint16_t width, uint16_t pitch,
                                     const uint8_t *buf, uint32_t buf_bytes) {
  uint16_t copy_width = width;

  (void)pitch;
  if (y >= s_frame_height || rows == 0 || copy_width == 0 || !buf) {
    return;
  }
  if (copy_width > s_chunky_pitch) {
    copy_width = s_chunky_pitch;
  }
  if ((uint32_t)copy_width * rows > buf_bytes) {
    rows = (uint16_t)(buf_bytes / copy_width);
  }
  if (rows == 0) {
    return;
  }
  if ((uint32_t)y + rows > s_frame_height) {
    rows = (uint16_t)(s_frame_height - y);
  }

  for (uint16_t row = 0; row < rows; row++) {
    uint8_t *dst = &s_chunky_frame[((uint32_t)(y + row) * s_chunky_pitch)];
    const uint8_t *src = &buf[(uint32_t)row * width];
    stdoom_copy_swap_bytes(dst, src, copy_width);
  }
}

/* ── Command dispatch ────────────────────────────────────────────────────── */

static void stdoom_dispatch_command(const TransmissionProtocol *proto) {
  if (proto->payload_size < 4u) {
    DPRINTF("stdoom_dispatch: short payload for 0x%04X\n", proto->command_id);
    return;
  }

  uint32_t random_token = TPROTO_GET_RANDOM_TOKEN(proto->payload);
  const uint16_t *payload16 = (const uint16_t *)proto->payload;
  const uint16_t *params16 = payload16 + 2;

  switch (proto->command_id) {
    case CMD_STDOOM_PING: {
      s_diag_ping_count++;
      if (stdoom_diag_should_log(s_diag_ping_count)) {
        DPRINTF("stdoom_dispatch: PING #%lu token=%08lX\n",
                (unsigned long)s_diag_ping_count, (unsigned long)random_token);
      }
      stdoom_write_result(STDOOM_VERSION_STRING);
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_DONE);
      stdoom_send_response(random_token);
      break;
    }
    case CMD_STDOOM_INIT: {
      uint32_t d3;
      uint16_t width;
      uint16_t height;

      if (proto->payload_size < 8u) {
        DPRINTF("stdoom_dispatch: short INIT payload\n");
        return;
      }

      d3 = ((uint32_t)params16[1] << 16) | params16[0];
      width = (uint16_t)(d3 >> 16);
      height = (uint16_t)(d3 & 0xFFFFu);

      s_diag_init_count++;
      if (stdoom_diag_should_log(s_diag_init_count)) {
        DPRINTF("stdoom_dispatch: INIT #%lu width=%u height=%u\n",
                (unsigned long)s_diag_init_count, (unsigned)width,
                (unsigned)height);
      }

      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_BUSY);
      stdoom_init_frame_state(width, height);
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_DONE);
      stdoom_send_response(random_token);
      break;
    }
    case CMD_STDOOM_SET_MAP: {
      uint32_t map_bytes;
      const uint8_t *map_buf;

      if (proto->payload_size < 16u) {
        DPRINTF("stdoom_dispatch: short SET_MAP payload\n");
        return;
      }

      map_bytes = (uint32_t)proto->payload_size - 16u;
      if (map_bytes > 256u) {
        map_bytes = 256u;
      }
      map_buf = (const uint8_t *)&params16[6];

      s_diag_set_map_count++;
      if (stdoom_diag_should_log(s_diag_set_map_count)) {
        DPRINTF("stdoom_dispatch: SET_MAP #%lu bytes=%lu\n",
                (unsigned long)s_diag_set_map_count, (unsigned long)map_bytes);
      }

      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_BUSY);
      if ((map_bytes & 1u) == 0u) {
        stdoom_copy_swap_bytes(s_doom8_to_st4, map_buf, map_bytes);
      } else {
        uint32_t even_bytes = map_bytes & ~1u;
        stdoom_copy_swap_bytes(s_doom8_to_st4, map_buf, even_bytes);
        s_doom8_to_st4[even_bytes] = map_buf[even_bytes];
      }
      if (map_bytes < 256u) {
        for (uint32_t i = map_bytes; i < 256u; i++) {
          s_doom8_to_st4[i] = (uint8_t)(i & 0x0Fu);
        }
      }
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_DONE);
      stdoom_send_response(random_token);
      break;
    }
    case CMD_STDOOM_BLIT_ROWS: {
      uint32_t d3;
      uint32_t d4;
      uint32_t d5;
      uint16_t y;
      uint16_t width;
      uint16_t rows;
      uint16_t pitch;
      uint32_t buf_bytes;
      const uint8_t *buf;

      if (proto->payload_size < 16u) {
        DPRINTF("stdoom_dispatch: short BLIT_ROWS payload\n");
        return;
      }

      d3 = ((uint32_t)params16[1] << 16) | params16[0];
      d4 = ((uint32_t)params16[3] << 16) | params16[2];
      d5 = ((uint32_t)params16[5] << 16) | params16[4];
      y = (uint16_t)(d3 & 0xFFFFu);
      width = (uint16_t)(d4 >> 16);
      rows = (uint16_t)(d4 & 0xFFFFu);
      pitch = (uint16_t)(d5 >> 16);
      buf = (const uint8_t *)&params16[6];
      buf_bytes = (uint32_t)proto->payload_size - 16u;

      s_diag_blit_rows_count++;
      if (stdoom_diag_should_log(s_diag_blit_rows_count)) {
        DPRINTF("stdoom_dispatch: BLIT_ROWS #%lu y=%u rows=%u width=%u pitch=%u\n",
                (unsigned long)s_diag_blit_rows_count, (unsigned)y,
                (unsigned)rows, (unsigned)width, (unsigned)pitch);
      }

      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_BUSY);
      stdoom_store_chunky_rows(y, rows, width, pitch, buf, buf_bytes);
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_DONE);
      stdoom_send_response(random_token);
      break;
    }
    case CMD_STDOOM_C2P: {
      s_diag_c2p_count++;
      if (stdoom_diag_should_log(s_diag_c2p_count)) {
        DPRINTF("stdoom_dispatch: C2P #%lu\n",
                (unsigned long)s_diag_c2p_count);
      }
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_BUSY);
      stdoom_pack_to_planar();
      *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_DONE);
      stdoom_send_response(random_token);
      break;
    }
    default:
      DPRINTF("stdoom_dispatch: ignoring command 0x%04X\n", proto->command_id);
      break;
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void stdoom_worker_init(void) {
  s_rom_base = (uint32_t)&__rom_in_ram_start__;
  s_token_addr = s_rom_base + STDOOM_RANDOM_TOKEN_OFFSET;
  s_token_seed_addr = s_rom_base + STDOOM_RANDOM_TOKEN_SEED_OFFSET;
  s_result_mem = (volatile char *)(s_rom_base + STDOOM_RESULT_OFFSET);
  s_status_mem = (volatile uint16_t *)(s_rom_base + STDOOM_STATUS_OFFSET);
  s_ready_mem = (volatile uint16_t *)(s_rom_base + STDOOM_READY_OFFSET);
  s_planar_mem = (volatile uint16_t *)(s_rom_base + STDOOM_PLANAR0_OFFSET);

  *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_IDLE);
  *s_ready_mem = 0;
  stdoom_init_frame_state(STDOOM_FRAME_WIDTH, STDOOM_FRAME_HEIGHT);

  /* Seed the random token with a guaranteed non-zero value (see
   * stdoom_next_seed: rand() would yield 0 here without an RTC). */
  TPROTO_SET_RANDOM_TOKEN(s_token_seed_addr, stdoom_next_seed());

  /* Drain anything the protocol parser has already latched before arming. */
  TransmissionProtocol drain;
  for (int i = 0; i < 50; i++) {
    while (stdoom_consume_protocol(&drain)) {
      DPRINTF("stdoom_worker_init: draining startup cmd 0x%04X payload=%u\n",
              drain.command_id, (unsigned)drain.payload_size);
    }
    sleep_ms(1);
  }

  /* Publish the ready magic now that the worker can answer commands. */
  *s_ready_mem = STDOOM_READY_WORD;
  s_dispatch_armed = true;

  DPRINTF("STDOOM Accelerator ready. PING=0x%02X\n", CMD_STDOOM_PING);
}

/* TEMPORARY: publish the diagnostic counters into spare shared-memory slots so
 * the ST can display them. Offsets are above the result-buffer-free region and
 * unused by the protocol. ST reads at ROM4_ADDR + offset. */
#define STDOOM_DBG_ROM3_IRQ_OFFSET 0xF010
#define STDOOM_DBG_CMD_OFFSET      0xF014
#define STDOOM_DBG_CHK_ERR_OFFSET  0xF018
#define STDOOM_DBG_LAST_CMD_OFFSET 0xF01C

void __not_in_flash_func(stdoom_worker_loop)(void) {
  if (stdoom_consume_protocol(&s_loop_proto)) {
    if (!s_dispatch_armed) {
      DPRINTF("stdoom_worker_loop: dropping pre-arm cmd 0x%04X\n",
              s_loop_proto.command_id);
    } else {
      stdoom_dispatch_command(&s_loop_proto);
    }
  }

  /* Publish diagnostic counters every iteration. */
  *(volatile uint32_t *)(s_rom_base + STDOOM_DBG_ROM3_IRQ_OFFSET) =
      stdoom_dbg_rom3_irq;
  *(volatile uint32_t *)(s_rom_base + STDOOM_DBG_CMD_OFFSET) = stdoom_dbg_cmd;
  *(volatile uint32_t *)(s_rom_base + STDOOM_DBG_CHK_ERR_OFFSET) =
      stdoom_dbg_chk_err;
  *(volatile uint32_t *)(s_rom_base + STDOOM_DBG_LAST_CMD_OFFSET) =
      (uint32_t)stdoom_dbg_last_cmd;
}
