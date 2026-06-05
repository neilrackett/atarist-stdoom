#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void init_c2p_table();
void c2p_screen(unsigned char *out, const unsigned char *in);
void c2p_statusbar(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
extern void (*set_doom_palette)(const unsigned char *colors);
void draw_palette_table(unsigned char *st_screen);
extern void (*install_palette)(const unsigned short *palette);
extern void (*save_palette)(unsigned short *palette);

// Optional: override low-res (16-pen) dithering weights from a .W16 file.
// 4116-byte C2P_WeightSet: magic "W16\0", subset[16], weights[256][16].
// subset[pen] = PLAYPAL index for hardware pen pen; weights[src][pen] dither mix weights.
// Returns 1 if installed, 0 on failure.
int atari_try_install_w16(const char *path);
// Try stdoom.w16 in the current directory; silent if missing. Returns 1 if installed.
int atari_try_install_default_w16(void);

#endif // ATARI_C2P_H