/**
 * File: stdoom_protocol.c
 * Description: Protocol-only DMA bridge for STDOOM Coprocessor (no terminal UI side
 *              effects). Decodes the SidecarTridge transmission protocol off
 *              the cartridge bus and publishes completed commands to Core 0.
 */

#include "stdoom_protocol.h"

#include <string.h>

#include "debug.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "romemul.h"

#define STDOOM_ADDRESS_HIGH_BIT 0x8000u

static TransmissionProtocol s_protocol_buffers[2];
static volatile uint8_t s_protocol_read_index = 0;
static volatile uint8_t s_protocol_write_index = 1;
static volatile bool s_protocol_ready = false;

/* TEMPORARY diagnostics (published to shared memory by the worker, read on the
 * ST screen) to localise why detection fails:
 *   rom3_irq = times the lookup IRQ saw a ROM3 access (capture works)
 *   cmd      = times a command fully parsed + checksum-matched (parse works)
 *   chk_err  = checksum mismatches
 *   last_cmd = command_id of the last fully-parsed command                  */
volatile uint32_t stdoom_dbg_rom3_irq = 0;
volatile uint32_t stdoom_dbg_cmd = 0;
volatile uint32_t stdoom_dbg_chk_err = 0;
volatile uint16_t stdoom_dbg_last_cmd = 0;

static inline void __not_in_flash_func(stdoom_handle_protocol_command)(
    const TransmissionProtocol *protocol) {
  uint8_t write_index = s_protocol_write_index;
  TransmissionProtocol *write_buffer = &s_protocol_buffers[write_index];

  stdoom_dbg_cmd++;
  stdoom_dbg_last_cmd = protocol->command_id;

  write_buffer->command_id = protocol->command_id;
  write_buffer->payload_size = protocol->payload_size;
  write_buffer->bytes_read = protocol->bytes_read;
  write_buffer->final_checksum = protocol->final_checksum;

  uint16_t payload_size = protocol->payload_size;
  if (payload_size > MAX_PROTOCOL_PAYLOAD_SIZE) {
    payload_size = MAX_PROTOCOL_PAYLOAD_SIZE;
  }
  memcpy(write_buffer->payload, protocol->payload, payload_size);

  uint8_t read_index = s_protocol_read_index;
  s_protocol_read_index = write_index;
  s_protocol_write_index = read_index;
  s_protocol_ready = true;
}

static inline void __not_in_flash_func(stdoom_handle_protocol_checksum_error)(
    const TransmissionProtocol *protocol) {
  stdoom_dbg_chk_err++;
  DPRINTF("Checksum error detected (ID=%u, Size=%u)\n", protocol->command_id,
          protocol->payload_size);
}

void __not_in_flash_func(stdoom_dma_irq_handler_lookup)(void) {
  int lookup_channel = romemul_getLookupDataRomDmaChannel();
  if ((lookup_channel < 0) || (lookup_channel >= NUM_DMA_CHANNELS)) {
    return;
  }

  dma_hw->ints1 = 1u << (uint)lookup_channel;

  uint32_t addr = dma_hw->ch[(uint)lookup_channel].al3_read_addr_trig;
  if (__builtin_expect((addr & 0x00010000u) != 0u, 0)) {
    stdoom_dbg_rom3_irq++;
    uint16_t addr_lsb = (uint16_t)(addr ^ STDOOM_ADDRESS_HIGH_BIT);
    tprotocol_parse(addr_lsb, stdoom_handle_protocol_command,
                    stdoom_handle_protocol_checksum_error);
  }
}

bool __not_in_flash_func(stdoom_consume_protocol)(TransmissionProtocol *out) {
  bool ready = false;
  uint32_t irq_state = save_and_disable_interrupts();
  if (s_protocol_ready) {
    *out = s_protocol_buffers[s_protocol_read_index];
    s_protocol_ready = false;
    ready = true;
  }
  restore_interrupts(irq_state);
  return ready;
}
