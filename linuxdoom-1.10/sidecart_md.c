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

/* ── Inter-command settle delay ───────────────────────────────────────────── */
/* Ported from md-js (mdjs.c). A freshly-booted SidecarTridge can lose the
 * first protocol command(s) to a cartridge-bus settle race in the RP's
 * protocol parser. A short delay before the first command eliminates it.
 * md-js found 6ms reliable on an 8 MHz 68000; we use a generous margin here
 * since detection runs once. */
static void sidecart_md_settle(void) {
  volatile long i;
  for (i = 0; i < 200000L; i++) {
  }
}

/* ── Detection ──────────────────────────────────────────────────────────── */
/* Aligned with md-js's proven mdjs_ping(): a single PING with payload_size 0
 * (the 4-byte random token is added by the stub internally). Detection is
 * authoritative on the token handshake, exactly as md-js/examples/mdjscode
 * checks for firmware. The ready byte and seed are read for the on-screen
 * diagnostic only.
 *
 * Diagnostic stage codes (via *stage):
 *   0 = success
 *   1 = ready magic mismatch (recorded, no longer aborts)
 *   2 = seed looks dead (recorded, no longer aborts)
 *   3 = PING handshake timed out                                            */
int sidecart_md_detect_verbose(int *stage, unsigned char *ready,
                               unsigned long *seed_out, int *ping_rc) {
  unsigned char ready_val;
  unsigned long seed;
  int rc;
  int attempt;

  ready_val = *STDOOM_READY_ADDR;
  if (ready) *ready = ready_val;

  seed = *STDOOM_SEED_ADDR;
  if (seed_out) *seed_out = seed;

  if (stage) *stage = 0;
  if (ready_val != STDOOM_READY_MAGIC) {
    if (stage) *stage = 1;
  }
  if (seed == 0UL || seed == 0xFFFFFFFFUL) {
    if (stage) *stage = 2;
  }

  /* Settle the bus then PING. Detection runs before sound VBL install
   * (moved to I_Init in i_system.c), so no interrupt-disable needed. */
  sidecart_md_settle();

  (void)attempt;
  rc = stdoom_send_sync_command(CMD_STDOOM_PING, 0, 0L, 0L);
  if (ping_rc) *ping_rc = rc;
  if (rc != 0) {
    if (stage) *stage = 3;
    return 0;
  }

  if (stage) *stage = 0;
  return 1;
}

int sidecart_md_detect(void) {
  return sidecart_md_detect_verbose(0, 0, 0, 0);
}
