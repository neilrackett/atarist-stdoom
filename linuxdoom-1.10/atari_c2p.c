#include <mint/osbind.h>
#include <stdint.h>
#include "atari_c2p.h"
#include "i_system.h"
#include "r_main.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomdef.h"

/* Some environments don't define u_int32_t, but do define uint32_t. */
typedef uint32_t u_int32_t;

// Fixed greyscale palette indices for Atari ST lo-res.
const unsigned char subset_lorez[] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const unsigned char subset_midrez[] =
    {0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const unsigned char subset_hirez[] = 
    {0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const unsigned char *subset;

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
        int rem = luma % 17;
        int w_hi = 0;
        if (level >= 15) {
            level = 15;
            rem = 0;
        } else {
            w_hi = (rem * 16 + 8) / 17;
        }
        int w_lo = 16 - w_hi;
        unsigned char *weights = mix_weights_lorez[i];
        for (int j = 0; j < 16; j++) weights[j] = 0;
        weights[level] = (unsigned char)w_lo;
        if (w_hi && level < 15) {
            weights[level + 1] = (unsigned char)w_hi;
        }
    }
}

// [DOOM color 0..255][ST color 0..3]
static unsigned char mix_weights_midrez[256][4];

static void build_greyscale_weights_midrez(const unsigned char *colors) {
    for (int i = 0; i < 256; i++) {
        const unsigned char *c = &colors[3 * i];
        int luma = (c[0] * 30 + c[1] * 59 + c[2] * 11 + 50) / 100;
        int level = luma / 85;
        int rem = luma % 85;
        int w_hi = 0;
        if (level >= 3) {
            level = 3;
            rem = 0;
        } else {
            w_hi = (rem * 16 + 42) / 85;
        }
        int w_lo = 16 - w_hi;
        unsigned char *weights = mix_weights_midrez[i];
        for (int j = 0; j < 4; j++) weights[j] = 0;
        weights[level] = (unsigned char)w_lo;
        if (w_hi && level < 3) {
            weights[level + 1] = (unsigned char)w_hi;
        }
    }
}

static unsigned char mix_weights_hirez[256][2] = {
{ 16, 0,}, // 0: 0.000000
{ 16, 0,}, // 1: 3.162877
{ 16, 0,}, // 2: 1.972921
{ 13, 3,}, // 3: 3.023968
{ 0, 16,}, // 4: 0.000000
{ 15, 1,}, // 5: 2.532063
{ 16, 0,}, // 6: 2.244976
{ 16, 0,}, // 7: 1.103170
{ 16, 0,}, // 8: 0.612999
{ 14, 2,}, // 9: 2.950513
{ 15, 1,}, // 10: 3.628078
{ 15, 1,}, // 11: 2.962386
{ 16, 0,}, // 12: 2.260143
{ 13, 3,}, // 13: 4.352364
{ 14, 2,}, // 14: 3.923292
{ 14, 2,}, // 15: 3.834948
{ 4, 12,}, // 16: 9.109753
{ 5, 11,}, // 17: 9.462293
{ 5, 11,}, // 18: 9.766668
{ 6, 10,}, // 19: 9.968747
{ 7, 9,}, // 20: 10.500564
{ 7, 9,}, // 21: 10.161597
{ 8, 8,}, // 22: 10.842706
{ 8, 8,}, // 23: 10.716986
{ 9, 7,}, // 24: 10.545033
{ 9, 7,}, // 25: 10.821122
{ 10, 6,}, // 26: 10.660322
{ 10, 6,}, // 27: 10.538860
{ 11, 5,}, // 28: 10.584070
{ 11, 5,}, // 29: 10.591230
{ 11, 5,}, // 30: 10.575039
{ 12, 4,}, // 31: 10.432224
{ 12, 4,}, // 32: 10.136872
{ 12, 4,}, // 33: 10.134342
{ 13, 3,}, // 34: 9.705795
{ 13, 3,}, // 35: 9.699158
{ 13, 3,}, // 36: 9.314177
{ 13, 3,}, // 37: 9.354319
{ 14, 2,}, // 38: 8.745334
{ 14, 2,}, // 39: 8.538713
{ 14, 2,}, // 40: 8.107565
{ 14, 2,}, // 41: 8.077693
{ 15, 1,}, // 42: 7.717675
{ 15, 1,}, // 43: 7.339993
{ 15, 1,}, // 44: 6.622275
{ 16, 0,}, // 45: 6.227608
{ 16, 0,}, // 46: 5.420531
{ 16, 0,}, // 47: 5.026935
{ 1, 15,}, // 48: 4.700754
{ 2, 14,}, // 49: 5.661571
{ 2, 14,}, // 50: 6.486069
{ 3, 13,}, // 51: 7.524214
{ 3, 13,}, // 52: 7.910698
{ 3, 13,}, // 53: 8.952188
{ 4, 12,}, // 54: 9.728190
{ 4, 12,}, // 55: 10.216728
{ 5, 11,}, // 56: 11.440166
{ 5, 11,}, // 57: 11.134138
{ 6, 10,}, // 58: 11.076385
{ 6, 10,}, // 59: 10.948090
{ 7, 9,}, // 60: 10.764825
{ 7, 9,}, // 61: 10.778378
{ 8, 8,}, // 62: 10.479200
{ 8, 8,}, // 63: 10.468348
{ 9, 7,}, // 64: 9.695905
{ 9, 7,}, // 65: 9.070922
{ 10, 6,}, // 66: 8.700906
{ 10, 6,}, // 67: 8.142102
{ 10, 6,}, // 68: 8.179445
{ 11, 5,}, // 69: 7.165587
{ 11, 5,}, // 70: 7.101255
{ 12, 4,}, // 71: 6.687458
{ 12, 4,}, // 72: 6.140609
{ 13, 3,}, // 73: 6.086236
{ 13, 3,}, // 74: 4.998869
{ 13, 3,}, // 75: 4.571978
{ 14, 2,}, // 76: 4.566504
{ 14, 2,}, // 77: 3.736165
{ 14, 2,}, // 78: 4.061320
{ 15, 1,}, // 79: 3.359486
{ 1, 15,}, // 80: 3.154431
{ 2, 14,}, // 81: 2.270905
{ 3, 13,}, // 82: 3.742738
{ 3, 13,}, // 83: 2.498833
{ 3, 13,}, // 84: 3.948201
{ 4, 12,}, // 85: 2.407922
{ 4, 12,}, // 86: 3.616925
{ 5, 11,}, // 87: 2.033330
{ 6, 10,}, // 88: 3.559891
{ 6, 10,}, // 89: 2.388310
{ 6, 10,}, // 90: 3.910947
{ 7, 9,}, // 91: 2.900738
{ 7, 9,}, // 92: 3.349602
{ 8, 8,}, // 93: 2.373181
{ 8, 8,}, // 94: 2.711676
{ 9, 7,}, // 95: 3.064542
{ 9, 7,}, // 96: 3.060956
{ 10, 6,}, // 97: 3.840060
{ 10, 6,}, // 98: 2.226325
{ 11, 5,}, // 99: 3.688077
{ 11, 5,}, // 100: 2.681853
{ 11, 5,}, // 101: 3.276316
{ 12, 4,}, // 102: 2.752042
{ 12, 4,}, // 103: 2.174505
{ 13, 3,}, // 104: 3.930553
{ 13, 3,}, // 105: 2.131779
{ 13, 3,}, // 106: 2.725150
{ 14, 2,}, // 107: 3.527215
{ 14, 2,}, // 108: 2.698863
{ 14, 2,}, // 109: 2.882897
{ 15, 1,}, // 110: 3.554518
{ 15, 1,}, // 111: 2.826927
{ 4, 12,}, // 112: 14.138620
{ 5, 11,}, // 113: 13.178967
{ 6, 10,}, // 114: 12.242190
{ 7, 9,}, // 115: 11.329672
{ 8, 8,}, // 116: 10.247314
{ 9, 7,}, // 117: 9.438586
{ 10, 6,}, // 118: 8.713874
{ 10, 6,}, // 119: 8.222451
{ 11, 5,}, // 120: 7.135536
{ 12, 4,}, // 121: 6.360008
{ 13, 3,}, // 122: 5.951371
{ 13, 3,}, // 123: 5.143507
{ 14, 2,}, // 124: 4.200910
{ 15, 1,}, // 125: 4.289530
{ 15, 1,}, // 126: 2.953075
{ 16, 0,}, // 127: 2.164012
{ 6, 10,}, // 128: 5.623372
{ 7, 9,}, // 129: 5.132666
{ 8, 8,}, // 130: 5.511966
{ 8, 8,}, // 131: 5.082297
{ 9, 7,}, // 132: 5.237485
{ 9, 7,}, // 133: 5.121642
{ 9, 7,}, // 134: 5.493481
{ 10, 6,}, // 135: 4.845304
{ 10, 6,}, // 136: 5.292644
{ 11, 5,}, // 137: 4.566669
{ 11, 5,}, // 138: 4.724578
{ 12, 4,}, // 139: 4.687091
{ 12, 4,}, // 140: 4.157088
{ 13, 3,}, // 141: 4.846282
{ 13, 3,}, // 142: 3.897707
{ 13, 3,}, // 143: 3.956564
{ 9, 7,}, // 144: 5.641809
{ 10, 6,}, // 145: 5.458966
{ 11, 5,}, // 146: 5.537491
{ 11, 5,}, // 147: 5.270129
{ 12, 4,}, // 148: 4.649210
{ 13, 3,}, // 149: 4.487115
{ 13, 3,}, // 150: 4.657611
{ 14, 2,}, // 151: 3.779618
{ 10, 6,}, // 152: 3.635238
{ 11, 5,}, // 153: 4.154866
{ 11, 5,}, // 154: 3.430387
{ 12, 4,}, // 155: 3.887741
{ 12, 4,}, // 156: 3.737777
{ 13, 3,}, // 157: 3.395479
{ 13, 3,}, // 158: 3.548160
{ 14, 2,}, // 159: 3.637814
{ 0, 16,}, // 160: 10.628353
{ 4, 12,}, // 161: 11.262287
{ 6, 10,}, // 162: 10.775217
{ 8, 8,}, // 163: 10.476766
{ 9, 7,}, // 164: 9.909468
{ 11, 5,}, // 165: 9.357160
{ 12, 4,}, // 166: 8.779848
{ 13, 3,}, // 167: 8.150807
{ 0, 16,}, // 168: 0.000000
{ 2, 14,}, // 169: 5.630848
{ 4, 12,}, // 170: 8.818243
{ 5, 11,}, // 171: 11.774660
{ 7, 9,}, // 172: 14.472336
{ 8, 8,}, // 173: 16.744305
{ 9, 7,}, // 174: 19.187151
{ 10, 6,}, // 175: 21.260931
{ 11, 5,}, // 176: 22.539795
{ 12, 4,}, // 177: 20.917423
{ 12, 4,}, // 178: 19.625549
{ 12, 4,}, // 179: 18.424139
{ 12, 4,}, // 180: 17.326004
{ 13, 3,}, // 181: 16.103060
{ 13, 3,}, // 182: 14.913567
{ 13, 3,}, // 183: 13.833478
{ 13, 3,}, // 184: 12.882592
{ 14, 2,}, // 185: 11.307191
{ 14, 2,}, // 186: 10.253874
{ 14, 2,}, // 187: 9.347155
{ 15, 1,}, // 188: 8.555857
{ 15, 1,}, // 189: 7.435950
{ 16, 0,}, // 190: 6.227608
{ 16, 0,}, // 191: 5.026935
{ 2, 14,}, // 192: 4.067422
{ 4, 12,}, // 193: 5.951301
{ 6, 10,}, // 194: 7.883518
{ 7, 9,}, // 195: 9.822075
{ 9, 7,}, // 196: 11.271643
{ 11, 5,}, // 197: 13.057537
{ 12, 4,}, // 198: 14.514785
{ 13, 3,}, // 199: 15.818985
{ 16, 0,}, // 200: 16.481758
{ 16, 0,}, // 201: 14.168860
{ 16, 0,}, // 202: 12.253104
{ 16, 0,}, // 203: 10.404237
{ 16, 0,}, // 204: 8.628445
{ 16, 0,}, // 205: 6.933524
{ 16, 0,}, // 206: 5.329669
{ 16, 0,}, // 207: 3.830918
{ 0, 16,}, // 208: 0.000000
{ 1, 15,}, // 209: 4.941270
{ 2, 14,}, // 210: 7.504269
{ 3, 13,}, // 211: 9.569269
{ 5, 11,}, // 212: 11.725086
{ 5, 11,}, // 213: 13.692756
{ 7, 9,}, // 214: 15.626595
{ 7, 9,}, // 215: 17.146545
{ 8, 8,}, // 216: 16.473553
{ 8, 8,}, // 217: 16.144873
{ 9, 7,}, // 218: 15.162672
{ 10, 6,}, // 219: 14.916658
{ 10, 6,}, // 220: 14.026222
{ 11, 5,}, // 221: 13.782843
{ 11, 5,}, // 222: 12.858162
{ 11, 5,}, // 223: 12.400164
{ 0, 16,}, // 224: 0.000000
{ 0, 16,}, // 225: 3.278798
{ 0, 16,}, // 226: 6.077520
{ 0, 16,}, // 227: 8.711452
{ 0, 16,}, // 228: 11.152089
{ 0, 16,}, // 229: 13.354692
{ 0, 16,}, // 230: 15.234977
{ 0, 16,}, // 231: 16.481758
{ 12, 4,}, // 232: 11.759201
{ 12, 4,}, // 233: 11.289021
{ 13, 3,}, // 234: 10.645083
{ 13, 3,}, // 235: 9.944007
{ 13, 3,}, // 236: 4.505016
{ 14, 2,}, // 237: 3.818308
{ 15, 1,}, // 238: 4.303171
{ 15, 1,}, // 239: 3.484603
{ 16, 0,}, // 240: 3.830918
{ 16, 0,}, // 241: 3.127067
{ 16, 0,}, // 242: 2.458153
{ 16, 0,}, // 243: 1.829062
{ 16, 0,}, // 244: 1.246780
{ 16, 0,}, // 245: 0.722350
{ 16, 0,}, // 246: 0.276893
{ 16, 0,}, // 247: 0.000000
{ 6, 10,}, // 248: 14.617802
{ 3, 13,}, // 249: 13.213470
{ 6, 10,}, // 250: 13.676251
{ 9, 7,}, // 251: 21.175062
{ 11, 5,}, // 252: 16.536200
{ 12, 4,}, // 253: 12.386811
{ 14, 2,}, // 254: 8.435212
{ 10, 6,}, // 255: 7.343525
};

// C2P table for full resolution
// [phase 0..3][color 0..255][pixel 0..7]
static unsigned long c2p_table[4][256][8];

// C2P table for half resolution (2x2 pixels)
// [phase 0..3][color 0..255][pixel 0..3]
static unsigned long c2p_2x_table[4][256][4];

// C2P table for quarter resolution (4x4 pixels)
// [phase 0..3][color 0..255][pixel 0..1]
static unsigned long c2p_4x_table[4][256][2];

// Small table for C2P on the Atari TT where the table should fit in cache.
// Converts 4 bits of a chunky pixel into a planar representation that can
// be written into planar video memory using movep. Sets bits 0, 8, 16 and 24.
static uint32_t c2p_tt_table[16];

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
static unsigned short ttcolor(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short entry = r >> 4;
    entry <<= 4;
    entry |= g >> 4;
    entry <<= 4;
    entry |= b >> 4;
    return entry;
}

void install_st_palette(const unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xff8240;
    for (short n=0; n<16; n++) *reg++ = *palette++;
}

void install_tt_palette(const unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xffff8400;
    for (short n=0; n<256; n++) *reg++ = *palette++;
}

void save_st_palette(unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xff8240;
    for (short n=0; n<16; n++) *palette++ = *reg++;
}

void save_tt_palette(unsigned short *palette) {
    volatile unsigned short *reg = (unsigned short*) 0xffff8400;
    for (short n=0; n<256; n++) *palette++ = *reg++;
}

void set_st_doom_palette(const unsigned char *colors) {
    unsigned short stpalette[16];
    (void)colors;
    short res = Getrez();
    for (int i = 0; i < 16; i++) {
        unsigned char grey = (unsigned char)((i * 255) / 15);
        if (res == 1 && i < 4) {
            static const unsigned char mid_greys[4] = {0, 85, 170, 255};
            grey = mid_greys[i];
        }
        stpalette[i] = stcolor(grey, grey, grey);
    }
    install_st_palette(stpalette);
}

void set_tt_doom_palette(const unsigned char *colors) {
    unsigned short ttpalette[256];
    for (int i=0; i<256; i++) {
        const unsigned char *c = &colors[3*i];
        ttpalette[i] = ttcolor(c[0], c[1], c[2]);
    }
    install_tt_palette(ttpalette);
}

// Find the ST palette color index to fill a pixel with according to given weights.
short bayer4_color(const unsigned char *weights, short numcolors, short phase, short px) {
    static unsigned char bayer[4][4] = {
        {0,  8, 2,10},
        {12, 4,14, 6},
        { 3,11, 1, 9},
        {15, 7,13, 5}
    };

    unsigned char bayer_lwb = 0, bayer_upb = 0;
    short c;
    for (c=0; c<numcolors; c++) {
        bayer_upb += weights[c];
        if (bayer[phase][px%4] >= bayer_lwb && bayer[phase][px%4] < bayer_upb) {
            return c; // Search for color is finished.
        }
        bayer_lwb += weights[c];
    }
    return -1;
}


/// @brief Computes a movep-compatible Bayer-dithered pixel given a set of mixing weights.
/// @param weights An array of mixing weights, each weight corresponding to a palette color. Must sum up to 16.
/// @param phase The vertical phase within the bayer pattern (0..3).
/// @param px The pixel within the group of 8 pixels covered by a movep-DWORD (0..7).
/// @return A DWORD that can be written into an Atari ST lo-res framebuffer using movep.l.
unsigned long bayer4_lorez_pdata(const unsigned char *weights, short phase, short px) {
    short c = bayer4_color(weights, 16, phase, px);
    // Compute a pdata-compatible pixel representation.
    unsigned long pdata = 0;
    if (c & 1) pdata |= 0x01000000;
    if (c & 2) pdata |= 0x00010000;
    if (c & 4) pdata |= 0x00000100;
    if (c & 8) pdata |= 0x00000001;
    return pdata << (7-px);
}

/// @brief Computes a longword containing Bayer-dithered pixel given a set of mixing weights.
/// @param weights An array of mixing weights, each weight corresponding to a palette color. Must sum up to 16.
/// @param phase The vertical phase within the bayer pattern (0..3).
/// @param px The pixel within the group of 16 pixels covered by a longword in midrez (0..15).
/// @return A DWORD that can be written into an Atari ST mid-res framebuffer.
unsigned long bayer4_midrez_pdata(const unsigned char *weights, short phase, short px) {
    short c = bayer4_color(weights, 4, phase, px);
    // Compute a midrez-compatible pixel representation.
    unsigned long data = 0;
    if (c & 1) data |= 0x00010000;
    if (c & 2) data |= 0x00000001;
    return data << (15-px);
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]);
static void c2p_1x_midrez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]);
static void c2p_1x_hirez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]);
static void c2p_1x_tt_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, u_int32_t *table);
static void c2p_screen_lorez(unsigned char *out, const unsigned char *in);
static void c2p_screen_midrez(unsigned char *out, const unsigned char *in);
static void c2p_screen_hirez(unsigned char *out, const unsigned char *in);
static void c2p_screen_tt_lorez(unsigned char *out, const unsigned char *in);

static void c2p_statusbar_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    out += y_begin * 160 + x_begin / 2;
    in += y_begin * 320 + x_begin;
    for (int line = y_begin; line < y_end; line++ ) {
        c2p_1x_lorez(out, in, x_end - x_begin, c2p_table[line&3]);
        out += 160;
        in += 320;
    }
}

static void c2p_statusbar_midrez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    out += y_begin * 160 + x_begin / 2;
    in += y_begin * 320 + x_begin;
    for (int line = y_begin; line < y_end; line++ ) {
        c2p_1x_midrez(out, in, x_end - x_begin, c2p_table[line&3]);
        out += 160;
        in += 320;
    }
}

static void c2p_statusbar_hirez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    out += y_begin * 160 + x_begin / 4;
    in += y_begin * 320 + x_begin;
    for (int line = y_begin; line < y_end; line++ ) {
        c2p_1x_hirez(out, in, x_end - x_begin, c2p_table[line&1]);
        out += 160;
        in += 320;
    }
}

static void c2p_statusbar_tt_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end) {
    out += y_begin * 640 + x_begin;
    in += y_begin * 320 + x_begin;
    for (int line = y_begin; line < y_end; line++ ) {
        c2p_1x_tt_lorez(out, in, x_end - x_begin, c2p_tt_table);
        out += 640;
        in += 320;
    }
}

void init_c2p_table() {
    short res = Getrez();
    if (res == 0) {
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
            for (int phase=0; phase<4; phase++) {
                // Fill 1x1 c2p table
                for (int px=0; px<8; px++) {
                    c2p_table[phase][i][px] = bayer4_lorez_pdata(weights, phase, px);
                }
                // Fill 2x2 c2p table
                for (int ipx=0; ipx<4; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=2*ipx; opx<2*ipx+2; opx++) {
                        ipx_pdata |= bayer4_lorez_pdata(weights, phase, opx);
                    }
                    c2p_2x_table[phase][i][ipx] = ipx_pdata;
                }
                // Fill 4x4 c2p table
                for (int ipx=0; ipx<2; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=4*ipx; opx<4*ipx+4; opx++) {
                        ipx_pdata |= bayer4_lorez_pdata(weights, phase, opx);
                    }
                    c2p_4x_table[phase][i][ipx] = ipx_pdata;
                }
            }
        }
    } else if (res == 1) {
        // Medium resolution, 4 colors
        subset = subset_midrez;
        c2p_screen_drawfunc = c2p_screen_midrez;
        c2p_statusbar_drawfunc = c2p_statusbar_midrez;
        install_palette = install_st_palette;
        save_palette = save_st_palette;
        set_doom_palette = set_st_doom_palette;
        build_greyscale_weights_midrez(W_CacheLumpName("PLAYPAL", PU_CACHE));
        for (int i=0; i<256; i++) {
            unsigned char *weights = mix_weights_midrez[i];
            for (int phase=0; phase<4; phase++) {
                // Fill 1x1 c2p table
                for (int ipx=0; ipx<8; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=2*ipx; opx<2*ipx+2; opx++) {
                        ipx_pdata |= bayer4_midrez_pdata(weights, phase, opx);
                    }
                    c2p_table[phase][i][ipx] = ipx_pdata;
                }
                // Fill 2x2 c2p table
                for (int ipx=0; ipx<4; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=4*ipx; opx<4*ipx+4; opx++) {
                        ipx_pdata |= bayer4_midrez_pdata(weights, phase, opx);
                    }
                    c2p_2x_table[phase][i][ipx] = ipx_pdata;
                }
                // Fill 4x4 c2p table
                for (int ipx=0; ipx<2; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=8*ipx; opx<8*ipx+8; opx++) {
                        ipx_pdata |= bayer4_midrez_pdata(weights, phase, opx);
                    }
                    c2p_4x_table[phase][i][ipx] = ipx_pdata;
                }
            }
        }
    } else if (res == 2) {
        // High resolution, 2 colors
        subset = subset_hirez;
        c2p_screen_drawfunc = c2p_screen_hirez;
        c2p_statusbar_drawfunc = c2p_statusbar_hirez;
        install_palette = install_st_palette;
        save_palette = save_st_palette;
        set_doom_palette = set_st_doom_palette;
        for (int i=0; i<256; i++) {
            unsigned char *weights = mix_weights_hirez[i];
            for (int phase=0; phase<2; phase++) {
                // Fill 1x1 c2p table
                for (int ipx=0; ipx<8; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=2*ipx; opx<2*ipx+2; opx++) {
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase, opx) << (31 - opx);
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase+1, opx) << (15 - opx);
                    }
                    c2p_table[phase][i][ipx] = ipx_pdata;
                }
                // Fill 2x2 c2p table
                for (int ipx=0; ipx<4; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=4*ipx; opx<4*ipx+4; opx++) {
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase, opx) << (31 - opx);
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase+1, opx) << (15 - opx);
                    }
                    c2p_2x_table[phase][i][ipx] = ipx_pdata;
                }
                // Fill 4x4 c2p table
                for (int ipx=0; ipx<2; ipx++) {
                    unsigned long ipx_pdata = 0;
                    for (int opx=8*ipx; opx<8*ipx+8; opx++) {
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase, opx) << (31 - opx);
                        ipx_pdata |= bayer4_color(weights, 2, 2*phase+1, opx) << (15 - opx);
                    }
                    c2p_4x_table[phase][i][ipx] = ipx_pdata;
                }
            }
        }
    } else if (res == 7) {
        // TT low resolution, 256 colors in 32
        c2p_screen_drawfunc = c2p_screen_tt_lorez;
        c2p_statusbar_drawfunc = c2p_statusbar_tt_lorez;
        install_palette = install_tt_palette;
        save_palette = save_tt_palette;
        set_doom_palette = set_tt_doom_palette;
        for (int i=0; i<16; i++) {
            uint32_t pdata = 0;
            if (i & 1) pdata |= 1<<24;
            if (i & 2) pdata |= 1<<16;
            if (i & 4) pdata |= 1<<8;
            if (i & 8) pdata |= 1<<0;
            c2p_tt_table[i] = pdata;
        }
        // Clear screen
        memset(Physbase(), 0, 320*480);
    } else {
        I_Error("Unsupported resolution %d\n", res);
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

static void c2p_1x_midrez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]) {
    if (pixels < 8) return;
    pixels -= pixels % 8; 
    while (pixels != 0) {
		register unsigned long pdata = 0;
        for(int i=0; i<8; i++) {
            pdata |= table[*in++][i];
        }
        *(unsigned long*)out = pdata;
		pixels -= 8;
		out += 4;
	}
}

static void c2p_1x_hirez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]) {
    if (pixels < 16) return;
    pixels -= pixels % 16; 
    while (pixels != 0) {
		register unsigned long pdata = 0;
        for(int i=0; i<8; i++) {
            pdata |= table[*in++][i];
        }
        *(unsigned short*)(out + 80) = pdata;
        *(unsigned short*)out = pdata >> 16;
		pixels -= 8;
		out += 2;
	}
}

static void c2p_1x_tt_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, uint32_t *table) {
    if (pixels < 16) return;
    uint32_t p03, p47, tmp;
    uint32_t groups = (pixels / 16) - 1;
    asm volatile(
        // Loop over 16-pixel groups
        "0:                                     \n\t"

        // Read 8 chunky pixels from memory
        "movem.l    (%[in])+,%%d0-%%d1          \n\t"

        // Process pixel 0
        "rol.l      #6,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "move.l     (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "move.l     (%[table],%[tmp].w),%[p03]  \n\t"

        // Process pixels 1-3
        ".rept      3                           \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"
        ".endr                                  \n\t"

        // Process pixel 4
        "rol.l      #6,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"

        // Process pixels 5-7
        ".rept      3                           \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"
        ".endr                                  \n\t"

        // Write pixels 0-7
        "movep.l    %[p03],(%[out],0)           \n\t"
        "movep.l    %[p03],(%[out],320)         \n\t"
        "movep.l    %[p47],(%[out],8)           \n\t"
        "movep.l    %[p47],(%[out],328)         \n\t"

        // Read another 8 chunky pixels from memory
        "movem.l    (%[in])+,%%d0-%%d1          \n\t"

        // Process pixel 0
        "rol.l      #6,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "move.l     (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "move.l     (%[table],%[tmp].w),%[p03]  \n\t"

        // Process pixels 1-3
        ".rept      3                           \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d0                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d0,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"
        ".endr                                  \n\t"

        // Process pixel 4
        "rol.l      #6,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"

        // Process pixels 5-7
        ".rept      3                           \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p47],%[p47]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p47]  \n\t"
        "rol.l      #4,%%d1                     \n\t"
        "moveq      #60,%[tmp]                  \n\t"
        "and.l      %%d1,%[tmp]                 \n\t"
        "add.l      %[p03],%[p03]               \n\t"
        "or.l       (%[table],%[tmp].w),%[p03]  \n\t"
        ".endr                                  \n\t"

        // Write pixels 0-7
        "movep.l    %[p03],(%[out],1)           \n\t"
        "movep.l    %[p03],(%[out],321)         \n\t"
        "movep.l    %[p47],(%[out],9)           \n\t"
        "movep.l    %[p47],(%[out],329)         \n\t"

        // Increment counters
        "lea        (%[out],16),%[out]          \n\t"

        // Next pixel group
        "dbra.w     %[groups],0b                \n\t"
        : [in] "+&a" (in)
        , [out] "+&a" (out)
        , [groups] "+&d" (groups)
        , [tmp] "=&d" (tmp)
        , [p03] "=&d" (p03)
        , [p47] "=&d" (p47)
        : [table] "a" (table)
        : "d0", "d1", "memory", "cc"
    );
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

static void c2p_2x_midrez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][4]) {
    if (pixels < 8) return;
    short groups = pixels / 8 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned long mask = 0x00ff00ff<<4; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read eight consecutive pixels from buffer into two 32 bit registers
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"

        // Prepare pixels 1, 3
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #4,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 3
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 1
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"

        // Prepare pixels 0, 2
        "lsr.l      #4,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"

        // Pixel 2
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"

        // Pixel 0
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"

        // Write these pixels into ST screen buffer
        "move.l    %[pdata], (%[out])+              \n\t"

        // Prepare pixels 5,7
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #4,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 7
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 5
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]   \n\t"

        // Prepare pixels 4, 6
        "lsr.l      #4,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"

        // Pixel 6
        "or.l       8(%[table],%%d1.w), %[pdata]   \n\t"

        // Pixel 4
        "swap       %%d1                            \n\t"
        "or.l       (%[table],%%d1.w), %[pdata]     \n\t"

        // Write these pixels into ST screen buffer
        "move.l     %[pdata], (%[out])+             \n\t"

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

static void c2p_2x_hirez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][4]) {
    if (pixels < 4) return;
    short groups = pixels / 4 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned long mask = 0x00ff00ff<<4; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read four consecutive pixels from buffer
        "move.l     (%[in])+, %%d0                  \n\t"

        // Prepare pixels 1, 3
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #4,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"

        // Pixel 3
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"

        // Pixel 1
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"

        // Prepare pixels 0, 2
        "lsr.l      #4,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"

        // Pixel 2
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"

        // Pixel 0
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"

        // Write these pixels into ST screen buffer
        // Hi-rez specialty: write into two consecutive lines
        "move.w     %[pdata], 80(%[out])           \n\t"
        "swap       %[pdata]                        \n\t"
        "move.w     %[pdata], (%[out])+             \n\t"

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
        : "d0", "d2", "memory"
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

static void c2p_4x_midrez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][2]) {
    if (pixels < 4) return;
    short groups = pixels / 4 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned short mask = 0x00ff<<3; // Mask for isolating table indices (after shifting)
    unsigned char *out2 = out + 320;
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read four consecutive pixels from buffer into two 16 bit registers
        "movem.w    (%[in])+, %%d0-%%d1             \n\t"

        // Pixel 0
        "move.w     %%d0,%%d2                       \n\t"
        "lsr.w      #5,%%d2                         \n\t"
        "and.w      %[mask],%%d2                    \n\t"
        "move.l     (%[table],%%d2.w), %[pdata]     \n\t"

        // Pixel 1
        "lsl.w      #3,%%d0                         \n\t"
        "and.w      %[mask],%%d0                    \n\t"
        "or.l       4(%[table],%%d0.w), %[pdata]    \n\t"

        // Write these pixels into ST screen buffer
        "move.l     %[pdata], (%[out])+             \n\t"
        "move.l     %[pdata], (%[out2])+            \n\t"

        // Pixel 2
        "move.w     %%d1,%%d2                       \n\t"
        "lsr.w      #5,%%d2                         \n\t"
        "and.w      %[mask],%%d2                    \n\t"
        "move.l     0(%[table],%%d2.w), %[pdata]    \n\t"

        // Pixel 3
        "lsl.w      #3,%%d1                         \n\t"
        "and.w      %[mask],%%d1                    \n\t"
        "or.l       4(%[table],%%d1.w), %[pdata]    \n\t"

        // Write these pixels into ST screen buffer
        "move.l     %[pdata], (%[out])+             \n\t"
        "move.l     %[pdata], (%[out2])+            \n\t"

        "dbra.w     %[groups],0b                    \n\t"

        // Outputs
        : [out] "+a" (out)
        , [out2] "+a" (out2)
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


static void c2p_4x_hirez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][2]) {
    if (pixels < 2) return;
    short groups = pixels / 2 - 1;
    unsigned long pdata = 0; // 32 bits of planar pixel data
    unsigned short mask = 0x00ff<<3; // Mask for isolating table indices (after shifting)
    asm volatile (
        // Beginning of dbra loop
        "0:                                         \n\t"

        // Read two consecutive pixels from buffer into a 16 bit register
        "move.w     (%[in])+, %%d0                  \n\t"

        // Pixel 0
        "move.w     %%d0,%%d2                       \n\t"
        "lsr.w      #5,%%d2                         \n\t"
        "and.w      %[mask],%%d2                    \n\t"
        "move.l     (%[table],%%d2.w), %[pdata]     \n\t"

        // Pixel 1
        "lsl.w      #3,%%d0                         \n\t"
        "and.w      %[mask],%%d0                    \n\t"
        "or.l       4(%[table],%%d0.w), %[pdata]    \n\t"

        // Write these pixels into ST screen buffer
        "move.w     %[pdata], 80(%[out])            \n\t"
        "move.w     %[pdata], 400(%[out])           \n\t"
        "swap       %[pdata]                        \n\t"
        "move.w     %[pdata], 320(%[out])           \n\t"
        "move.w     %[pdata], (%[out])+             \n\t"

        "dbra.w     %[groups],0b                    \n\t"

        // Outputs
        : [out] "+&a" (out)
        , [in] "+&a" (in)
        , [pdata] "+&d" (pdata)
        , [groups] "+&d" (groups)
        
        // Inputs
        : [table] "a" (table)
        , [mask] "d" (mask)
        
        // Clobbers
        : "d0", "d2", "memory"
    );
}

void draw_palette_table(unsigned char *st_screen) {
    set_doom_palette(W_CacheLumpName("PLAYPAL", PU_CACHE));
    short res = Getrez();
    boolean is8bit = res == 7;
    unsigned char buf[128+16];
	unsigned char c = 0;
	for (int y=0; y<128; y+=8) {
		for (int x=0; x<128; x+=8) {
			for (int i=0; i<8; i++) buf[x+i] = c;
            for (int i=0; i<16; i++) {
                // Draw highlight around color if from palette
                if (c == subset[i] && !is8bit) {
                    buf[x] = 4;
                    buf[x+7] = 0;
                }
            }
			c++;
		}
        if (!is8bit) {
            for (int x=128; x<128+8; x++) buf[x] = 0;
            for (int x=128+8; x<128+16; x++) buf[x] = subset[y/8];
        }
        if (res == 0) {
		    for (int i=0; i<8; i++) c2p_1x_lorez(st_screen + 160*(32+y+i), buf, 128+16, c2p_table[i%4]);
        } else if (res == 1) {
		    for (int i=0; i<8; i++) c2p_1x_midrez(st_screen + 160*(32+y+i), buf, 128+16, c2p_table[i%4]);
        } else if (res == 2) {
		    for (int i=0; i<8; i++) c2p_1x_hirez(st_screen + 160*(32+y+i), buf, 128+16, c2p_table[i%2]);
        } else if (res == 7) {
		    for (int i=0; i<8; i++) c2p_1x_tt_lorez(st_screen + 640*(32+y+i), buf, 128+16, c2p_tt_table);
        }
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

static void c2p_screen_midrez(unsigned char *out, const unsigned char *in) {
    boolean zoom_allowed = gamestate == GS_LEVEL
        && !menuactive && !inhelpscreens && !automapactive;
    short splitline = !zoom_allowed || viewheight == SCREENHEIGHT ? SCREENHEIGHT : SCREENHEIGHT - 32;
    if (!zoom_allowed || viewwidth > SCREENWIDTH/2) {
        for (short line = 0; line < splitline; line++ ) {
            c2p_1x_midrez(out + 160*line, in + 320*line, 320, c2p_table[line&3]);
        }
    } else if(viewwidth > SCREENWIDTH/4) {
        // 2x zoom
        for (short line = 0; line < splitline; line++ ) {
            c2p_2x_midrez(out + 160*line, in + SCREENWIDTH*(42 + line/2) + 80, 160, c2p_2x_table[line&3]);
        }
    } else {
        // 4x zoom
        for (short line = 0; line < splitline; line++ ) {
            short phase = line & 3;
            if (phase < 2) {
                c2p_4x_midrez(out + 160*line, in + SCREENWIDTH*(63 + line/4) + 120, 80, c2p_4x_table[phase]);
            }
        }
    }
}

static void c2p_screen_hirez(unsigned char *out, const unsigned char *in) {
    boolean zoom_allowed = gamestate == GS_LEVEL
        && !menuactive && !inhelpscreens && !automapactive;
    short splitline = !zoom_allowed || viewheight == SCREENHEIGHT ? SCREENHEIGHT : SCREENHEIGHT - 32;
    if (!zoom_allowed || viewwidth > SCREENWIDTH/2) {
        for (short line = 0; line < splitline; line++ ) {
            c2p_1x_hirez(out + 160*line, in + 320*line, 320, c2p_table[line&1]);
        }
    } else if(viewwidth > SCREENWIDTH/4) {
        // 2x zoom
        for (short line = 0; line < splitline; line++ ) {
            c2p_2x_hirez(out + 160*line, in + SCREENWIDTH*(42 + line/2) + 80, 160, c2p_2x_table[line&1]);
        }
    } else {
        // 4x zoom
        for (short line = 0; line < splitline; line++ ) {
            short phase = line & 3;
            if (phase < 2) {
                c2p_4x_hirez(out + 160*line, in + SCREENWIDTH*(63 + line/4) + 120, 80, c2p_4x_table[phase&1]);
            }
        }
    }
}

static void c2p_screen_tt_lorez(unsigned char *out, const unsigned char *in) {
    boolean zoom_allowed = gamestate == GS_LEVEL
        && !menuactive && !inhelpscreens && !automapactive;
    short splitline = !zoom_allowed || viewheight == SCREENHEIGHT ? SCREENHEIGHT : SCREENHEIGHT - 32;
    if (!zoom_allowed || viewwidth > SCREENWIDTH/2) {
        for (short line = 0; line < splitline; line++ ) {
            c2p_1x_tt_lorez(out + 640*line, in + 320*line, 320, c2p_tt_table);
        }
    } else if(viewwidth > SCREENWIDTH/4) {
        // 2x zoom
        for (short line = 0; line < splitline; line++ ) {
            c2p_2x_hirez(out + 160*line, in + SCREENWIDTH*(42 + line/2) + 80, 160, c2p_2x_table[line&1]);
        }
    } else {
        // 4x zoom
        for (short line = 0; line < splitline; line++ ) {
            short phase = line & 3;
            if (phase < 2) {
                c2p_4x_hirez(out + 160*line, in + SCREENWIDTH*(63 + line/4) + 120, 80, c2p_4x_table[phase&1]);
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
