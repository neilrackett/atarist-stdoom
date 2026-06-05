#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void init_c2p_table();

void c2p_screen(unsigned char *out, const unsigned char *in);
void c2p_statusbar(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
extern void (*set_doom_palette)(const unsigned char *colors);
void draw_palette_table(unsigned char *st_screen);
extern void (*install_palette)(const unsigned short *palette);
extern void (*save_palette)(unsigned short *palette);

/* Software drawfuncs, palette helper, function pointers, and reduction-map
 * source data — exposed so sidecart_c2p.c can install over them and fall back
 * to them when the SidecarTridge accelerator is active. */
extern void (*c2p_screen_drawfunc)(unsigned char *out, const unsigned char *in);
extern void (*c2p_statusbar_drawfunc)(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
void c2p_screen_lorez(unsigned char *out, const unsigned char *in);
void c2p_statusbar_lorez(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
void set_st_doom_palette(const unsigned char *colors);
extern unsigned char mix_weights_lorez[256][16];

#endif // ATARI_C2P_H
