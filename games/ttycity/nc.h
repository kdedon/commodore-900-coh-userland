/* nc.h  --  shared declarations for the ncurses Micropolis front end.
 *
 * Included by the nc_*.c files after <curses.h>.  Only the terminal UI uses
 * this; the simulation engine never sees it.
 */

#ifndef NC_H
#define NC_H

/* --- color pairs ---------------------------------------------------------
 * We allocate one curses pair per (fg,bg) combination: pair = fg*8 + bg + 1,
 * giving 1..64 (SysV curses guarantees at least 64 pairs).  A_BOLD supplies
 * the 8 "bright" tones on top of the 8 base colors.
 *
 * NC_MONO is true when no color must be emitted: a mono gfx mode (Gfx->mono,
 * e.g. "ascii") or a terminal without color support.  NC_CP then yields
 * pair 0 -- the terminal's own default colors -- so nothing on the screen is
 * ever colored or remapped; bold and reverse video still apply.  NC_MSEL(x)
 * is for selection/highlight bars: the colored attribute normally, plain
 * reverse video when there is no color to highlight with.
 */
#define NC_PAIR(fg, bg) (((fg) * 8 + (bg)) + 1)
#define NC_MONO         (Gfx->mono || !has_colors())
#define NC_CP(fg, bg)   (NC_MONO ? 0 : COLOR_PAIR(NC_PAIR((fg), (bg))))
#define NC_MSEL(colored) (NC_MONO ? A_REVERSE : (colored))

/* --- shared editor/view state (owned by nc_main.c) ----------------------- */
extern SimView *EditorView;		/* the single editor view */
extern int CursorX, CursorY;		/* cursor position in tile coords */
extern int ViewPanX, ViewPanY;		/* top-left visible tile */
extern int Quitting;

/* --- nc_gfx.c: pluggable tile-graphics modes ------------------------------
 * Each mode renders one map tile as `tilew` screen columns.  The editor loop
 * (nc_render.c) only does layout/panning and dispatches through Gfx; adding a
 * mode (braille, aalib shading, 7-bit ASCII, B&W...) = one new GfxOps entry.
 */
struct GfxOps {
  char *name;					/* "color", "unicode", ... */
  int tilew;					/* screen columns per map tile */
  int emojiui;					/* emoji faces on the tool palette */
  int mono;					/* emit no color anywhere (see NC_MONO) */
  int (*avail)();				/* runtime check (NULL = always) */
  void (*tile)();				/* (sy, sx, mapx, mapy) */
  void (*sprite)();				/* (sy, sx, type) */
  void (*cursor)();				/* (sy, sx, mapx, mapy) */
};
extern struct GfxOps *Gfx;			/* current mode */
extern struct GfxOps GfxAA;			/* nc_aa.c: aalib-style shading */
int   nc_gfx_set();			/* -gfx <name>; 0 = unknown/unavail */
void  nc_gfx_auto();			/* no -gfx: pick from TERM/locale */
void  nc_gfx_list();			/* -gfx help/list, or bad -gfx arg */
int   nc_gfx_count();			/* number of registered modes */
char *nc_gfx_name_at();			/* mode name by index, or NULL */
int   nc_gfx_avail_at();			/* 1 if usable now */
int   nc_gfx_current();			/* index of the active mode */
int   nc_gfx_select_at();			/* switch to mode i; 0 if unavailable */
void  nc_popup_snap();		/* align popup to the tile grid */
int   nc_zone_den();

/* --- nc_render.c --------------------------------------------------------- */
extern int EdTop, EdLeft, EdW, EdH;		/* editor region (screen coords) */
extern int MinimapW;				/* right minimap panel width (0=off) */
extern int ToolbarW;				/* left tool-palette width */
extern int ThemeLand;				/* land background color */
char *nc_cycle_theme();			/* Options menu: cycle land color */
void  nc_set_theme();			/* -theme tan|grass|dark */
int  nc_toolbar_hit();		/* click -> tool state, or -1 */
void nc_colors_init();
int  nc_transit_class();			/* 0 none, 1 road, 2 rail, 3 wire */
int  nc_transit_mask();	/* up/dn/lf/rt neighbor bits */
chtype nc_line_glyph();	/* ACS glyph for road/rail/wire */
chtype nc_sprite_glyph();		/* default-mode sprite chtype */
chtype nc_cell();		/* decode Map[x][y] -> glyph */
void nc_draw_editor();		/* render viewport to stdscr */
void nc_draw_toolbar();		/* vertical left tool palette */
void nc_screenshot();			/* dump stdscr as ASCII (testing) */

/* --- nc_dialogs.c -------------------------------------------------------- */
void nc_budget_modal();			/* engine callback (w_budget.c) */
void nc_eval_modal();
void nc_graph_modal();
void nc_newgame_modal();
void nc_load_modal();			/* load .cty from disk (browser) */
void nc_load_embedded_modal();		/* load a baked-in example city */
void nc_save_modal();
int  nc_prompt();
void nc_gfx_modal();			/* 'u' key / Options menu: pick a mode */

/* --- nc_menu.c ----------------------------------------------------------- */
int  nc_menu_active();
void nc_menu_enter();
int  nc_menu_key();			/* returns 1 if consumed */
int  nc_menu_mouse();		/* click; returns 1 if consumed */
void nc_menu_draw();

/* --- nc_minimap.c -------------------------------------------------------- */
void nc_draw_minimap();			/* sets MinimapW, draws side panel */
void nc_minimap_late();		/* narrow overlay, after the editor */
void nc_minimap_cycle();			/* 'm' key: cycle overlay/off */
void nc_minimap_close();			/* close button */
int  nc_minimap_on();
/* click: 0 = not ours, 1 = map grid (*tx,*ty), 2 = close button, 3 = chrome */
int  nc_minimap_hit();

/* --- nc_status.c --------------------------------------------------------- */
extern int NoticeActive;
void nc_set_status();			/* transient status-line message */
void nc_clear_status();
void nc_draw_status();		/* the bottom status line */
void nc_auto_goto();		/* engine callback */
void nc_show_notice();			/* engine callback */
void nc_draw_notice();
void nc_notice_dismiss();

/* --- nc_input.c ---------------------------------------------------------- */
extern int QueryActive;
char *nc_tool_name();
int  nc_tool_from_key();			/* letter -> tool state, or -1 */
int  nc_toolbar_count();
int  nc_toolbar_state();
int  nc_toolbar_color();
int  nc_toolbar_bg();
char nc_toolbar_key();
char *nc_toolbar_code();
char *nc_toolbar_lcode();
char *nc_toolbar_emoji();
int  nc_toolbar_gridrows();
int  nc_toolbar_rowlen();
void nc_tool_next();
void nc_tool_prev();
void nc_tool_select();
void nc_apply_tool();
int  nc_tool_cost();
void nc_draw_query();
void nc_query_dismiss();
/* engine callbacks (called from w_tool.c) */
void nc_show_zone_status();
void nc_did_tool();
#ifdef NCURSES_MOUSE_VERSION
void nc_mouse();			/* KEY_MOUSE dispatcher */
#endif

#endif /* NC_H */
