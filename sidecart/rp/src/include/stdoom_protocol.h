/**
 * File: stdoom_protocol.h
 * Description: Minimal tprotocol bridge for DOOM Accelerator command ingestion.
 */

#ifndef STDOOM_PROTOCOL_H
#define STDOOM_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"
#include "tprotocol.h"

/* TEMPORARY diagnostic counters (defined in stdoom_protocol.c). */
extern volatile uint32_t stdoom_dbg_rom3_irq;
extern volatile uint32_t stdoom_dbg_cmd;
extern volatile uint32_t stdoom_dbg_chk_err;
extern volatile uint16_t stdoom_dbg_last_cmd;

/**
 * @brief DMA lookup IRQ callback used by ROM emulation.
 *
 * Feeds 16-bit words into tprotocol_parse() and publishes completed commands.
 */
void __not_in_flash_func(stdoom_dma_irq_handler_lookup)(void);

/**
 * @brief Atomically consume the latest decoded protocol command.
 *
 * @param out Destination for the decoded command snapshot.
 * @return true if a command was available, false otherwise.
 */
bool __not_in_flash_func(stdoom_consume_protocol)(TransmissionProtocol *out);

#endif /* STDOOM_PROTOCOL_H */
