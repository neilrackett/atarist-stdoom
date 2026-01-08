#include <mint/osbind.h>
#include <stdint.h>
#include "atari_c2p.h"
#include "i_system.h"
#include "r_main.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomdef.h"

// Fixed greyscale palette indices for Atari ST lo-res.
const unsigned char subset_lorez[] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const unsigned char *subset;

// Keep in sync with st_stuff.c
#define ST_STARTREDPALS 1
#define ST_NUMREDPALS 8

static int st_palette_index = 0;

void set_st_palette_index(int index) {
    st_palette_index = index;
}

// Some function pointers depending on screen resolution.
void (*c2p_statusbar_drawfunc)(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
void (*c2p_screen_drawfunc)(unsigned char *out, const unsigned char *in);
void (*set_doom_palette)(const unsigned char *colors);
void (*install_palette)(const unsigned short *palette);
void (*save_palette)(unsigned short *palette);


// [DOOM color 0..255][ST color 0..15]
static unsigned char mix_weights_lorez[256][16];

static void build_greyscale_weights_lorez(const unsigned char *colors) {
    for (int i = 0; i < 256; i++) {
        const unsigned char *c = &colors[3 * i];
        int luma = (c[0] * 30 + c[1] * 59 + c[2] * 11 + 50) / 100;
        int level = luma / 17;
        if (level > 15) level = 15;
        unsigned char *weights = mix_weights_lorez[i];
        for (int j = 0; j < 16; j++) weights[j] = 0;
        weights[level] = 16;
    }
}

// C2P table for full resolution
// [phase 0..3][color 0..255][pixel 0..7]
static unsigned long c2p_table[4][256][8];

// C2P table for half resolution (2x2 pixels)
// [phase 0..3][color 0..255][pixel 0..3]
static unsigned long c2p_2x_table[4][256][4];

// C2P table for quarter resolution (4x4 pixels)
// [phase 0..3][color 0..255][pixel 0..1]
static unsigned long c2p_4x_table[4][256][2];

static unsigned short convert_channel(unsigned char v) {
    unsigned short r = (v & 0xe0) >> 5; // Bits 7,6,5 shifted to 2,1,0
    r |= (v & 0x10) >> 1; // STe color bit
    return r;
}
static unsigned short stcolor(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short entry = convert_channel(r);
    entry <<= 4;
    entry |= convert_channel(g);
    entry <<= 4;
    entry |= convert_channel(b);
    return entry;
}

void install_st_palette(const unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xff8240;
    for (short n=0; n<16; n++) *reg++ = *palette++;
}


void save_st_palette(unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xff8240;
    for (short n=0; n<16; n++) *palette++ = *reg++;
}


void set_st_doom_palette(const unsigned char *colors) {
    unsigned short stpalette[16];
    int tint = 0;
    if (colors) {
        if (st_palette_index >= ST_STARTREDPALS
            && st_palette_index < ST_STARTREDPALS + ST_NUMREDPALS) {
            int level = st_palette_index - ST_STARTREDPALS + 1;
            tint = (level * 128) / ST_NUMREDPALS;
        }
    }
    for (int i = 0; i < 16; i++) {
        unsigned char grey = (unsigned char)((i * 255) / 15);
        int r = grey;
        int g = grey;
        int b = grey;
        if (tint > 0) {
            r = grey + (tint * (255 - grey)) / 128;
            g = grey - (tint * grey) / 256;
            b = grey - (tint * grey) / 256;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
        }
        stpalette[i] = stcolor((unsigned char)r, (unsigned char)g, (unsigned char)b);
    }
    install_st_palette(stpalette);
}


static unsigned char weights_to_index(const unsigned char *weights, short numcolors) {
    unsigned char best = 0;
    unsigned char best_weight = weights[0];
    for (short i = 1; i < numcolors; i++) {
        if (weights[i] > best_weight) {
            best_weight = weights[i];
            best = (unsigned char)i;
        }
    }
    return best;
}

static unsigned long lorez_pixel_pdata(unsigned char color, short px) {
    unsigned long pdata = 0;
    if (color & 1) pdata |= 0x01000000;
    if (color & 2) pdata |= 0x00010000;
    if (color & 4) pdata |= 0x00000100;
    if (color & 8) pdata |= 0x00000001;
    return pdata << (7 - px);
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]);
static void c2p_screen_lorez(unsigned char *out, const unsigned char *in);
static void c2p_statusbar_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);




void init_c2p_table() {
    short res = Getrez();
    if (res != 0) {
        I_Error("Unsupported resolution %d\n", res);
    }
    // Low Resolution, 16 colors
    subset = subset_lorez;
    c2p_screen_drawfunc = c2p_screen_lorez;
    c2p_statusbar_drawfunc = c2p_statusbar_lorez;
    install_palette = install_st_palette;
    save_palette = save_st_palette;
    set_doom_palette = set_st_doom_palette;
    build_greyscale_weights_lorez(W_CacheLumpName("PLAYPAL", PU_CACHE));
    for (int i=0; i<256; i++) {
        unsigned char *weights = mix_weights_lorez[i];
        unsigned char color = weights_to_index(weights, 16);
        for (int phase=0; phase<4; phase++) {
            // Fill 1x1 c2p table
            for (int px=0; px<8; px++) {
                c2p_table[phase][i][px] = lorez_pixel_pdata(color, px);
            }
            // Fill 2x2 c2p table
            for (int ipx=0; ipx<4; ipx++) {
                unsigned long ipx_pdata = 0;
                for (int opx=2*ipx; opx<2*ipx+2; opx++) {
                    ipx_pdata |= lorez_pixel_pdata(color, opx);
                }
                c2p_2x_table[phase][i][ipx] = ipx_pdata;
            }
            // Fill 4x4 c2p table
            for (int ipx=0; ipx<2; ipx++) {
                unsigned long ipx_pdata = 0;
                for (int opx=4*ipx; opx<4*ipx+4; opx++) {
                    ipx_pdata |= lorez_pixel_pdata(color, opx);
                }
                c2p_4x_table[phase][i][ipx] = ipx_pdata;
            }
        }
    }
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]) {
    if (pixels < 16) return;
    unsigned short groups = pixels / 16 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned long mask = 0x00ff00ff<<5; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read eight consecutive pixels from buffer into two 32 bit registers
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"

        // Prepare pixels 1, 3
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 3
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 1
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"

        // Prepare pixels 0, 2
        "lsr.l      #3,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"

        // Pixel 2
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"

        // Pixel 0
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"

        // Prepare pixels 5,7
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 7
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 5
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"

        // Prepare pixels 4, 6
        "lsr.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"

        // Pixel 6
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"

        // Pixel 4
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata], 0(%[out])             \n\t"

        // Read another eight consecutive pixels from buffer into two 32 bit registers
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"

        // Prepare pixels 1, 3
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 3
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 1
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"

        // Prepare pixels 0, 2
        "lsr.l      #3,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"

        // Pixel 2
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"

        // Pixel 0
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"

        // Prepare pixels 5,7
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 7
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 5
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"

        // Prepare pixels 4, 6
        "lsr.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"

        // Pixel 6
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"

        // Pixel 4
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata], 1(%[out])             \n\t"
        
        // Advance out address by 16 pixels (8 bytes) and loop
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"

        // Outputs
        : [out] "+a" (out)
        , [in] "+a" (in)
        , [pdata] "+d" (pdata)
        , [groups] "+d" (groups)
        
        // Inputs
        : [table] "a" (table)
        , [mask] "d" (mask)
        
        // Clobbers
        : "d0", "d1", "d2", "memory"
    );
}

static void c2p_statusbar_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    out += y_begin * 160 + x_begin / 2;
    in += y_begin * 320 + x_begin;
    for (int line = y_begin; line < y_end; line++ ) {
        c2p_1x_lorez(out, in, x_end - x_begin, c2p_table[line&3]);
        out += 160;
        in += 320;
    }
}



static void c2p_2x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][4]) {
    if (pixels < 8) return;
    short groups = pixels / 8 - 1;
    unsigned long pdata; // 32 bits of planar pixel data
    unsigned long p0, p1; // Helper variables for holding pixel data read from input
    unsigned long mask = 0x00ff00ff<<4; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read four consecutive pixels from buffer into a 32 bit register
        "move.l     (%[in])+,%[p0]                  \n\t"

        // Prepare pixels 1, 3
        "move.l     %[p0],%[p1]                     \n\t"
        "lsl.l      #4,%[p1]                        \n\t"
        "and.l      %[mask],%[p1]                   \n\t"

        // Pixel 3
        "move.l     12(%[table],%[p1].w), %[pdata]  \n\t"

        // Pixel 1
        "swap       %[p1]                           \n\t"
        "or.l       4(%[table],%[p1].w), %[pdata]   \n\t"

        // Prepare pixels 0, 2
        "lsr.l      #4,%[p0]                        \n\t"
        "and.l      %[mask],%[p0]                   \n\t"

        // Pixel 2
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"

        // Pixel 0
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata], 0(%[out])             \n\t"

        // Read four consecutive pixels from buffer into a 32 bit register
        "move.l     (%[in])+,%[p0]                  \n\t"

        // Prepare pixels 5,7
        "move.l     %[p0],%[p1]                     \n\t"
        "lsl.l      #4,%[p1]                        \n\t"
        "and.l      %[mask],%[p1]                   \n\t"

        // Pixel 7
        "move.l     12(%[table],%[p1].w), %[pdata]  \n\t"

        // Pixel 5
        "swap       %[p1]                           \n\t"
        "or.l       4(%[table],%[p1].w), %[pdata]   \n\t"

        // Prepare pixels 4, 6
        "lsr.l      #4,%[p0]                        \n\t"
        "and.l      %[mask],%[p0]                   \n\t"

        // Pixel 6
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"

        // Pixel 4
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata], 1(%[out])             \n\t"

        // Advance out address by 16 pixels (8 bytes) and loop
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"

        // Outputs
        : [out] "+&a" (out)
        , [in] "+&a" (in)
        , [pdata] "=&d" (pdata)
        , [groups] "+&d" (groups)
        , [p0] "=&d" (p0)
        , [p1] "=&d" (p1)
        
        // Inputs
        : [table] "a" (table)
        , [mask] "d" (mask)
        
        // Clobbers
        : "memory", "cc"
    );
}



static void c2p_4x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][2]) {
    if (pixels < 4) return;
    short groups = pixels / 4 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned long mask = 0xff00ff<<3; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read four consecutive pixels from buffer into one 32 register
        "move.l     (%[in])+, %%d0                  \n\t"
        "move.l     %%d0,%%d1                       \n\t"

        // Prepare d0 for reading pixels 0 and 2
        "lsr.l      #5,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"

        // Prepare d1 for reading pixels 1 and 3
        "lsl.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"

        // Pixel 2
        "move.l     0(%[table],%%d0.w),%[pdata]     \n\t"

        // Pixel 3
        "or.l       4(%[table],%%d1.w),%[pdata]     \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata],1(%[out])              \n\t"
        "movep.l    %[pdata],321(%[out])            \n\t"

        // Move pixels 0 and 1 into lower word positions of d0 and d1
        "swap       %%d0                            \n\t"
        "swap       %%d1                            \n\t"

        // Pixel 0
        "move.l     (%[table],%%d0.w),%[pdata]      \n\t"

        // Pixel 1
        "or.l       4(%[table],%%d1.w),%[pdata]     \n\t"

        // Write these pixels into ST screen buffer
        "movep.l    %[pdata],0(%[out])              \n\t"
        "movep.l    %[pdata],320(%[out])            \n\t"

        // Advance out address by 16 pixels (8 bytes) and loop
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"

        // Outputs
        : [out] "+a" (out)
        , [in] "+a" (in)
        , [pdata] "+d" (pdata)
        , [groups] "+d" (groups)
        
        // Inputs
        : [table] "a" (table)
        , [mask] "d" (mask)
        
        // Clobbers
        : "d0", "d1", "d2", "memory"
    );
}




void draw_palette_table(unsigned char *st_screen) {
    set_doom_palette(W_CacheLumpName("PLAYPAL", PU_CACHE));
    short res = Getrez();
    if (res != 0) {
        I_Error("Unsupported resolution %d\n", res);
    }
    unsigned char buf[128+16];
    unsigned char c = 0;
    for (int y=0; y<128; y+=8) {
        for (int x=0; x<128; x+=8) {
            for (int i=0; i<8; i++) buf[x+i] = c;
            for (int i=0; i<16; i++) {
                // Draw highlight around color if from palette
                if (c == subset[i]) {
                    buf[x] = 4;
                    buf[x+7] = 0;
                }
            }
            c++;
        }
        for (int x=128; x<128+8; x++) buf[x] = 0;
        for (int x=128+8; x<128+16; x++) buf[x] = subset[y/8];
        for (int i=0; i<8; i++) c2p_1x_lorez(st_screen + 160*(32+y+i), buf, 128+16, c2p_table[i%4]);
    }
}

extern boolean inhelpscreens;
extern boolean menuactive;
extern boolean automapactive;
extern gamestate_t gamestate;

static void c2p_screen_lorez(unsigned char *out, const unsigned char *in) {
    boolean zoom_allowed = gamestate == GS_LEVEL
        && !menuactive && !inhelpscreens && !automapactive;
    short splitline = !zoom_allowed || viewheight == SCREENHEIGHT ? SCREENHEIGHT : SCREENHEIGHT - 32;
    if (!zoom_allowed || viewwidth > SCREENWIDTH/2) {
        for (short line = 0; line < splitline; line++ ) {
            c2p_1x_lorez(out + 160*line, in + 320*line, 320, c2p_table[line&3]);
        }
    } else if(viewwidth > SCREENWIDTH/4) {
        // 2x zoom
        for (short line = 0; line < splitline; line++ ) {
            c2p_2x_lorez(out + 160*line, in + SCREENWIDTH*(42 + line/2) + 80, 160, c2p_2x_table[line&3]);
        }
    } else {
        // 4x zoom
        for (short line = 0; line < splitline; line++ ) {
            short phase = line & 3;
            if (phase < 2) {
                c2p_4x_lorez(out + 160*line, in + SCREENWIDTH*(63 + line/4) + 120, 80, c2p_4x_table[phase]);
            }
        }
    }
}

void c2p_screen(unsigned char *out, const unsigned char *in) {
    c2p_screen_drawfunc(out, in);
}

void c2p_statusbar(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    if (y_end <= 0 || x_end <= 0)
        return;
    boolean statusbar_drawn = gamestate == GS_LEVEL
        && !menuactive && !inhelpscreens && !automapactive && viewheight < SCREENHEIGHT;
    if (!statusbar_drawn)
        return;
    if (y_begin < SCREENHEIGHT - 32)
        y_begin = SCREENHEIGHT - 32;
    if (y_end > SCREENHEIGHT)
        y_end = SCREENHEIGHT;
    if (x_begin < 0)
        x_begin = 0;
    if (x_end > SCREENWIDTH)
        x_end = SCREENWIDTH;

    x_begin &= ~15;
    x_end = (x_end + 15) & ~15;
    //fprintf(stderr, "%d %d %d %d \r", y_begin, y_end, x_begin, x_end);

    c2p_statusbar_drawfunc(out, in, y_begin, y_end, x_begin, x_end);
}
