/*
 * pingtest.c - SidecarTridge DOOM Accelerator smoke test.
 *
 * Standalone TOS app; loops until ESC, re-running detect + Milestone 2 C2P
 * smoke test on each keypress.
 *
 * Build (from repo root):
 *   make -C sidecart pingtest
 */
#include <mint/osbind.h>
#include <stdio.h>

#include "sidecart_md.h"

static void fill_test_map(unsigned char *map)
{
    int i;

    for (i = 0; i < 256; i++) {
        map[i] = (unsigned char)(((i * 13u) + 7u) & 0x0Fu);
    }
}

static void fill_test_frame(unsigned char *frame)
{
    unsigned short y;
    unsigned short x;

    for (y = 0; y < STDOOM_FRAME_HEIGHT; y++) {
        for (x = 0; x < STDOOM_FRAME_WIDTH; x++) {
            frame[(unsigned long)y * STDOOM_FRAME_WIDTH + x] =
                (unsigned char)((x + (y * 3u)) & 0xFFu);
        }
    }
}

static unsigned long checksum_planar(const volatile unsigned short *planar,
                                     unsigned short words)
{
    unsigned long checksum = 0;
    unsigned short i;

    for (i = 0; i < words; i++) {
        checksum = (checksum << 1) ^ planar[i];
    }

    return checksum;
}

static void print_planar_sample(const volatile unsigned short *planar)
{
    unsigned short i;

    printf("planar[0..7]=");
    for (i = 0; i < 8; i++) {
        printf("%s%04X", (i == 0) ? "" : " ", (unsigned)planar[i]);
    }
    printf("\n");
}

int main(void)
{
    static unsigned char map[256];
    static unsigned char chunky[STDOOM_CHUNKY_SIZE];
    long key;

    fill_test_map(map);
    fill_test_frame(chunky);

    printf("DOOM Accelerator ping test\n");
    printf("Press any key to run detection/C2P, ESC to quit.\n\n");

    for (;;) {
        int stage = 0;
        int ping_rc = -2;
        unsigned char ready = 0;
        unsigned long seed = 0;
        int detected;

        key = Cconin();
        if ((key & 0xFF) == 27) {
            break;
        }

        detected = sidecart_md_detect_verbose(&stage, &ready, &seed, &ping_rc);

        printf("ready=$%02X(want $%02X) seed=$%08lX\n",
               (unsigned)ready, (unsigned)STDOOM_READY_MAGIC,
               (unsigned long)seed);
        printf("stage=%d ping=%d detected=%d\n", stage, ping_rc, detected);
        printf("irq=%lX cmd=%lX ce=%lX lc=%lX\n",
               (unsigned long)*STDOOM_DBG_ROM3_IRQ_ADDR,
               (unsigned long)*STDOOM_DBG_CMD_ADDR,
               (unsigned long)*STDOOM_DBG_CHK_ERR_ADDR,
               (unsigned long)*STDOOM_DBG_LAST_CMD_ADDR);

        if (detected) {
            char version[64];
            int init_rc;
            int map_rc = 0;
            int blit_rc = 0;
            int c2p_rc = 0;
            volatile unsigned short *planar =
                (volatile unsigned short *)STDOOM_PLANAR0_ADDR;
            unsigned short y;

            sidecart_md_result(version, sizeof(version));
            printf("DETECTED: %s\n", version);

            init_rc = sidecart_md_init(STDOOM_FRAME_WIDTH, STDOOM_FRAME_HEIGHT);
            printf("INIT rc=%d\n", init_rc);
            if (init_rc == 0) {
                map_rc = sidecart_md_set_map(map);
                printf("SET_MAP rc=%d\n", map_rc);
            }

            if (init_rc == 0 && map_rc == 0) {
                for (y = 0; y < STDOOM_FRAME_HEIGHT; y += 6) {
                    unsigned short rows = (unsigned short)(STDOOM_FRAME_HEIGHT - y);
                    if (rows > 6) {
                        rows = 6;
                    }
                    if (sidecart_md_blit_rows(
                            y, rows, STDOOM_FRAME_WIDTH, STDOOM_FRAME_WIDTH,
                            chunky + ((unsigned long)y * STDOOM_FRAME_WIDTH)) != 0) {
                        blit_rc = -1;
                        break;
                    }
                }
                printf("BLIT_ROWS rc=%d\n", blit_rc);
            }

            if (init_rc == 0 && map_rc == 0 && blit_rc == 0) {
                c2p_rc = sidecart_md_c2p();
                printf("C2P rc=%d\n", c2p_rc);
            }

            if (init_rc == 0 && map_rc == 0 && blit_rc == 0 && c2p_rc == 0) {
                print_planar_sample(planar);
                printf("planar checksum[64]=%08lX\n",
                       checksum_planar(planar, 64));
            }
            printf("\n");
        } else {
            printf("not detected\n\n");
        }
    }

    return 0;
}
