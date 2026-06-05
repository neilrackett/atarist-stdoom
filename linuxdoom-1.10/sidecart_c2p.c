/*
 * sidecart_c2p.c — SidecarTridge-accelerated C2P for STDOOM.
 *
 * Holds everything sidecart-specific that used to be interleaved into
 * atari_c2p.c: the 256->16 reduction map, the RP2040 C2P offload screen
 * drawfunc, the planar->screen blitter copy, and the palette hook. When the
 * accelerator is detected, sidecart_c2p_install() overrides the software
 * function pointers (declared in atari_c2p.h) with the accelerated versions.
 *
 * atari_c2p.c is the pure software fallback and knows nothing about the
 * sidecart; this mirrors how a clean SDL XBIOS driver swaps implementations.
 */

#include <mint/osbind.h>
#include <stdio.h>

#include "doomdef.h"
#include "atari_c2p.h"
#include "sidecart_c2p.h"
#include "sidecart_md.h"

/* STDOOM Accelerator active flag (1 = SidecarTridge firmware detected). */
int c2p_md_active = 0;

/*
 * STE blitter registers (fast copy path from ROM4 slot 0 to ST screen RAM).
 *
 * The first 16 words at BLT_BASE ($FFFF8A00) are the halftone RAM; the actual
 * registers start at +$20. The earlier +$02..+$1E offsets were wrong (they
 * wrote into halftone RAM, so the blitter control register was never touched
 * and the blit silently did nothing). Offsets below match the Atari STE
 * hardware reference and md-sprites-demo's main.s.
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

static unsigned char s_doom8_to_st4[256];
static void (*s_sw_set_doom_palette)(const unsigned char *colors);
static int s_blitter_present = -1;
/* Set when c2p_screen_md offloaded a full frame (which already carries the
 * status bar), so the following c2p_statusbar_md call can skip its work. */
static int c2p_md_frame_offloaded = 0;

static int blitter_present(void) {
    if (s_blitter_present < 0) {
        long mode = Blitmode(-1);
        s_blitter_present = (mode & 1) != 0;
    }
    return s_blitter_present;
}

/* Build the 256->16 nearest-ST-colour reduction map from the lorez dithering
 * weights (the dominant weight in each row is the nearest ST colour). */
static void rebuild_doom8_to_st4_map(void) {
    for (int i = 0; i < 256; i++) {
        unsigned char best_idx = 0;
        unsigned char best_weight = 0;
        const unsigned char *weights = mix_weights_lorez[i];

        for (int j = 0; j < 16; j++) {
            if (weights[j] > best_weight) {
                best_weight = weights[j];
                best_idx = (unsigned char)j;
            }
        }
        s_doom8_to_st4[i] = best_idx;
    }
}

/* Disable the accelerator and restore the software function pointers. */
static void sidecart_c2p_fallback(void) {
    c2p_md_active = 0;
    c2p_screen_drawfunc = c2p_screen_lorez;
    c2p_statusbar_drawfunc = c2p_statusbar_lorez;
    set_doom_palette = s_sw_set_doom_palette ? s_sw_set_doom_palette
                                             : set_st_doom_palette;
}

/* Palette hook: run the software palette setup, then push the rebuilt
 * reduction map to the accelerator. */
static void set_st_doom_palette_md(const unsigned char *colors) {
    if (s_sw_set_doom_palette) {
        s_sw_set_doom_palette(colors);
    } else {
        set_st_doom_palette(colors);
    }

    if (c2p_md_active) {
        if (sidecart_md_set_map(s_doom8_to_st4) != 0) {
            printf("MD SET_MAP failed; SW C2P\n");
            sidecart_c2p_fallback();
        }
    }
}

static void sidecart_md_copy_planar_to_screen(unsigned long src_addr,
                                              unsigned char *dst) {
    if (blitter_present()) {
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
        return;
    }

    const unsigned long *src = (const unsigned long *)src_addr;
    unsigned long *d = (unsigned long *)dst;
    unsigned long words = STDOOM_PLANAR_SIZE / 4;

    while (words--) {
        *d++ = *src++;
    }
}

/* Accelerated full-screen drawfunc. Uploads the chunky frame to the RP2040,
 * triggers C2P, and blits the planar result to the ST screen. Routes all
 * screens (splash, menus, intermission, automap, gameplay) through the
 * accelerator; falls back to software if any sidecart command fails. */
static void c2p_screen_md(unsigned char *out, const unsigned char *in) {
    c2p_md_frame_offloaded = 0;

    if (!c2p_md_active) {
        c2p_screen_lorez(out, in);
        return;
    }

    for (short y = 0; y < SCREENHEIGHT; y += 6) {
        short rows = SCREENHEIGHT - y;
        if (rows > 6) {
            rows = 6;
        }
        if (sidecart_md_blit_rows((unsigned short)y, (unsigned short)rows,
                                  STDOOM_FRAME_WIDTH, STDOOM_FRAME_WIDTH,
                                  in + (y * SCREENWIDTH)) != 0) {
            c2p_screen_lorez(out, in);
            return;
        }
    }

    if (sidecart_md_c2p() != 0) {
        c2p_screen_lorez(out, in);
        return;
    }

    sidecart_md_copy_planar_to_screen((unsigned long)STDOOM_PLANAR0_ADDR, out);
    c2p_md_frame_offloaded = 1;
}

/* Accelerated status-bar drawfunc. When the preceding c2p_screen_md offloaded
 * a full frame, the status bar is already on screen, so skip. Otherwise fall
 * back to the software status-bar path. */
static void c2p_statusbar_md(unsigned char *out, const unsigned char *in,
                             short y_begin, short y_end,
                             short x_begin, short x_end) {
    if (c2p_md_frame_offloaded) {
        c2p_md_frame_offloaded = 0;
        return;
    }
    c2p_statusbar_lorez(out, in, y_begin, y_end, x_begin, x_end);
}

/* Detect a SidecarTridge Multi-device running the STDOOM Accelerator firmware
 * and prime the single-slot C2P path. Synchronous (Milestone 2). */
void sidecart_c2p_init(void) {
    int stage = 0, ping_rc = -2;
    unsigned char ready = 0;
    unsigned long seed = 0;

    rebuild_doom8_to_st4_map();
    c2p_md_active = sidecart_md_detect_verbose(&stage, &ready, &seed, &ping_rc);

    /* TEMPORARY diagnostics (40-col friendly). */
    printf("MD ready=$%02X(want $%02X) seed=$%08lX\n",
           (unsigned)ready, (unsigned)STDOOM_READY_MAGIC,
           (unsigned long)seed);
    printf("MD stage=%d ping=%d\n", stage, ping_rc);
    /* RP-side counters: irq=ROM3 accesses seen, cmd=commands parsed,
     * ce=checksum errors, lc=last command id (non-zero = RP is active). */
    printf("MD irq=%lX cmd=%lX ce=%lX lc=%lX\n",
           (unsigned long)*STDOOM_DBG_ROM3_IRQ_ADDR,
           (unsigned long)*STDOOM_DBG_CMD_ADDR,
           (unsigned long)*STDOOM_DBG_CHK_ERR_ADDR,
           (unsigned long)*STDOOM_DBG_LAST_CMD_ADDR);

    if (c2p_md_active) {
        char version[64];
        int init_rc;
        int map_rc;

        sidecart_md_result(version, sizeof(version));
        init_rc = sidecart_md_init(STDOOM_FRAME_WIDTH, STDOOM_FRAME_HEIGHT);
        map_rc = (init_rc == 0) ? sidecart_md_set_map(s_doom8_to_st4) : -1;
        if (init_rc != 0 || map_rc != 0) {
            c2p_md_active = 0;
            printf("MD detected: %s\n", version);
            printf("MD C2P init failed; SW C2P\n");
        } else {
            printf("MD detected: %s\n", version);
        }
    } else {
        printf("MD not detected; SW C2P\n");
    }
}

/* Override the software function pointers (set by init_c2p_table) with the
 * accelerated versions. No-op unless the accelerator is present and the screen
 * is low-res (the only resolution the accelerator path supports). */
void sidecart_c2p_install(void) {
    if (!c2p_md_active) {
        return;
    }
    if (Getrez() != 0) {
        return;
    }

    /* init_c2p_table just set set_doom_palette to the software lorez palette;
     * capture it so the md palette hook and the fallback can defer to it. */
    s_sw_set_doom_palette = set_doom_palette;

    c2p_screen_drawfunc = c2p_screen_md;
    c2p_statusbar_drawfunc = c2p_statusbar_md;
    set_doom_palette = set_st_doom_palette_md;
}
