/**
 * File: emul.c
 * Description: STDOOM Accelerator runtime bootstrap (ROM emulation + accelerator loop).
 */

#include "emul.h"

#include <stdint.h>

#include "debug.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "romemul.h"
#include "stdoom_commands.h"
#include "stdoom_protocol.h"
#include "target_firmware.h"

#define SLEEP_LOOP_MS 1

void emul_publish_rom(void) {
  /* Copy the ST-side cartridge program into ROM-in-RAM. */
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  /* Serve cartridge bus requests using the protocol-only DMA lookup handler. */
  init_romemul(NULL, stdoom_dma_irq_handler_lookup, false);
}

void emul_arm_worker(void) {
  /* Initialise the accelerator worker (seeds token, publishes ready magic). */
  stdoom_worker_init();
}

void emul_start(void) {
  while (true) {
    stdoom_worker_loop();
    sleep_ms(SLEEP_LOOP_MS);
  }
}
