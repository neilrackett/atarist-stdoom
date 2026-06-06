#ifndef SIDECART_C2P_H
#define SIDECART_C2P_H

/*
 * SidecarTridge-accelerated C2P (DOOM Accelerator).
 *
 * When a SidecarTridge Multi-device running the DOOM Accelerator firmware is
 * present, this layer overrides the software C2P function pointers in
 * atari_c2p.c with versions that offload chunky-to-planar conversion to the
 * RP2040. When no accelerator is detected, the software path in atari_c2p.c is
 * used unchanged.
 *
 * Lifecycle:
 *   sidecart_c2p_init()     — detect + INIT + SET_MAP. Call once early (in
 *                             D_DoomMain, user mode, before I_Init). Sets
 *                             c2p_md_active.
 *   sidecart_c2p_install()  — override the software drawfunc/palette pointers
 *                             with the accelerated versions. Call after
 *                             init_c2p_table() in I_InitGraphics. No-op unless
 *                             c2p_md_active and the screen is low-res.
 */

/* 1 = SidecarTridge DOOM Accelerator firmware detected and primed. */
extern int c2p_md_active;

void sidecart_c2p_init(void);
void sidecart_c2p_install(void);

#endif /* SIDECART_C2P_H */
