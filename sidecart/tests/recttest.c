/*
 * recttest.c - SidecarTridge DOOM Accelerator rect C2P test.
 *
 * Generates a 320x200 8bpp chunky image: a 5x5 grid of 64x40 coloured blocks
 * cycling through all 16 ST colours. Uploads the full frame once, then
 * converts and blits 14 non-overlapping sub-rects of varying sizes (1-cell,
 * 2-wide, 4-wide, 1x2, 2x2) onto the screen in sequence with a short pause
 * between each. Together they tile the full 320x200 frame, proving the rect
 * pipeline works for arbitrary aligned sub-rects.
 *
 * Build (from repo root):
 *   make -C sidecart recttest
 */
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "sidecart_md.h"

#define CELL_W            64u
#define CELL_H            40u
#define GRID_COLS          5u
#define GRID_ROWS          5u
#define PLANAR_ROW_BYTES 160u
#define PLANAR_WORDS_PER_ROW 80u

/* 16 visually distinct ST hardware colours ($RGB, 3 bits per channel). */
static const unsigned short s_test_palette[16] = {
    0x0000, /* 0  black      */
    0x0700, /* 1  red        */
    0x0070, /* 2  green      */
    0x0770, /* 3  yellow     */
    0x0007, /* 4  blue       */
    0x0707, /* 5  magenta    */
    0x0077, /* 6  cyan       */
    0x0777, /* 7  white      */
    0x0400, /* 8  dark red   */
    0x0040, /* 9  dark green */
    0x0004, /* 10 dark blue  */
    0x0740, /* 11 orange     */
    0x0407, /* 12 purple     */
    0x0474, /* 13 teal       */
    0x0333, /* 14 dark grey  */
    0x0555, /* 15 light grey */
};

/*
 * 14 non-overlapping rects that together tile exactly 320x200.
 * Mix of 1x1 (64x40), 2x1 (128x40), 4x1 (256x40), 1x2 (64x80),
 * and 2x2 (128x80) cell regions. All x/w are multiples of 16.
 *
 *  col:  0    1    2    3    4
 *       +----+----+----+----+----+
 * row 0 | #1 |   #2   | #3 | #4 |   #3 spans rows 0-1 (1x2)
 *       +----+----+----+    +----+
 * row 1 | #5 |   #6   |    | #7 |
 *       +----+----+----+----+----+
 * row 2 |      #8          | #9 |   #8 is 4x1
 *       +----+----+----+----+----+
 * row 3 | #10|  #11   | #12|    |   #11 spans rows 3-4 (2x2)
 *       +----+         +----+----+   #12 spans cols 3-4
 * row 4 | #13|         | #14|   |
 *       +----+----+----+----+----+
 */
static const struct { unsigned short x, y, w, h; } s_rects[] = {
    {   0,   0,  64,  40 },  /* #1  1x1: col 0, row 0         */
    {  64,   0, 128,  40 },  /* #2  2x1: cols 1-2, row 0      */
    { 192,   0,  64,  80 },  /* #3  1x2: col 3, rows 0-1      */
    { 256,   0,  64,  40 },  /* #4  1x1: col 4, row 0         */
    {   0,  40,  64,  40 },  /* #5  1x1: col 0, row 1         */
    {  64,  40, 128,  40 },  /* #6  2x1: cols 1-2, row 1      */
    { 256,  40,  64,  40 },  /* #7  1x1: col 4, row 1         */
    {   0,  80, 256,  40 },  /* #8  4x1: cols 0-3, row 2      */
    { 256,  80,  64,  40 },  /* #9  1x1: col 4, row 2         */
    {   0, 120,  64,  40 },  /* #10 1x1: col 0, row 3         */
    {  64, 120, 128,  80 },  /* #11 2x2: cols 1-2, rows 3-4   */
    { 192, 120, 128,  40 },  /* #12 2x1: cols 3-4, row 3      */
    {   0, 160,  64,  40 },  /* #13 1x1: col 0, row 4         */
    { 192, 160, 128,  40 },  /* #14 2x1: cols 3-4, row 4      */
};

#define NUM_RECTS ((int)(sizeof(s_rects) / sizeof(s_rects[0])))

/* ~100ms per rect so each appearance is visible. */
#define RECT_DELAY_TICKS 20u

static unsigned char  s_chunky[STDOOM_CHUNKY_SIZE];
static unsigned char  s_map[256];
static unsigned char  s_saved_screen[STDOOM_PLANAR_SIZE];
static unsigned short s_saved_palette[16];
static int            s_blitter_present = -1;

/* ── STE blitter registers ($FFFF8A00 + offset; first $20 bytes are halftone RAM) ── */
#define BLT_BASE      0xFFFF8A00UL
#define BLT_SRC_INC_X (*(volatile unsigned short *)(BLT_BASE + 0x20))
#define BLT_SRC_INC_Y (*(volatile unsigned short *)(BLT_BASE + 0x22))
#define BLT_SRC_ADDR  (*(volatile unsigned long  *)(BLT_BASE + 0x24))
#define BLT_END_MASK1 (*(volatile unsigned short *)(BLT_BASE + 0x28))
#define BLT_END_MASK2 (*(volatile unsigned short *)(BLT_BASE + 0x2A))
#define BLT_END_MASK3 (*(volatile unsigned short *)(BLT_BASE + 0x2C))
#define BLT_DST_INC_X (*(volatile unsigned short *)(BLT_BASE + 0x2E))
#define BLT_DST_INC_Y (*(volatile unsigned short *)(BLT_BASE + 0x30))
#define BLT_DST_ADDR  (*(volatile unsigned long  *)(BLT_BASE + 0x32))
#define BLT_X_COUNT   (*(volatile unsigned short *)(BLT_BASE + 0x36))
#define BLT_Y_COUNT   (*(volatile unsigned short *)(BLT_BASE + 0x38))
#define BLT_HOP       (*(volatile unsigned char  *)(BLT_BASE + 0x3A))
#define BLT_OP        (*(volatile unsigned char  *)(BLT_BASE + 0x3B))
#define BLT_CTRL      (*(volatile unsigned char  *)(BLT_BASE + 0x3C))
#define BLT_CTRL_BUSY 0x80u
#define BLT_CTRL_HOG  0x40u
#define BLT_HOP_SRC   0x02u
#define BLT_OP_COPY   0x03u

static int blitter_present(void)
{
    if (s_blitter_present < 0)
        s_blitter_present = (Blitmode(-1) & 1) != 0;
    return s_blitter_present;
}

static void install_palette(const unsigned short *pal)
{
    long old_sp = Super(0L);
    volatile unsigned short *reg = (volatile unsigned short *)0xFF8240UL;
    short i;
    for (i = 0; i < 16; i++)
        *reg++ = *pal++;
    if (old_sp) Super((void *)old_sp);
}

static void save_palette(unsigned short *pal)
{
    long old_sp = Super(0L);
    volatile unsigned short *reg = (volatile unsigned short *)0xFF8240UL;
    short i;
    for (i = 0; i < 16; i++)
        *pal++ = *reg++;
    if (old_sp) Super((void *)old_sp);
}

static unsigned long timer_ticks(void)
{
    long old_sp = Super(0L);
    unsigned long t = *(volatile unsigned long *)0x4BAUL;
    if (old_sp) Super((void *)old_sp);
    return t;
}

static void delay_ticks(unsigned long n)
{
    unsigned long end = timer_ticks() + n;
    while (timer_ticks() < end) {}
}

/*
 * Copy one aligned sub-rect from planar slot 0 to the ST screen.
 * x and w must be multiples of 16 (plane-word aligned).
 * Uses STE blitter for multi-line rect if available; CPU row-by-row otherwise.
 */
static void copy_rect_to_screen(unsigned short x, unsigned short y,
                                 unsigned short w, unsigned short h,
                                 unsigned char *screen)
{
    unsigned short words_per_rect = (unsigned short)((w / 16u) * 4u);
    unsigned short skip_words     = (unsigned short)(PLANAR_WORDS_PER_ROW - words_per_rect);
    unsigned long  row_offset     = (unsigned long)(x / 16u) * 8u;
    unsigned long  src = STDOOM_PLANAR0_ADDR
                         + (unsigned long)y * PLANAR_ROW_BYTES + row_offset;
    unsigned long  dst = (unsigned long)screen
                         + (unsigned long)y * PLANAR_ROW_BYTES + row_offset;

    if (blitter_present() && skip_words == 0) {
        /* Linear blit: full-width rect only. skip_words==0 means the rect
         * spans the full 320px row width so source/dest are contiguous.
         * Blitter INC_Y!=0 (true sub-rects) is unreliable on ROM4 space. */
        unsigned short total_words = (unsigned short)((unsigned int)words_per_rect * h);
        long old_sp = Super(0L);
        short old_sr;

        __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr"
                         : "=d"(old_sr) : : "cc");

        BLT_SRC_INC_X = 2;
        BLT_SRC_INC_Y = 0;
        BLT_SRC_ADDR  = src;
        BLT_END_MASK1 = 0xFFFFu;
        BLT_END_MASK2 = 0xFFFFu;
        BLT_END_MASK3 = 0xFFFFu;
        BLT_DST_INC_X = 2;
        BLT_DST_INC_Y = 0;
        BLT_DST_ADDR  = dst;
        BLT_X_COUNT   = total_words;
        BLT_Y_COUNT   = 1;
        BLT_HOP  = BLT_HOP_SRC;
        BLT_OP   = BLT_OP_COPY;
        BLT_CTRL = (unsigned char)(BLT_CTRL_HOG | BLT_CTRL_BUSY);
        while (BLT_CTRL & BLT_CTRL_BUSY) {}

        __asm__ volatile("move.w %0,%%sr" : : "d"(old_sr) : "cc");
        if (old_sp) Super((void *)old_sp);
    } else {
        unsigned short row;
        unsigned short row_bytes = (unsigned short)(words_per_rect * 2u);
        for (row = 0; row < h; row++) {
            unsigned long off = (unsigned long)row * PLANAR_ROW_BYTES;
            memcpy((void *)(dst + off), (const void *)(src + off), row_bytes);
        }
    }
}

static void fill_grid_frame(unsigned char *frame)
{
    unsigned short x;
    unsigned short y;
    for (y = 0; y < STDOOM_FRAME_HEIGHT; y++) {
        for (x = 0; x < STDOOM_FRAME_WIDTH; x++) {
            frame[(unsigned long)y * STDOOM_FRAME_WIDTH + x] =
                (unsigned char)(((x / CELL_W) + (y / CELL_H) * GRID_COLS) % 16u);
        }
    }
}

static void fill_identity_map(unsigned char *map)
{
    int i;
    for (i = 0; i < 256; i++)
        map[i] = (unsigned char)(i & 0x0Fu);
}

static int blit_full_frame(const unsigned char *chunky, unsigned short *failed_y)
{
    unsigned short y;
    for (y = 0; y < STDOOM_FRAME_HEIGHT; y += 6) {
        unsigned short rows = STDOOM_FRAME_HEIGHT - y;
        if (rows > 6) rows = 6;
        if (sidecart_md_blit_rows(y, rows, STDOOM_FRAME_WIDTH, STDOOM_FRAME_WIDTH,
                                  chunky + (unsigned long)y * STDOOM_FRAME_WIDTH) != 0) {
            if (failed_y) *failed_y = y;
            return -1;
        }
    }
    return 0;
}

static void restore_display(unsigned char *screen)
{
    memcpy(screen, s_saved_screen, STDOOM_PLANAR_SIZE);
    install_palette(s_saved_palette);
}

int main(void)
{
    unsigned char *screen;
    long key;

    if (Getrez() != 0) {
        printf("RECTTEST requires low resolution.\n");
        return 1;
    }

    screen = (unsigned char *)Physbase();
    memcpy(s_saved_screen, screen, STDOOM_PLANAR_SIZE);
    save_palette(s_saved_palette);

    fill_grid_frame(s_chunky);
    fill_identity_map(s_map);

    printf("DOOM Accelerator rect C2P test\n");
    printf("Blitter: %d\n", blitter_present());
    printf("Grid: %ux%u cells of %ux%u px\n",
           GRID_COLS, GRID_ROWS, CELL_W, CELL_H);
    printf("%d rects tile the full 320x200\n", NUM_RECTS);
    printf("Press key to run; ESC to quit.\n\n");

    for (;;) {
        int stage    = 0;
        int ping_rc  = -2;
        int detected;
        unsigned char  ready = 0;
        unsigned long  seed  = 0;

        key = Cconin();
        if ((key & 0xFF) == 27) break;

        detected = sidecart_md_detect_verbose(&stage, &ready, &seed, &ping_rc);
        printf("stage=%d ping=%d det=%d\n", stage, ping_rc, detected);

        if (detected) {
            char version[64];
            int init_rc;
            int map_rc;
            int blit_rc;
            int c2p_rc;
            int i;
            int ok;
            unsigned short failed_y = 0;

            sidecart_md_result(version, sizeof(version));
            printf("DETECTED: %s\n", version);

            init_rc = sidecart_md_init(STDOOM_FRAME_WIDTH, STDOOM_FRAME_HEIGHT);
            printf("INIT rc=%d\n", init_rc);

            map_rc = (init_rc == 0) ? sidecart_md_set_map(s_map) : -1;
            printf("SET_MAP rc=%d\n", map_rc);

            blit_rc = (map_rc == 0) ? blit_full_frame(s_chunky, &failed_y) : -1;
            if (blit_rc != 0)
                printf("BLIT_ROWS failed at y=%u\n", (unsigned)failed_y);
            else
                printf("BLIT_ROWS ok\n");

            ok = (init_rc == 0 && map_rc == 0 && blit_rc == 0);
            if (!ok) {
                printf("Aborted. Press key to retry.\n\n");
                continue;
            }

            /* Clear screen to black, install test palette, then reveal rect by rect. */
            memset(screen, 0, STDOOM_PLANAR_SIZE);
            install_palette(s_test_palette);

            printf("Blitting %d rects...\n", NUM_RECTS);
            c2p_rc = 0;

            for (i = 0; i < NUM_RECTS; i++) {
                unsigned short rx = s_rects[i].x;
                unsigned short ry = s_rects[i].y;
                unsigned short rw = s_rects[i].w;
                unsigned short rh = s_rects[i].h;

                c2p_rc = sidecart_md_c2p_rect(rx, ry, rw, rh);
                if (c2p_rc != 0) {
                    printf("C2P_RECT #%d failed\n", i + 1);
                    break;
                }
                copy_rect_to_screen(rx, ry, rw, rh, screen);
                delay_ticks(RECT_DELAY_TICKS);
            }

            if (c2p_rc == 0)
                printf("All rects ok.\n");

            printf("Press key to restore; ESC=quit.\n");
            key = Cconin();
            restore_display(screen);
            if ((key & 0xFF) == 27) break;
        } else {
            printf("Not detected.\n");
        }

        printf("Press key to run again; ESC=quit.\n\n");
    }

    restore_display(screen);
    return 0;
}
