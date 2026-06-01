/**
 * File: sidecart_md.c
 * Description: STDOOM Coprocessor ST-side client library implementation.
 *
 * Wraps the SidecarTridge low-level command protocol (send_sync, implemented
 * in sidecart_stubs.S) for use from the Doom C code. Register marshalling is
 * done in the assembly wrappers to keep this C ABI-safe on m68k.
 */

#include "sidecart_md.h"

/* C ABI entry points provided by sidecart_stubs.S.
 *   send_sync_command(cmd, payload_size, d3, d4)
 *     payload_size is in bytes, EXCLUDING the 4-byte token (added internally).
 *     Returns 0 on success (token handshake completed), -1 on timeout. */
extern int stdoom_send_sync_command(int cmd, int payload_size, long d3, long d4);

/* ── Read the result string from STDOOM_RESULT_ADDR ─────────────────────── */
/* The RP2040 stores the result with a 16-bit byte swap (big-endian on the
 * m68k bus). Un-swap while copying. */
void sidecart_md_result(char *buf, int size) {
  volatile unsigned short *src = (volatile unsigned short *)STDOOM_RESULT_ADDR;
  unsigned char *dst = (unsigned char *)buf;
  int max = size - 1;
  int i = 0;

  while (i < max) {
    unsigned short w = *src++;
    unsigned char hi = (unsigned char)(w >> 8);
    unsigned char lo = (unsigned char)(w & 0xFF);
    if (hi == 0) break;
    dst[i++] = hi;
    if (lo == 0) break;
    if (i < max) dst[i++] = lo;
  }
  dst[i] = '\0';
}

/* ── Detection ──────────────────────────────────────────────────────────── */
int sidecart_md_detect(void) {
  unsigned long seed;

  /* 1. The firmware writes the ready magic to both bytes of the bus word once
   *    the worker is up. Absent firmware leaves this unset. */
  if (*STDOOM_READY_ADDR != STDOOM_READY_MAGIC) {
    return 0;
  }

  /* 2. Guard against a stale/absent cartridge: the firmware seeds the token
   *    from rand(), so an all-zero or all-ones seed means nothing is driving
   *    the bus. */
  seed = *STDOOM_SEED_ADDR;
  if (seed == 0UL || seed == 0xFFFFFFFFUL) {
    return 0;
  }

  /* 3. Issue a PING (payload_size 4 → token + d3 magic) and wait for the
   *    token handshake. send_sync returns 0 only when the firmware echoes the
   *    token back, which proves the worker is alive and dispatching. */
  if (stdoom_send_sync_command(CMD_STDOOM_PING, 4, STDOOM_PING_MAGIC, 0L) != 0) {
    return 0;
  }

  return 1;
}
