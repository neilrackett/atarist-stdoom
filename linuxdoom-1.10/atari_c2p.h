#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void init_c2p_table();

// STDOOM Coprocessor: detect a SidecarTridge running the STDOOM Coprocessor firmware.
// Call once after init_c2p_table(). Sets c2p_md_active (1 if present, else 0).
// Milestone 1 only detects; later milestones route C2P to the sidecart here.
void c2p_md_init();
extern int c2p_md_active;

void c2p_screen(unsigned char *out, const unsigned char *in);
void c2p_statusbar(unsigned char *out, const unsigned char *in, short y_begin, short y_end, short x_begin, short x_end);
extern void (*set_doom_palette)(const unsigned char *colors);
void draw_palette_table(unsigned char *st_screen);
extern void (*install_palette)(const unsigned short *palette);
extern void (*save_palette)(unsigned short *palette);

#endif // ATARI_C2P_H