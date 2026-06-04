/*
 * sctest.c - SidecarTridge STDOOM Coprocessor detection test.
 *
 * Standalone TOS app; loops until ESC, re-running detect on each keypress.
 * Build (from repo root):
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 \
 *     -I sidecart/test -I linuxdoom-1.10 \
 *     -o sidecart/test/SCTEST.TOS \
 *     sidecart/test/sctest.c linuxdoom-1.10/sidecart_md.c linuxdoom-1.10/sidecart_stubs.S
 */
#include <stdio.h>
#include <mint/osbind.h>
#include "sidecart_md.h"

int main(void)
{
    printf("STDOOM SidecarTridge detection test\n");
    printf("Press any key to ping, ESC to quit.\n\n");

    for (;;) {
        long key = Cconin();
        if ((key & 0xFF) == 27) break; /* ESC */

        int stage = 0, ping_rc = -2;
        unsigned char ready = 0;
        unsigned long seed = 0;

        int detected = sidecart_md_detect_verbose(&stage, &ready, &seed, &ping_rc);

        printf("ready=$%02X(want $%02X) seed=$%08lX\n",
               (unsigned)ready, (unsigned)STDOOM_READY_MAGIC,
               (unsigned long)seed);
        printf("stage=%d ping=%d detected=%d\n", stage, ping_rc, detected);

        /* RP-side diagnostic counters (byte-swapped: display raw hex,
         * true value = swap the two 16-bit halves). */
        printf("irq=%lX cmd=%lX ce=%lX lc=%lX\n",
               (unsigned long)*STDOOM_DBG_ROM3_IRQ_ADDR,
               (unsigned long)*STDOOM_DBG_CMD_ADDR,
               (unsigned long)*STDOOM_DBG_CHK_ERR_ADDR,
               (unsigned long)*STDOOM_DBG_LAST_CMD_ADDR);

        if (detected) {
            char version[64];
            sidecart_md_result(version, sizeof(version));
            printf("DETECTED: %s\n\n", version);
        } else {
            printf("not detected\n\n");
        }
    }

    return 0;
}
