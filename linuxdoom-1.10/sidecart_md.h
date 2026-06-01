/**
 * File: sidecart_md.h
 * Description: STDOOM Coprocessor ST-side client library — public header.
 *
 * Provides a small C API for STDOOM to detect and talk to the STDOOM Coprocessor
 * coprocessor running on a SidecarTridge Multi-device. The low-level
 * SidecarTridge command protocol is implemented in sidecart_stubs.S; this
 * header/implementation pair wraps it for use from the Doom C code.
 *
 * Milestone 1: detection only (sidecart_md_detect via PING).
 * Milestone 2 adds the C2P offload entry points.
 */

#ifndef SIDECART_MD_H
#define SIDECART_MD_H

/* ── Command IDs (must match stdoom_commands.h on the RP2040 side) ───────── */
#define CMD_STDOOM_PING 0x0010
/* Milestone 2: INIT 0x11, SET_MAP 0x12, BLIT_ROWS 0x13, C2P 0x14 */

/* PING magic — sent in d3. The firmware echoes the random token for any PING,
 * so this is informational/handshake padding ('STDM'). */
#define STDOOM_PING_MAGIC 0x5354444DL /* 'STDM' */

/* ── Shared memory addresses (ROM4 base $FA0000) ────────────────────────── */
#define STDOOM_TOKEN_ADDR ((volatile unsigned long *)0xFAF000L)
#define STDOOM_SEED_ADDR ((volatile unsigned long *)0xFAF004L)
#define STDOOM_STATUS_ADDR ((volatile unsigned char *)0xFAF008L)
#define STDOOM_READY_ADDR ((volatile unsigned char *)0xFAF00AL)
#define STDOOM_RESULT_ADDR ((volatile char *)0xFAF100L)
#define STDOOM_PLANAR0_ADDR 0xFA0000L

/* Ready magic written by the firmware once the worker is up (STDOOM_READY_MAGIC
 * = 'S' in stdoom_commands.h). The firmware writes it to both bytes of the bus
 * word, so a byte read at the even address $FAF00A returns it directly. */
#define STDOOM_READY_MAGIC 0x53

/* Status values (mirror STDOOM_STATUS_* in stdoom_commands.h). */
#define STDOOM_STATUS_IDLE 0x00
#define STDOOM_STATUS_BUSY 0x01
#define STDOOM_STATUS_DONE 0x02

#define STDOOM_RESULT_SIZE 2048

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief Detect whether the STDOOM Coprocessor firmware is present.
 *
 * Checks the ready magic, guards against a stale/absent cartridge via the
 * random seed, then issues a PING and waits for the token handshake. On
 * success the firmware version string is available at STDOOM_RESULT_ADDR.
 *
 * @return 1 if the coprocessor is present and responding, 0 otherwise.
 */
int sidecart_md_detect(void);

/**
 * @brief Copy the firmware version string (set by the last PING) into a C
 *        buffer, un-swapping the big-endian word storage.
 * @param buf  Destination buffer.
 * @param size Size of buf in bytes.
 */
void sidecart_md_result(char *buf, int size);

#endif /* SIDECART_MD_H */
