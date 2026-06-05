/**
 * File: emul.h
 * Author: Diego Parrilla Santamaría
 * Date: January 20205, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS SL
 * Description: Header for the MD/JS runtime bootstrap.
 */

#ifndef EMUL_H
#define EMUL_H

/**
 * @brief Publish the cartridge image into ROM-in-RAM and initialize ROM
 * emulation so the Atari can see the cartridge during boot.
 */
void emul_publish_rom();

/**
 * @brief Initialize the STDOOM worker state and publish the ready magic.
 *
 * Call after the ROM image has been published so the ST boot stub can detect
 * the accelerator as early as possible.
 */
void emul_arm_worker();

/**
 * @brief Enter the STDOOM worker dispatch loop.
 *
 * Call after the app startup path has finished any slower settings work.
 */
void emul_start();

#endif  // EMUL_H
