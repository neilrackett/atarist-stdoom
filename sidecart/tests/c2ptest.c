/*
 * c2ptest.c - SidecarTridge STDOOM C2P round-trip test.
 *
 * Standalone TOS app that uploads a 320x200 8-bit indexed image, asks the
 * RP2040 to convert it to planar slot 0, and copies the result back to the ST
 * screen using the same planar copy path STDOOM uses.
 *
 * Build (from repo root):
 *   make -C sidecart c2ptest
 */
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "sidecart_md.h"

#define ST_COLOR(r, g, b) (((r & 7) << 8) | ((g & 7) << 4) | ((b & 7) << 0))

static const unsigned char s_test_rgb_palette[16 * 3] = {
    0, 0, 0,
    0, 0, 170,
    0, 170, 0,
    0, 170, 170,
    170, 0, 0,
    170, 0, 170,
    170, 85, 0,
    170, 170, 170,
    85, 85, 85,
    85, 85, 255,
    85, 255, 85,
    85, 255, 255,
    255, 85, 85,
    255, 85, 255,
    255, 255, 85,
    255, 255, 255
};

static unsigned char s_test_map[256];
static unsigned char s_chunky[STDOOM_CHUNKY_SIZE];
static unsigned char s_saved_screen[STDOOM_PLANAR_SIZE];
static unsigned short s_saved_palette[16];
static int s_blitter_present = -1;
static unsigned long s_last_copy_ms = 0;

/*
 * STE blitter register map. The first 16 words at BLT_BASE ($FFFF8A00) are the
 * halftone RAM; the actual registers start at +$20. The earlier +$02..+$1E
 * offsets here were wrong (they wrote into halftone RAM, so the blitter control
 * register was never touched and the blit silently did nothing). Offsets below
 * match the Atari STE hardware reference and md-sprites-demo's main.s.
 */
#define BLT_BASE        0xFFFF8A00UL
#define BLT_SRC_INC_X   (*(volatile unsigned short *)(BLT_BASE + 0x20))
#define BLT_SRC_INC_Y   (*(volatile unsigned short *)(BLT_BASE + 0x22))
#define BLT_SRC_ADDR    (*(volatile unsigned long *)(BLT_BASE + 0x24))
#define BLT_END_MASK1   (*(volatile unsigned short *)(BLT_BASE + 0x28))
#define BLT_END_MASK2   (*(volatile unsigned short *)(BLT_BASE + 0x2A))
#define BLT_END_MASK3   (*(volatile unsigned short *)(BLT_BASE + 0x2C))
#define BLT_DST_INC_X   (*(volatile unsigned short *)(BLT_BASE + 0x2E))
#define BLT_DST_INC_Y   (*(volatile unsigned short *)(BLT_BASE + 0x30))
#define BLT_DST_ADDR    (*(volatile unsigned long *)(BLT_BASE + 0x32))
#define BLT_X_COUNT     (*(volatile unsigned short *)(BLT_BASE + 0x36))
#define BLT_Y_COUNT     (*(volatile unsigned short *)(BLT_BASE + 0x38))
#define BLT_HOP         (*(volatile unsigned char *)(BLT_BASE + 0x3A))
#define BLT_OP          (*(volatile unsigned char *)(BLT_BASE + 0x3B))
#define BLT_CTRL        (*(volatile unsigned char *)(BLT_BASE + 0x3C))

#define BLT_CTRL_BUSY   0x80u
#define BLT_CTRL_HOG    0x40u
#define BLT_HOP_SOURCE  0x02u
#define BLT_OP_COPY     0x03u

static int blitter_present(void)
{
    if (s_blitter_present < 0) {
        long mode = Blitmode(-1);
        s_blitter_present = (mode & 1) != 0;
    }
    return s_blitter_present;
}

static unsigned short convert_channel(unsigned char v)
{
    unsigned short r = (unsigned short)((v & 0xE0u) >> 5);
    r |= (unsigned short)((v & 0x10u) >> 1);
    return r;
}

static unsigned short stcolor(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned short entry = convert_channel(r);
    entry <<= 4;
    entry |= convert_channel(g);
    entry <<= 4;
    entry |= convert_channel(b);
    return entry;
}

static void install_st_palette(const unsigned short *palette)
{
    long old_stack = Super(0L);
    volatile unsigned short *reg = (volatile unsigned short *)0xFF8240;
    short n;

    for (n = 0; n < 16; n++) {
        *reg++ = *palette++;
    }
    if (old_stack != 0) {
        Super((void *)old_stack);
    }
}

static void save_st_palette(unsigned short *palette)
{
    long old_stack = Super(0L);
    volatile unsigned short *reg = (volatile unsigned short *)0xFF8240;
    short n;

    for (n = 0; n < 16; n++) {
        *palette++ = *reg++;
    }
    if (old_stack != 0) {
        Super((void *)old_stack);
    }
}

/*
 * Diagnostic: force the CPU longword copy instead of the STE blitter. The CPU
 * can read the SidecarTridge emulated ROM (the planar checksum proves it); the
 * blitter is a separate bus master and may not get valid data from the RP2040
 * ROM emulation. Set to 1 to test whether the CPU path makes the image appear.
 */
#define C2PTEST_FORCE_CPU_COPY 0

static void copy_planar_to_screen(unsigned long src_addr, unsigned char *dst)
{
    if (!C2PTEST_FORCE_CPU_COPY && blitter_present()) {
        long old_stack = Super(0L);
        short old_sr;

        __asm__ volatile(
            "move.w %%sr,%0\n\t"
            "ori.w #0x0700,%%sr"
            : "=d"(old_sr)
            :
            : "cc");

        BLT_SRC_INC_X = 2;
        BLT_SRC_INC_Y = 0;
        BLT_SRC_ADDR = src_addr;
        BLT_END_MASK1 = 0xFFFFu;
        BLT_END_MASK2 = 0xFFFFu;
        BLT_END_MASK3 = 0xFFFFu;
        BLT_DST_INC_X = 2;
        BLT_DST_INC_Y = 0;
        BLT_DST_ADDR = (unsigned long)dst;
        BLT_X_COUNT = (unsigned short)(STDOOM_PLANAR_SIZE / 2);
        BLT_Y_COUNT = 1;
        BLT_HOP = BLT_HOP_SOURCE;
        BLT_OP = BLT_OP_COPY;
        BLT_CTRL = (unsigned char)(BLT_CTRL_HOG | BLT_CTRL_BUSY);
        while (BLT_CTRL & BLT_CTRL_BUSY) {
        }

        __asm__ volatile(
            "move.w %0,%%sr"
            :
            : "d"(old_sr)
            : "cc");

        if (old_stack != 0) {
            Super((void *)old_stack);
        }
        return;
    }

    memcpy(dst, (const void *)src_addr, STDOOM_PLANAR_SIZE);
}

static unsigned long timer_ticks(void)
{
    long old_stack = Super(0L);
    volatile unsigned long *hz_200 = (volatile unsigned long *)0x4BAL;
    unsigned long ticks = *hz_200;

    if (old_stack != 0) {
        Super((void *)old_stack);
    }
    return ticks;
}

static unsigned long elapsed_ms(unsigned long start, unsigned long end)
{
    return (end - start) * 5UL;
}

static void fill_test_map(unsigned char *map)
{
    int i;

    for (i = 0; i < 256; i++) {
        map[i] = (unsigned char)(i >> 4);
    }
}

static void fill_test_frame(unsigned char *frame)
{
    unsigned short y;
    unsigned short x;

    for (y = 0; y < STDOOM_FRAME_HEIGHT; y++) {
        for (x = 0; x < STDOOM_FRAME_WIDTH; x++) {
            frame[(unsigned long)y * STDOOM_FRAME_WIDTH + x] =
                (unsigned char)(((x * 3u) + (y * 5u)) & 0xFFu);
        }
    }
}

static void build_test_palette(unsigned short *palette)
{
    int i;

    for (i = 0; i < 16; i++) {
        const unsigned char *c = &s_test_rgb_palette[i * 3];
        palette[i] = stcolor(c[0], c[1], c[2]);
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

    printf("planar[0..4]=");
    for (i = 0; i < 5; i++) {
        printf("%s%04X", (i == 0) ? "" : " ", (unsigned)planar[i]);
    }
    printf("\n");
}

/* Split across three <=40-column lines for the ST low-res console. */
static void print_shared_state(const char *label)
{
    printf("%s rdy=$%02X seed=$%08lX\n",
           label,
           (unsigned)*STDOOM_READY_ADDR,
           (unsigned long)*STDOOM_SEED_ADDR);
    printf("st=$%02X irq=%lX cmd=%lX\n",
           (unsigned)*STDOOM_STATUS_ADDR,
           (unsigned long)*STDOOM_DBG_ROM3_IRQ_ADDR,
           (unsigned long)*STDOOM_DBG_CMD_ADDR);
    printf("ce=%lX lc=%lX\n",
           (unsigned long)*STDOOM_DBG_CHK_ERR_ADDR,
           (unsigned long)*STDOOM_DBG_LAST_CMD_ADDR);
}

static void print_map_sample(const unsigned char *map)
{
    unsigned short i;

    printf("map[0..7]=");
    for (i = 0; i < 8; i++) {
        printf("%s%02X", (i == 0) ? "" : " ", (unsigned)map[i]);
    }
    printf("\n");
}

static void print_frame_sample(const unsigned char *frame)
{
    unsigned short i;

    printf("chunky[0..7]=");
    for (i = 0; i < 8; i++) {
        printf("%s%02X", (i == 0) ? "" : " ", (unsigned)frame[i]);
    }
    printf("\n");
}

static void restore_display(void)
{
    memcpy((void *)Physbase(), s_saved_screen, STDOOM_PLANAR_SIZE);
    install_st_palette(s_saved_palette);
}

int main(void)
{
    static unsigned char map[256];
    static unsigned short roundtrip_palette[16];
    static unsigned char *screen;
    long key;

    if (Getrez() != 0) {
        printf("C2PTEST requires low resolution.\n");
        return 1;
    }

    screen = (unsigned char *)Physbase();
    memcpy(s_saved_screen, screen, STDOOM_PLANAR_SIZE);
    save_st_palette(s_saved_palette);

    fill_test_map(map);
    fill_test_frame(s_chunky);
    build_test_palette(roundtrip_palette);

    printf("STDOOM Sidecart C2P round-trip test\n");
    printf("Blitter detected: %d\n", blitter_present());
    printf("Press a key to run; ESC to quit.\n\n");
    print_map_sample(map);
    print_frame_sample(s_chunky);
    printf("\n");

    for (;;) {
        int stage = 0;
        int ping_rc = -2;
        unsigned char ready = 0;
        unsigned long seed = 0;
        unsigned long detect_start;
        unsigned long detect_end;
        int detected;

        key = Cconin();
        if ((key & 0xFF) == 27) {
            break;
        }

        detect_start = timer_ticks();
        detected = sidecart_md_detect_verbose(&stage, &ready, &seed, &ping_rc);
        detect_end = timer_ticks();

        printf("DETECT rdy=$%02X(want$%02X) det=%lums\n",
               (unsigned)ready, (unsigned)STDOOM_READY_MAGIC,
               elapsed_ms(detect_start, detect_end));
        printf("seed=$%08lX\n", (unsigned long)seed);
        printf("stage=%d ping=%d detected=%d\n", stage, ping_rc, detected);
        print_shared_state("POST_DETECT");

        if (detected) {
            char version[64];
            int init_rc;
            int map_rc = 0;
            int blit_rc = 0;
            int c2p_rc = 0;
            unsigned short failed_y = 0;
            unsigned long start_ticks = 0;
            unsigned long init_ticks = 0;
            unsigned long map_ticks = 0;
            unsigned long blit_ticks = 0;
            unsigned long c2p_ticks = 0;
            unsigned long copy_ticks = 0;
            int copy_uses_blitter = (!C2PTEST_FORCE_CPU_COPY) && blitter_present();
            volatile unsigned short *planar =
                (volatile unsigned short *)STDOOM_PLANAR0_ADDR;
            unsigned short y;

            sidecart_md_result(version, sizeof(version));
            printf("DETECTED: %s\n", version);

            install_st_palette(roundtrip_palette);
            start_ticks = timer_ticks();

            init_rc = sidecart_md_init(STDOOM_FRAME_WIDTH, STDOOM_FRAME_HEIGHT);
            init_ticks = timer_ticks();
            if (init_rc == 0) {
                map_rc = sidecart_md_set_map(map);
            }
            map_ticks = timer_ticks();

            if (init_rc == 0 && map_rc == 0) {
                for (y = 0; y < STDOOM_FRAME_HEIGHT; y += 6) {
                    unsigned short rows = (unsigned short)(STDOOM_FRAME_HEIGHT - y);
                    if (rows > 6) {
                        rows = 6;
                    }
                    if (sidecart_md_blit_rows(
                            y, rows, STDOOM_FRAME_WIDTH, STDOOM_FRAME_WIDTH,
                            s_chunky + ((unsigned long)y * STDOOM_FRAME_WIDTH)) != 0) {
                        blit_rc = -1;
                        failed_y = y;
                        break;
                    }
                }
            }
            blit_ticks = timer_ticks();

            if (init_rc == 0 && map_rc == 0 && blit_rc == 0) {
                c2p_rc = sidecart_md_c2p();
            }
            c2p_ticks = timer_ticks();

            if (init_rc == 0 && map_rc == 0 && blit_rc == 0 && c2p_rc == 0) {
                /* Print the timing/checksum diagnostics first, while the test
                 * palette is installed but before the image is blitted. Then
                 * prompt, blit the image clean over the console text, and wait
                 * for a key so the image is actually visible. */
                printf("INIT rc=%d in %lums\n",
                       init_rc, elapsed_ms(start_ticks, init_ticks));
                printf("SET_MAP rc=%d in %lums\n",
                       map_rc, elapsed_ms(init_ticks, map_ticks));
                printf("BLIT_ROWS rc=%d in %lums\n",
                       blit_rc, elapsed_ms(map_ticks, blit_ticks));
                printf("C2P rc=%d in %lums\n",
                       c2p_rc, elapsed_ms(blit_ticks, c2p_ticks));
                printf("ROUND_TRIP total=%lums\n",
                       elapsed_ms(start_ticks, c2p_ticks));
                print_planar_sample(planar);
                printf("planar checksum[64]=%08lX\n",
                       checksum_planar(planar, 64));
                printf("Press key to VIEW; ESC=quit.\n");
                key = Cconin();
                if ((key & 0xFF) == 27) {
                    break;
                }

                /* Time only the copy itself (the keypress above is excluded). */
                {
                    unsigned long copy_start = timer_ticks();
                    copy_planar_to_screen((unsigned long)STDOOM_PLANAR0_ADDR, screen);
                    copy_ticks = timer_ticks();
                    s_last_copy_ms = elapsed_ms(copy_start, copy_ticks);
                }

                /* Image is now on screen. Do NOT print before the keypress or
                 * the console scroll tramples it. Wait, restore, then report. */
                key = Cconin();
                restore_display();
                printf("COPY blitter=%d in %lums\n",
                       copy_uses_blitter, s_last_copy_ms);
                if ((key & 0xFF) == 27) {
                    break;
                }
            } else {
                printf("Failed at:%s%s%s%s\n",
                       (init_rc != 0) ? " INIT" : "",
                       (init_rc == 0 && map_rc != 0) ? " SET_MAP" : "",
                       (init_rc == 0 && map_rc == 0 && blit_rc != 0) ? " BLIT_ROWS" : "",
                       (init_rc == 0 && map_rc == 0 && blit_rc == 0 && c2p_rc != 0) ? " C2P" : "");
                printf("INIT rc=%d in %lums\n",
                       init_rc, elapsed_ms(start_ticks, init_ticks));
                printf("SET_MAP rc=%d in %lums\n",
                       map_rc, elapsed_ms(init_ticks, map_ticks));
                printf("BLIT_ROWS rc=%d in %lums",
                       blit_rc, elapsed_ms(map_ticks, blit_ticks));
                if (blit_rc != 0) {
                    printf(" fail_y=%u", (unsigned)failed_y);
                }
                printf("\n");
                printf("C2P rc=%d in %lums\n",
                       c2p_rc, elapsed_ms(blit_ticks, c2p_ticks));
                print_shared_state("FAIL_STATE");
                printf("Failed; press key to retry.\n");
            }
        } else {
            printf("not detected\n\n");
        }

        printf("Press a key to run; ESC to quit.\n\n");
    }

    restore_display();
    return 0;
}
