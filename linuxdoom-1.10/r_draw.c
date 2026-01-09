// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	The actual span/column drawing functions.
//	Here find the main potential for optimization,
//	 e.g. inline assembly, different algorithms.
//
//-----------------------------------------------------------------------------

static const char
    rcsid[] = "$Id: r_draw.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";

#include "doomdef.h"

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "r_local.h"

// Needs access to LFB (guess what).
#include "v_video.h"

// State.
#include "doomstat.h"

// ?
#define MAXWIDTH 320
#define MAXHEIGHT 200

// status bar height at bottom of screen
#define SBARHEIGHT 32

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//

byte *viewimage;
int viewwidth;
int scaledviewwidth;
int viewheight;
int viewwindowx;
int viewwindowy;
byte *ylookup[MAXHEIGHT];
int columnofs[MAXWIDTH];

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//
byte translations[3][256];

//
// R_DrawColumn
// Source is the top of the column to scale.
//
lighttable_t *dc_colormap;
short dc_x;
short dc_yl;
short dc_yh;
fixed_t dc_iscale;
fixed_t dc_texturemid;

// first pixel in a column (possibly virtual)
byte *dc_source;

// just for profiling
int dccount;

/// @brief Converts a Q15.16 fixed-point value into a Q7.8 value that fits in a short
/// @param n the Q15.16 value to convert
/// @return a a short in Q7.8 format
static short reduce(fixed_t n)
{
    return (n >> 8) & 0xffff;
}

/// @brief Converts a Q7.8 value into an inverted 8.0Q6 representation where the integral part is after the fractional part.
/// @param n the Q7.8 value to convert
/// @return a short in 8.0Q6 format
static short to_8_0Q6(short n)
{
    return (n << 8) | ((n >> 8) & 0x007f);
}

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
//
void R_DrawColumn(void)
{
    short count;
    byte *dest;
    unsigned short frac;
    unsigned short fracstep;

    count = dc_yh - dc_yl;

    // Zero length, column does not exceed a pixel.
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows?
    dest = ylookup[dc_yl] + columnofs[dc_x];

    // Determine scaling,
    //  which is the only mapping to be done.
    // Reduce to 8.8 fixed-point short.
    fracstep = reduce(dc_iscale);
    frac = reduce(dc_texturemid) + (int)(dc_yl - centery) * fracstep;

    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    byte *source = dc_source;
    lighttable_t *colormap = dc_colormap;
    // Invert the order of the fixed-point values so that the integral part
    // is in the lowest 7 bits, thereby making bit-shifting unnecessary.
    fracstep = to_8_0Q6(fracstep);
    frac = to_8_0Q6(frac);
    // Invariant: only least-significant byte is ever != 0
    unsigned long tmp = 0;
    asm volatile(
        // Unroll loop 2x
        "lsr.w  #1,%[count]                     \n\t"

        // Even number of pixels to be drawn (i.e., Carry set)? Skip special initialization.
        "jcs    3f                              \n\t"

        // Odd number of pixels. Clear X flag and ompensate for %[dest]
        // getting incremented by 2*%[width] at end of loop.
        "add.w  %[tmp],%[frac]                  \n\t"
        "lea    (-1*%c[width])(%[dest]),%[dest] \n\t"
        "jmp    1f                              \n\t"

        // Even number of pixels. Clear X flag for next addx and enter loop.
        "3:                                     \n\t"
        "add.w  %[tmp],%[frac]                  \n\t"

        // Loop begin
        "0:                                     \n\t"

        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        "move.b (%[colormap],%[tmp].w),(%[dest])\n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"

        "1:                                     \n\t"
        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        "move.b (%[colormap],%[tmp].w),%c[width](%[dest])\n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"

        "lea    (2*%c[width])(%[dest]),%[dest]  \n\t"
        "dbra   %[count],0b                     \n\t"

        // outputs
        : [tmp] "+&d"(tmp), [frac] "+&d"(frac), [dest] "+&a"(dest), [count] "+&d"(count)
        // inputs
        : [source] "a"(source), [colormap] "a"(colormap), [step] "d"(fracstep), [zero] "d"(0), [mask] "d"(0xff7f), [width] "i"(SCREENWIDTH)
        // clobbers
        : "memory", "cc");
}

// UNUSED.
// Loop unrolled.
#if 0
void R_DrawColumn (void) 
{ 
    int			count; 
    byte*		source;
    byte*		dest;
    byte*		colormap;
    
    unsigned		frac;
    unsigned		fracstep;
    unsigned		fracstep2;
    unsigned		fracstep3;
    unsigned		fracstep4;	 
 
    count = dc_yh - dc_yl + 1; 

    source = dc_source;
    colormap = dc_colormap;		 
    dest = ylookup[dc_yl] + columnofs[dc_x];  
	 
    fracstep = dc_iscale<<9; 
    frac = (dc_texturemid + (dc_yl-centery)*dc_iscale)<<9; 
 
    fracstep2 = fracstep+fracstep;
    fracstep3 = fracstep2+fracstep;
    fracstep4 = fracstep3+fracstep;
	
    while (count >= 8) 
    { 
	dest[0] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*2] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*3] = colormap[source[(frac+fracstep3)>>25]];
	
	frac += fracstep4; 

	dest[SCREENWIDTH*4] = colormap[source[frac>>25]]; 
	dest[SCREENWIDTH*5] = colormap[source[(frac+fracstep)>>25]]; 
	dest[SCREENWIDTH*6] = colormap[source[(frac+fracstep2)>>25]]; 
	dest[SCREENWIDTH*7] = colormap[source[(frac+fracstep3)>>25]]; 

	frac += fracstep4; 
	dest += SCREENWIDTH*8; 
	count -= 8;
    } 
	
    while (count > 0)
    { 
	*dest = colormap[source[frac>>25]]; 
	dest += SCREENWIDTH; 
	frac += fracstep; 
	count--;
    } 
}
#endif

void R_DrawColumnLow(void)
{
    short count;
    byte *dest;
    byte *dest2;
    unsigned short frac;
    unsigned short fracstep;

    count = dc_yh - dc_yl;

    // Zero length.
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {

        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
    }
    //	dccount++;
#endif
    // Blocky mode, need to multiply by 2.
    dc_x <<= 1;

    dest = ylookup[dc_yl] + columnofs[dc_x];
    dest2 = ylookup[dc_yl] + columnofs[dc_x + 1];

    fracstep = reduce(dc_iscale);
    frac = reduce(dc_texturemid) + (int)(dc_yl - centery) * fracstep;

    byte *source = dc_source;
    lighttable_t *colormap = dc_colormap;
    fracstep = to_8_0Q6(fracstep);
    frac = to_8_0Q6(frac);
    unsigned long tmp = 0;
    asm volatile(
        // Clear X flag for addx in loop.
        "add.w  %[tmp],%[frac]                  \n\t"
        "0:                                     \n\t"
        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        "move.b (%[colormap],%[tmp].w),(%[dest])\n\t"
        "move.b (%[colormap],%[tmp].w),(%[dest2])\n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"
        "lea    (%c[width])(%[dest]),%[dest]    \n\t"
        "lea    (%c[width])(%[dest2]),%[dest2]  \n\t"
        "dbra   %[count],0b                     \n\t"
        : [tmp] "+&d"(tmp), [frac] "+&d"(frac), [dest] "+&a"(dest), [dest2] "+&a"(dest2), [count] "+&d"(count)
        : [source] "a"(source), [colormap] "a"(colormap), [step] "d"(fracstep), [mask] "d"(0xff7f), [width] "i"(SCREENWIDTH)
        : "memory", "cc");
}

//
// Spectre/Invisibility.
//
#define FUZZTABLE 50
#define FUZZOFF (SCREENWIDTH)

short fuzzoffset[FUZZTABLE] =
    {
        FUZZOFF, -FUZZOFF, FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF,
        FUZZOFF, FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF,
        FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF, -FUZZOFF, -FUZZOFF, -FUZZOFF,
        FUZZOFF, -FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF,
        FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF, -FUZZOFF, FUZZOFF,
        FUZZOFF, -FUZZOFF, -FUZZOFF, -FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF,
        FUZZOFF, FUZZOFF, -FUZZOFF, FUZZOFF, FUZZOFF, -FUZZOFF, FUZZOFF};

short fuzzpos = 0;

//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//
void R_DrawFuzzColumn(void)
{
    short count;
    byte *dest;

    // Adjust borders. Low...
    if (!dc_yl)
        dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight - 1)
        dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;

    // Zero length.
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
        I_Error("R_DrawFuzzColumn: %i to %i at %i",
                dc_yl, dc_yh, dc_x);
    }
#endif

    // Does not work with blocky mode.
    dest = ylookup[dc_yl] + columnofs[dc_x];
    short fuzzpos_tmp = fuzzpos; // Copy global into register for performance
    // Use colormap #6 so the color gets a little darker.
    byte *colormap = colormaps + 6 * 256;

    asm volatile(
        "move.w %[fuzzpos],%%d6                 \n\t"
        "0:                                     \n\t"
        "move.w %%d6,%%d7                       \n\t"
        "add.w  %%d7,%%d7                       \n\t"
        "move.w (%[fuzzofs],%%d7.w),%%d5        \n\t"
        "move.b (%[dest],%%d5.w),%%d4           \n\t"
        "move.b (%[colormap],%%d4.w),%%d4       \n\t"
        "move.b %%d4,(%[dest])                  \n\t"
        "addq.w #1,%%d6                         \n\t"
        "cmpi.w #50,%%d6                        \n\t"
        "bne    1f                              \n\t"
        "moveq  #0,%%d6                         \n\t"
        "1:                                     \n\t"
        "lea    (%c[width])(%[dest]),%[dest]    \n\t"
        "dbra   %[count],0b                     \n\t"
        "move.w %%d6,%[fuzzpos]                 \n\t"
        : [dest] "+&a"(dest), [count] "+&d"(count), [fuzzpos] "+&d"(fuzzpos_tmp)
        : [fuzzofs] "a"(fuzzoffset), [colormap] "a"(colormap), [width] "i"(SCREENWIDTH)
        : "d4", "d5", "d6", "d7", "memory", "cc");

    fuzzpos = fuzzpos_tmp; // Copy register back into global
}

//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//
byte *dc_translation;
byte *translationtables;

void R_DrawTranslatedColumn(void)
{
    short count;
    byte *dest;
    unsigned short frac;
    unsigned short fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
        I_Error("R_DrawColumn: %i to %i at %i",
                dc_yl, dc_yh, dc_x);
    }

#endif

    // WATCOM VGA specific.
    /* Keep for fixing.
    if (detailshift)
    {
    if (dc_x & 1)
        outp (SC_INDEX+1,12);
    else
        outp (SC_INDEX+1,3);

    dest = destview + dc_yl*80 + (dc_x>>1);
    }
    else
    {
    outp (SC_INDEX+1,1<<(dc_x&3));

    dest = destview + dc_yl*80 + (dc_x>>2);
    }*/

    // FIXME. As above.
    dest = ylookup[dc_yl] + columnofs[dc_x];

    // Looks familiar.
    fracstep = reduce(dc_iscale);
    frac = reduce(dc_texturemid) + (int)(dc_yl - centery) * fracstep;

    byte *source = dc_source;
    byte *translation = dc_translation;
    lighttable_t *colormap = dc_colormap;
    fracstep = to_8_0Q6(fracstep);
    frac = to_8_0Q6(frac);
    unsigned long tmp = 0;
    asm volatile(
        // Clear X flag for addx in loop.
        "add.w  %[tmp],%[frac]                  \n\t"
        "0:                                     \n\t"
        "move.b %[frac],%[tmp]                  \n\t"
        "move.b (%[source],%[tmp].w),%[tmp]     \n\t"
        "move.b (%[trans],%[tmp].w),%[tmp]      \n\t"
        "move.b (%[colormap],%[tmp].w),(%[dest])\n\t"
        "addx.w %[step],%[frac]                 \n\t"
        "and.w  %[mask],%[frac]                 \n\t"
        "lea    (%c[width])(%[dest]),%[dest]    \n\t"
        "dbra   %[count],0b                     \n\t"
        : [tmp] "+&d"(tmp), [frac] "+&d"(frac), [dest] "+&a"(dest), [count] "+&d"(count)
        : [source] "a"(source), [trans] "a"(translation), [colormap] "a"(colormap), [step] "d"(fracstep), [mask] "d"(0xff7f), [width] "i"(SCREENWIDTH)
        : "memory", "cc");
}

//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//
void R_InitTranslationTables(void)
{
    short i;

    translationtables = Z_Malloc(256 * 3 + 255, PU_STATIC, 0);
    translationtables = (byte *)(((int)translationtables + 255) & ~255);

    // translate just the 16 green colors
    for (i = 0; i < 256; i++)
    {
        if (i >= 0x70 && i <= 0x7f)
        {
            // map green ramp to gray, brown, red
            translationtables[i] = 0x60 + (i & 0xf);
            translationtables[i + 256] = 0x40 + (i & 0xf);
            translationtables[i + 512] = 0x20 + (i & 0xf);
        }
        else
        {
            // Keep all other colors as is.
            translationtables[i] = translationtables[i + 256] = translationtables[i + 512] = i;
        }
    }
}

//
// R_DrawSpan
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//
short ds_y;
short ds_x1;
short ds_x2;

lighttable_t *ds_colormap;

fixed_t ds_xfrac;
fixed_t ds_yfrac;
fixed_t ds_xstep;
fixed_t ds_ystep;

// start of a 64*64 tile image
byte *ds_source;

// just for profiling
int dscount;

typedef unsigned int fixed_2in1_t;

// Poor man's SIMD:
// Pack xfrac and yfrac as 6.9 fixedpoint numbers into a single 32 bit integer:
// XXXXXXxx xxxxxxx0 YYYYYYyy yyyyyy0
static fixed_2in1_t pack(unsigned int a, unsigned int b)
{
    return ((a & 0x003fff80) << 10) | ((b & 0x003fff80) >> 6);
}

//
// Draws the actual span.
void R_DrawSpan(void)
{
    byte *dest;
    short count;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= SCREENWIDTH || (unsigned)ds_y > SCREENHEIGHT)
    {
        I_Error("R_DrawSpan: %i to %i at %i",
                ds_x1, ds_x2, ds_y);
    }
//	dscount++;
#endif

    dest = ylookup[ds_y] + columnofs[ds_x1];

    // We do not check for zero spans here?
    count = ds_x2 - ds_x1;

    lighttable_t *colormap = ds_colormap;
    byte *source = ds_source;
    // Poor man's SIMD:
    // Pack xfrac and yfrac as 6.9 fixedpoint numbers into a single 32 bit integer:
    // XXXXXXxx xxxxxxx0 YYYYYYyy yyyyyy0
    fixed_2in1_t mask = 0xfffefffe;
    fixed_2in1_t frac = pack(ds_xfrac, ds_yfrac);
    fixed_2in1_t step = pack(ds_xstep, ds_ystep);
    asm volatile(
        "0:                                     \n\t"
        "move.l %[frac],%%d3                    \n\t"
        "move.w %%d3,%%d4                       \n\t"
        "and.w  #0xfc00,%%d4                    \n\t"
        "lsr.w  #4,%%d4                         \n\t"
        "swap   %%d3                            \n\t"
        "and.w  #0xfc00,%%d3                    \n\t"
        "lsr.w  #8,%%d3                         \n\t"
        "lsr.w  #2,%%d3                         \n\t"
        "or.w   %%d4,%%d3                       \n\t"
        "move.b (%[source],%%d3.w),%%d5         \n\t"
        "move.b (%[colormap],%%d5.w),(%[dest])+ \n\t"
        "add.l  %[step],%[frac]                 \n\t"
        "and.l  %[mask],%[frac]                 \n\t"
        "dbra   %[count],0b                     \n\t"
        : [dest] "+&a"(dest), [count] "+&d"(count), [frac] "+&d"(frac)
        : [source] "a"(source), [colormap] "a"(colormap), [step] "d"(step), [mask] "d"(mask)
        : "d3", "d4", "d5", "memory", "cc");
}

// UNUSED.
// Loop unrolled by 4.
#if 0
void R_DrawSpan (void) 
{ 
    unsigned	position, step;

    byte*	source;
    byte*	colormap;
    byte*	dest;
    
    unsigned	count;
    usingned	spot; 
    unsigned	value;
    unsigned	temp;
    unsigned	xtemp;
    unsigned	ytemp;
		
    position = ((ds_xfrac<<10)&0xffff0000) | ((ds_yfrac>>6)&0xffff);
    step = ((ds_xstep<<10)&0xffff0000) | ((ds_ystep>>6)&0xffff);
		
    source = ds_source;
    colormap = ds_colormap;
    dest = ylookup[ds_y] + columnofs[ds_x1];	 
    count = ds_x2 - ds_x1 + 1; 
	
    while (count >= 4) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[0] = colormap[source[spot]]; 

	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[1] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[2] = colormap[source[spot]];
	
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	dest[3] = colormap[source[spot]]; 
		
	count -= 4;
	dest += 4;
    } 
    while (count > 0) 
    { 
	ytemp = position>>4;
	ytemp = ytemp & 4032;
	xtemp = position>>26;
	spot = xtemp | ytemp;
	position += step;
	*dest++ = colormap[source[spot]]; 
	count--;
    } 
}
#endif

//
// Again..
//
void R_DrawSpanLow(void)
{
    byte *dest;
    short count;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= SCREENWIDTH || (unsigned)ds_y > SCREENHEIGHT)
    {
        I_Error("R_DrawSpan: %i to %i at %i",
                ds_x1, ds_x2, ds_y);
    }
//	dscount++;
#endif

    // Blocky mode, need to multiply by 2.
    ds_x1 <<= 1;
    ds_x2 <<= 1;

    dest = ylookup[ds_y] + columnofs[ds_x1];

    count = ds_x2 - ds_x1;

    lighttable_t *colormap = ds_colormap;
    byte *source = ds_source;
    fixed_2in1_t mask = 0xfffefffe;
    fixed_2in1_t frac = pack(ds_xfrac, ds_yfrac);
    fixed_2in1_t step = pack(ds_xstep, ds_ystep);
    asm volatile(
        "0:                                     \n\t"
        "move.l %[frac],%%d3                    \n\t"
        "move.w %%d3,%%d4                       \n\t"
        "and.w  #0xfc00,%%d4                    \n\t"
        "lsr.w  #4,%%d4                         \n\t"
        "swap   %%d3                            \n\t"
        "and.w  #0xfc00,%%d3                    \n\t"
        "lsr.w  #8,%%d3                         \n\t"
        "lsr.w  #2,%%d3                         \n\t"
        "or.w   %%d4,%%d3                       \n\t"
        "move.b (%[source],%%d3.w),%%d5         \n\t"
        "move.b (%[colormap],%%d5.w),%%d5       \n\t"
        "move.b %%d5,(%[dest])+                 \n\t"
        "move.b %%d5,(%[dest])+                 \n\t"
        "add.l  %[step],%[frac]                 \n\t"
        "and.l  %[mask],%[frac]                 \n\t"
        "dbra   %[count],0b                     \n\t"
        : [dest] "+&a"(dest), [count] "+&d"(count), [frac] "+&d"(frac)
        : [source] "a"(source), [colormap] "a"(colormap), [step] "d"(step), [mask] "d"(mask)
        : "d3", "d4", "d5", "memory", "cc");
}

//
// R_InitBuffer
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//
void R_InitBuffer(short width,
                  short height)
{
    short i;
    short dispwidth = width;
    short dispheight = height;

#if ST_LOWRES_SCALAR > 1
    if (width == ST_LOWRES_WIDTH && height == ST_LOWRES_HEIGHT)
    {
        dispwidth = width * ST_LOWRES_SCALAR;
        dispheight = height * ST_LOWRES_SCALAR;
    }
#endif

    // Handle resize,
    //  e.g. smaller view windows
    //  with border and/or status bar.
    viewwindowx = (SCREENWIDTH - dispwidth) >> 1;
#if ST_LOWRES_SCALAR > 1
    // Align to 16-pixel blocks for C2P conversion.
    viewwindowx = (viewwindowx + 8) & ~15;
    if (viewwindowx + dispwidth > SCREENWIDTH)
        viewwindowx -= 16;
#endif

    // Column offset. For windows.
    for (i = 0; i < width; i++)
        columnofs[i] = viewwindowx + i;

    // Samw with base row offset.
    if (dispwidth == SCREENWIDTH || dispheight >= (SCREENHEIGHT - SBARHEIGHT))
        viewwindowy = 0;
    else
        viewwindowy = (SCREENHEIGHT - SBARHEIGHT - dispheight) >> 1;

    // Preclaculate all row offsets.
    for (i = 0; i < height; i++)
        ylookup[i] = screens[0] + (i + viewwindowy) * SCREENWIDTH;
}

//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
void R_FillBackScreen(void)
{
    byte *src;
    byte *dest;
    int x;
    int y;
    patch_t *patch;
    int dispwidth = scaledviewwidth;
    int dispheight = viewheight;

    // DOOM border patch.
    char name1[] = "FLOOR7_2";

    // DOOM II border patch.
    char name2[] = "GRNROCK";

    char *name;

#if ST_LOWRES_SCALAR > 1
    if (scaledviewwidth == ST_LOWRES_WIDTH && viewheight == ST_LOWRES_HEIGHT)
    {
        dispwidth = scaledviewwidth * ST_LOWRES_SCALAR;
        dispheight = viewheight * ST_LOWRES_SCALAR;
    }
#endif

    if (dispwidth == 320)
        return;

    if (gamemode == commercial)
        name = name2;
    else
        name = name1;

    src = W_CacheLumpName(name, PU_CACHE);
    dest = screens[1];

    for (y = 0; y < SCREENHEIGHT - SBARHEIGHT; y++)
    {
        for (x = 0; x < SCREENWIDTH / 64; x++)
        {
            memcpy(dest, src + ((y & 63) << 6), 64);
            dest += 64;
        }

        if (SCREENWIDTH & 63)
        {
            memcpy(dest, src + ((y & 63) << 6), SCREENWIDTH & 63);
            dest += (SCREENWIDTH & 63);
        }
    }

    patch = W_CacheLumpName("brdr_t", PU_CACHE);

    for (x = 0; x < dispwidth; x += 8)
        V_DrawPatch(viewwindowx + x, viewwindowy - 8, 1, patch);
    patch = W_CacheLumpName("brdr_b", PU_CACHE);

    for (x = 0; x < dispwidth; x += 8)
        V_DrawPatch(viewwindowx + x, viewwindowy + dispheight, 1, patch);
    patch = W_CacheLumpName("brdr_l", PU_CACHE);

    for (y = 0; y < dispheight; y += 8)
        V_DrawPatch(viewwindowx - 8, viewwindowy + y, 1, patch);
    patch = W_CacheLumpName("brdr_r", PU_CACHE);

    for (y = 0; y < dispheight; y += 8)
        V_DrawPatch(viewwindowx + dispwidth, viewwindowy + y, 1, patch);

    // Draw beveled edge.
    V_DrawPatch(viewwindowx - 8,
                viewwindowy - 8,
                1,
                W_CacheLumpName("brdr_tl", PU_CACHE));

    V_DrawPatch(viewwindowx + dispwidth,
                viewwindowy - 8,
                1,
                W_CacheLumpName("brdr_tr", PU_CACHE));

    V_DrawPatch(viewwindowx - 8,
                viewwindowy + viewheight,
                1,
                W_CacheLumpName("brdr_bl", PU_CACHE));

    V_DrawPatch(viewwindowx + dispwidth,
                viewwindowy + dispheight,
                1,
                W_CacheLumpName("brdr_br", PU_CACHE));
}

//
// Copy a screen buffer.
//
void R_VideoErase(unsigned ofs,
                  int count)
{
    // LFB copy.
    // This might not be a good idea if memcpy
    //  is not optiomal, e.g. byte by byte on
    //  a 32bit CPU, as GNU GCC/Linux libc did
    //  at one point.
    memcpy(screens[0] + ofs, screens[1] + ofs, count);
}

//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//
void V_MarkRect(int x,
                int y,
                int width,
                int height);

void R_DrawViewBorder(void)
{
    int top;
    int left;
    int right;
    int bottom;
    int ofs;
    int i;
    int dispwidth = scaledviewwidth;
    int dispheight = viewheight;

#if ST_LOWRES_SCALAR > 1
    if (scaledviewwidth == ST_LOWRES_WIDTH && viewheight == ST_LOWRES_HEIGHT)
    {
        dispwidth = scaledviewwidth * ST_LOWRES_SCALAR;
        dispheight = viewheight * ST_LOWRES_SCALAR;
    }
#endif

    if (dispwidth == SCREENWIDTH)
        return;

    top = viewwindowy;
    left = viewwindowx;
    right = SCREENWIDTH - (viewwindowx + dispwidth);
    bottom = (SCREENHEIGHT - SBARHEIGHT) - (top + dispheight);

    if (top > 0)
    {
        R_VideoErase(0, top * SCREENWIDTH);
    }
    if (bottom > 0)
    {
        ofs = (top + dispheight) * SCREENWIDTH;
        R_VideoErase(ofs, bottom * SCREENWIDTH);
    }

    for (i = 0; i < dispheight; i++)
    {
        ofs = (top + i) * SCREENWIDTH;
        if (left > 0)
            R_VideoErase(ofs, left);
        if (right > 0)
            R_VideoErase(ofs + SCREENWIDTH - right, right);
    }

    // ?
    V_MarkRect(0, 0, SCREENWIDTH, SCREENHEIGHT - SBARHEIGHT);
}
