/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * Application window declaration and startup interface.
 *
 * Fill HRAPP and call hr_open(&app, &argc, argv).  It connects to zview,
 * initializes clgfx and returns the granted content size in ha_w/ha_h.
 * Read font metrics from the shared VRAM tail.
 *
 * ha_menu selects HRM_* commands.  E_MENU arg0 contains the selected bit.
 * Global options are consumed from argv:
 *   -T title      override the window title
 *   -I icon       override the desktop icon
 *   -S WxH        request content size for HRF_STRETCH windows
 *   -P X,Y        request the frame origin
 *   -H            open minimized
 */
#ifndef HRAPP_H
#define HRAPP_H

/* An application's declaration of the window it wants.  ha_w/ha_h are updated
 * in place by hr_open() to the size actually granted.  ha_x/ha_y are usually
 * left alone: they matter only when ha_flags has HRF_POS, which is what -P
 * sets, and an app that does not care lets the server place the window. */
typedef struct {
	char	*ha_title;	/* window title; also the instance base name    */
	char	*ha_icon;	/* .icn under /usr/hr/icons; NULL/"" = default; */
				/* a name that is not installed also falls back */
	int	ha_w, ha_h;	/* desired -> granted content size, px          */
	int	ha_flags;	/* HRF_* (wire.h); HRF_STRETCH = resizable      */
	int	ha_x, ha_y;	/* wanted frame origin, px, if HRF_POS          */
	int	ha_menu;	/* HRM_* (wire.h): our window-menu entries, 0 = */
				/* none.  See "Menu entries" below.             */
} HRAPP;

extern int	hr_open();	/* hr_open(&app, &argc, argv) -> wid, or -1    */
extern int	hr_attach();	/* hr_attach(wid): adopt an EXISTING window
				 * (no handshake) -- for a spawned helper that
				 * borrows its blocked parent's window/ring     */
extern int	hr_wid();	/* our window id (-1 before hr_open succeeds)  */
extern int	hr_bye();	/* tell the server to reap our window          */
extern int	hr_cmd();	/* hr_cmd(C_*): send a bare command record     */

#endif /* HRAPP_H */
