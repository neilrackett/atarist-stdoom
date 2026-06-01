/**
 * File: stdoom_worker.c
 * Description: STDOOM Coprocessor worker (Core 0).
 *
 * Milestone 1: detection only. The worker publishes a ready magic at boot,
 * seeds the random token, and answers CMD_STDOOM_PING by writing a version
 * string into the result buffer and echoing the random token to unblock the
 * ST. Heavier render offload (C2P) is added on Core 1 in a later milestone.
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
#define STDOOM_VERSION_STRING "STDOOM Coprocessor/1.0"

/* Cached addresses (set in stdoom_worker_init, read-only thereafter). */
static uint32_t s_rom_base;
static uint32_t s_token_addr;
static uint32_t s_token_seed_addr;
static volatile char *s_result_mem;
static volatile uint16_t *s_status_mem;
static volatile uint16_t *s_ready_mem;

/* Drain spurious commands the PIO/parser may latch during cold start. */
static volatile bool s_dispatch_armed = false;
static TransmissionProtocol s_loop_proto;

#define STDOOM_BUS_BYTE_WORD(value) ((uint16_t)((uint16_t)(value) << 8))

/* Ready flag: write the magic to BOTH bytes of the bus word so the ST sees the
 * same byte regardless of which half of the 16-bit word is at the even
 * address. */
#define STDOOM_READY_WORD \
  ((uint16_t)((STDOOM_READY_MAGIC << 8) | STDOOM_READY_MAGIC))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Write the random token back to shared memory to unblock the ST and seed the
 * next token value. */
static void stdoom_send_response(uint32_t random_token) {
  TPROTO_SET_RANDOM_TOKEN(s_token_addr, random_token);
  uint32_t new_seed = rand();
  TPROTO_SET_RANDOM_TOKEN(s_token_seed_addr, new_seed);
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
  /* Round up to an even length so the final byte is not dropped by the
   * word-wise swap. */
  size_t even_len = (len + 2u) & ~(size_t)1u;
  if (even_len > sizeof(tmp)) {
    even_len = sizeof(tmp);
  }
  COPY_AND_CHANGE_ENDIANESS_BLOCK16(tmp, (void *)s_result_mem, even_len);
}

/* ── Command dispatch ────────────────────────────────────────────────────── */

static void stdoom_dispatch_command(const TransmissionProtocol *proto) {
  if (proto->payload_size < 4u) {
    DPRINTF("stdoom_dispatch: short payload for 0x%04X\n", proto->command_id);
    return;
  }

  uint32_t random_token = TPROTO_GET_RANDOM_TOKEN(proto->payload);

  switch (proto->command_id) {
    case CMD_STDOOM_PING: {
      stdoom_write_result(STDOOM_VERSION_STRING);
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

  *s_status_mem = STDOOM_BUS_BYTE_WORD(STDOOM_STATUS_IDLE);
  *s_ready_mem = 0;

  /* Seed the random token in shared memory. */
  srand((unsigned int)time(NULL));
  uint32_t seed = rand();
  TPROTO_SET_RANDOM_TOKEN(s_token_seed_addr, seed);

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

  DPRINTF("STDOOM Coprocessor ready. PING=0x%02X\n", CMD_STDOOM_PING);
}

void __not_in_flash_func(stdoom_worker_loop)(void) {
  if (stdoom_consume_protocol(&s_loop_proto)) {
    if (!s_dispatch_armed) {
      DPRINTF("stdoom_worker_loop: dropping pre-arm cmd 0x%04X\n",
              s_loop_proto.command_id);
    } else {
      stdoom_dispatch_command(&s_loop_proto);
    }
  }
}
