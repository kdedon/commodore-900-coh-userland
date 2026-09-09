/* nc_minimap.c  --  the overview map panel + data overlays.
 *
 * Reuses the engine's overlay classification (g_map.c: GetCI thresholds, the
 * VAL_* -> color ramp, and the reduced-resolution arrays PopDensity/CrimeMem/
 * PollutionMem/LandValueMem/TrfDensity/RateOGMem/PowerMap), rendered as a
 * compressed grid of colored cells instead of a pixmap.
 *
 * Two renderings, following the editor's graphics mode (Gfx->emojiui):
 *   default  colored ' ' blocks, plain title bar, [X] close button.  Works
 *            on any curses ever made.
 *   unicode  heavy box-drawing chrome with an emoji title and a clickable
 *            close button, braille terrain textures, box-drawing road/rail/
 *            wire lines, block-element density ramps (brighter = denser),
 *            a two-row emoji legend, and yellow corner cues marking the
 *            editor's viewport.
 *
 * The panel appears on the right when the terminal is wide enough; on a
 * narrow terminal it is drawn as a centered full overlay.  Cycled with the
 * 'm' key: off -> All -> ... -> off; the close button turns it off directly.
 */

#include "sim.h"
#include <curses.h>
#include "nc.h"

/* VAL_* levels (from g_map.c) */
#define VAL_NONE 0
#define VAL_LOW 1
#define VAL_MEDIUM 2
#define VAL_HIGH 3
#define VAL_VERYHIGH 4
#define VAL_PLUS 5
#define VAL_VERYPLUS 6
#define VAL_MINUS 7
#define VAL_VERYMINUS 8
/* power-grid pseudo-levels (PRMAP) */
#define POW_ON 9
#define POW_COND 10
#define POW_OFF 11

/* VAL_* -> curses color (bg fill); -1 means "transparent, show base tile" */
static int val_color[12] = {
  -1, COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_RED,
  COLOR_GREEN, COLOR_GREEN, COLOR_RED, COLOR_YELLOW,
  COLOR_RED, COLOR_WHITE, COLOR_CYAN
};
static int val_bold[12] = { 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0, 1 };

/* unicode mode: shading ramp per level (drawn bright-on-own-color) */
static char *val_tex[12] = {
  "", "\342\226\221", "\342\226\222", "\342\226\223", "\342\226\210", "\342\226\222", "\342\226\210", "\342\226\222", "\342\226\210", "\342\226\210", "\342\226\222", "\342\226\221"
};

static short
get_ci(x)
short x;
{
  if (x < 50)  return VAL_NONE;
  if (x < 100) return VAL_LOW;
  if (x < 150) return VAL_MEDIUM;
  if (x < 200) return VAL_HIGH;
  return VAL_VERYHIGH;
}

/* the overlay modes we cycle through (engine map_state constants) */
static struct { int state; char *label; char *emoji; } modes[] = {
  { ALMAP, "All",          "\360\237\214\206" },
  { PDMAP, "Population",   "\360\237\221\245" },
  { RGMAP, "Growth Rate",  "\360\237\223\210" },
  { TDMAP, "Traffic",      "\360\237\232\227" },
  { PLMAP, "Pollution",    "\360\237\222\250" },
  { CRMAP, "Crime",        "\360\237\232\223" },
  { LVMAP, "Land Value",   "\360\237\222\260" },
  { PRMAP, "Power Grid",   "\342\232\241" },
  { FIMAP, "Fire Cover",   "\360\237\232\222" },
  { POMAP, "Police Cover", "\360\237\221\256" }
};
#define NMODES ((int)(sizeof(modes) / sizeof(modes[0])))

static int MinimapMode = -1;		/* -1 = off, else index into modes[] */

int
nc_minimap_on()
{
  return MinimapMode >= 0;
}

/* 'm' key: advance off -> mode0 -> ... -> last -> off */
void
nc_minimap_cycle()
{
  MinimapMode++;
  if (MinimapMode >= NMODES) MinimapMode = -1;
}

/* the [X] close button */
void
nc_minimap_close()
{
  MinimapMode = -1;
}

/* base tile color for the overview (curses bg color) */
static int
base_color(tx, ty)
int tx;
int ty;
{
  int t = Map[tx][ty] & LOMASK;

  if (t == DIRT) return COLOR_BLACK;
  if (t >= RIVER && t <= LASTRIVEDGE) return COLOR_BLUE;
  if (t >= TREEBASE && t <= WOODS5) return COLOR_GREEN;
  if (t >= ROADBASE && t <= LASTROAD) return COLOR_WHITE;
  if (t >= POWERBASE && t <= LASTPOWER) return COLOR_YELLOW;
  if (t >= RAILBASE && t <= LASTRAIL) return COLOR_WHITE;
  if (t >= RESBASE && t < COMBASE) return COLOR_GREEN;
  if (t >= COMBASE && t < INDBASE) return COLOR_BLUE;
  if (t >= INDBASE && t < PORTBASE) return COLOR_YELLOW;
  if (t >= PORTBASE && t <= LASTPOWERPLANT) return COLOR_CYAN;
  if (t >= FIRESTBASE && t <= POLICESTATION) return COLOR_RED;
  if (t >= FIREBASE && t <= LASTFIRE) return COLOR_RED;
  return COLOR_BLACK;
}

/* overlay level for a tile: a VAL_ or POW_ level, or -1 = show the base tile */
static int
overlay_val(state, tx, ty)
int state;
int tx;
int ty;
{
  int hx = tx >> 1, hy = ty >> 1;	/* half-res arrays */
  int sx = tx >> 3, sy = ty >> 3;	/* eighth-res arrays */
  int z, t;

  switch (state) {
  case PDMAP: return get_ci(PopDensity[hx][hy]);
  case TDMAP: return get_ci(TrfDensity[hx][hy]);
  case PLMAP: return get_ci(10 + PollutionMem[hx][hy]);
  case CRMAP: return get_ci(CrimeMem[hx][hy]);
  case LVMAP: return get_ci(LandValueMem[hx][hy]);
  case FIMAP: return get_ci(FireRate[sx][sy]);
  case POMAP: return get_ci(PoliceMapEffect[sx][sy]);
  case RGMAP:
    z = RateOGMem[sx][sy];
    if (z > 100) return VAL_VERYPLUS;
    if (z > 20) return VAL_PLUS;
    if (z < -100) return VAL_VERYMINUS;
    if (z < -20) return VAL_MINUS;
    return VAL_NONE;
  case PRMAP:
    t = Map[tx][ty];
    if (!(t & ZONEBIT) && !(t & CONDBIT)) return -1;
    if (t & PWRBIT) return POW_ON;			/* powered */
    if (t & CONDBIT) return POW_COND;			/* conductive */
    return POW_OFF;					/* unpowered */
  }
  return -1;			/* ALMAP: base only */
}

/* last-drawn geometry, for mouse hit-testing (PW=0 when hidden) */
static int PTop, PLeft, PW, PH;		/* whole panel */
static int GTop, GLeft, GW, GH;		/* inner map grid */
static int CloseY, CloseX, CloseW;	/* close button */

/*
 * Classify a click on the panel: 0 = not ours, 1 = map grid (*tx,*ty set),
 * 2 = close button, 3 = chrome (swallowed, so clicks on the overlay never
 * leak to the editor underneath).
 */
int
nc_minimap_hit(y, x, tx, ty)
int y;
int x;
int *tx;
int *ty;
{
  if (MinimapMode < 0 || PW <= 0) return 0;
  if (CloseW > 0 && y == CloseY && x >= CloseX && x < CloseX + CloseW)
    return 2;
  if (GW > 0 && y >= GTop && y < GTop + GH &&
      x >= GLeft && x < GLeft + GW) {
    *tx = (x - GLeft) * WORLD_X / GW;
    *ty = (y - GTop) * WORLD_Y / GH;
    return 1;
  }
  if (y >= PTop && y < PTop + PH && x >= PLeft && x < PLeft + PW)
    return 3;
  return 0;
}

/* ===================== default mode (colored blocks) ===================== */

/* Monochrome cell, used when no color is emitted (NC_MONO: the ascii gfx
 * mode, or a terminal without colors -- the colored ' ' blocks would be
 * invisible): the overlay as a character intensity ramp, the base map as
 * tiny tile classes.  Mirrors val_color/base_color one to one. */
static chtype
mono_cell(v, tx, ty)
int v;
int tx;
int ty;
{
  static char vch[12] = {
    ' ', '.', '+', '*', '#',	/* NONE LOW MEDIUM HIGH VERYHIGH */
    '+', '#', '-', '=',		/* PLUS VERYPLUS MINUS VERYMINUS */
    '#', '+', 'x'		/* POW_ON POW_COND POW_OFF */
  };
  int t;

  if (v > 0) {
    if (v == POW_OFF) return ((chtype)'x') | A_BOLD | A_REVERSE;
    return ((chtype)vch[v]) | (val_bold[v] ? A_BOLD : 0);
  }
  t = Map[tx][ty] & LOMASK;
  if (t >= RIVER && t <= LASTRIVEDGE) return (chtype)'~';
  if (t >= TREEBASE && t <= WOODS5) return (chtype)'^';
  if (t >= ROADBASE && t <= LASTROAD) return (chtype)'+';
  if (t >= POWERBASE && t <= LASTPOWER) return (chtype)'=';
  if (t >= RAILBASE && t <= LASTRAIL) return (chtype)'+';
  if (t >= RESBASE && t < COMBASE) return (chtype)'r';
  if (t >= COMBASE && t < INDBASE) return (chtype)'c';
  if (t >= INDBASE && t < PORTBASE) return (chtype)'i';
  if (t >= PORTBASE && t <= LASTPOWERPLANT) return ((chtype)'#') | A_BOLD;
  if (t >= FIRESTBASE && t <= POLICESTATION) return (chtype)'#';
  if (t >= FIREBASE && t <= LASTFIRE) return ((chtype)'!') | A_BOLD | A_REVERSE;
  return (chtype)' ';
}

static void
draw_ascii(top, left, w, h)
int top;
int left;
int w;
int h;
{
  int iw = w - 2, ih = h - 2;
  int sx, sy, tx, ty, color, bold, v, i;
  int state = modes[MinimapMode].state;
  char title[64];

  if (iw < 4 || ih < 3) { PW = 0; return; }
  GTop = top + 1; GLeft = left + 1; GW = iw; GH = ih;

  /* border + title bar + close button */
  attrset(NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD);
  for (i = 0; i < w; i++) { mvaddch(top, left + i, ' '); mvaddch(top + h - 1, left + i, ' '); }
  sprintf(title, " Map: %s ", modes[MinimapMode].label);
  mvaddnstr(top, left + 1, title, w - 5);
  CloseY = top; CloseX = left + w - 4; CloseW = 3;
  mvaddstr(CloseY, CloseX, "[X]");

  if (NC_MONO) attrset(A_NORMAL);
  for (sy = 0; sy < ih; sy++) {
    /* own the side columns too: as a fallback overlay this box sits on live
     * tiles, and a row that skips them leaves half-covered wide glyphs */
    attrset(A_NORMAL);
    mvaddch(top + 1 + sy, left, ' ');
    mvaddch(top + 1 + sy, left + w - 1, ' ');
    ty = sy * WORLD_Y / ih;
    if (ty >= WORLD_Y) ty = WORLD_Y - 1;
    for (sx = 0; sx < iw; sx++) {
      tx = sx * WORLD_X / iw;
      if (tx >= WORLD_X) tx = WORLD_X - 1;

      v = overlay_val(state, tx, ty);
      if (NC_MONO) {			/* char ramp; bg fills are invisible */
	mvaddch(top + 1 + sy, left + 1 + sx, mono_cell(v, tx, ty));
	continue;
      }
      color = (v > 0) ? val_color[v] : -1;
      bold = (v > 0) ? val_bold[v] : 0;
      if (color < 0) { color = base_color(tx, ty); bold = 0; }

      attrset(NC_CP(COLOR_BLACK, color) | (bold ? A_BOLD : 0));
      mvaddch(top + 1 + sy, left + 1 + sx, ' ');
    }
  }

  attrset(A_NORMAL);
}

/* ================== unicode mode (textured, emoji legend) ================= */

/* one 1-column map cell */
static void
ucell(y, x, s, fg, bg, attr)
int y;
int x;
char *s;
int fg;
int bg;
int attr;
{
  attrset(NC_CP(fg, bg) | attr);
  mvaddstr(y, x, s);
}

/* 1-column box-drawing transit pieces, indexed by nc_transit_mask
 * (up<<3|dn<<2|lf<<1|rt).  Roads heavy, rails double, power lines light. */
static char *u_road[16] = {
  "\342\225\213", "\342\224\201", "\342\224\201", "\342\224\201", "\342\224\203", "\342\224\217", "\342\224\223", "\342\224\263",
  "\342\224\203", "\342\224\227", "\342\224\233", "\342\224\273", "\342\224\203", "\342\224\243", "\342\224\253", "\342\225\213"
};
static char *u_rail[16] = {
  "\342\225\254", "\342\225\220", "\342\225\220", "\342\225\220", "\342\225\221", "\342\225\224", "\342\225\227", "\342\225\246",
  "\342\225\221", "\342\225\232", "\342\225\235", "\342\225\251", "\342\225\221", "\342\225\240", "\342\225\243", "\342\225\254"
};
static char *u_wire[16] = {
  "\342\224\274", "\342\224\200", "\342\224\200", "\342\224\200", "\342\224\202", "\342\224\214", "\342\224\220", "\342\224\254",
  "\342\224\202", "\342\224\224", "\342\224\230", "\342\224\264", "\342\224\202", "\342\224\234", "\342\224\244", "\342\224\274"
};

/* braille terrain textures + the zone density ramp (brighter = denser) */
static char *u_dirt[4] = { "\342\240\220", "\342\240\204", "\342\240\202", "\342\240\240" };
static char *u_wave[4] = { "\342\243\200", "\342\240\244", "\342\240\264", "\342\240\246" };
static char *u_tree[4] = { "\342\240\233", "\342\240\277", "\342\240\276", "\342\240\267" };
static char *u_ramp[5] = { "\342\226\221", "\342\226\222", "\342\226\223", "\342\226\210", "\342\226\210" };

/* base tile, textured (mirrors uni_tile's classification, 1 column wide) */
static void
u_base(y, x, tx, ty)
int y;
int x;
int tx;
int ty;
{
  int t = Map[tx][ty];
  int h = tx * 73 + ty * 179;		/* stable per-cell hash for variety */
  int cls, den, vac;

  if ((t & LOMASK) >= TILE_COUNT) t -= TILE_COUNT;
  t &= LOMASK;

  cls = nc_transit_class(t);
  if (cls == 1) { ucell(y, x, u_road[nc_transit_mask(tx, ty, 1)], COLOR_WHITE, COLOR_BLACK, 0); return; }
  if (cls == 2) { ucell(y, x, u_rail[nc_transit_mask(tx, ty, 2)], COLOR_WHITE, COLOR_BLACK, A_BOLD); return; }
  if (cls == 3) { ucell(y, x, u_wire[nc_transit_mask(tx, ty, 3)], COLOR_RED, COLOR_BLACK, A_BOLD); return; }

  if (t == DIRT) { ucell(y, x, u_dirt[h & 3], COLOR_BLACK, COLOR_BLACK, A_BOLD); return; }
  if (t >= RIVER && t <= LASTRIVEDGE) { ucell(y, x, u_wave[h & 3], COLOR_CYAN, COLOR_BLUE, 0); return; }
  if (t >= TREEBASE && t <= WOODS5) { ucell(y, x, u_tree[h & 3], COLOR_GREEN, COLOR_GREEN, A_BOLD); return; }
  if (t >= RUBBLE && t <= LASTRUBBLE) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_BLACK, A_BOLD); return; }
  if (t >= FLOOD && t <= LASTFLOOD) { ucell(y, x, u_wave[h & 3], COLOR_WHITE, COLOR_CYAN, A_BOLD); return; }
  if (t == RADTILE) { ucell(y, x, "\342\230\242", COLOR_YELLOW, COLOR_MAGENTA, A_BOLD); return; }
  if (t >= FIREBASE && t <= LASTFIRE) { ucell(y, x, "\342\226\223", COLOR_YELLOW, COLOR_RED, A_BOLD); return; }

  if (t >= HOSPITAL - 4 && t <= HOSPITAL + 4) { ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_RED, 0); return; }
  if (t >= CHURCH - 4 && t <= CHURCH + 4) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_CYAN, 0); return; }

  /* R/C/I zones: the block ramp brightens as the zone densifies */
  if (t >= RESBASE && t < COMBASE) {
    den = nc_zone_den(t, RZB - 4, HOSPITAL - 5, 4, &vac);
    if (t >= LHTHR && t <= HHTHR) den = 1;	/* single family houses */
    if (den < 0) den = 0;
    ucell(y, x, u_ramp[den], COLOR_CYAN, COLOR_CYAN, A_BOLD);
    return;
  }
  if (t >= COMBASE && t < INDBASE) {
    den = nc_zone_den(t, CZB - 4, INDBASE - 1, 5, &vac);
    if (den < 0) den = 0;
    ucell(y, x, u_ramp[den], COLOR_BLUE, COLOR_BLUE, A_BOLD);
    return;
  }
  if (t >= INDBASE && t < PORTBASE) {
    den = nc_zone_den(t, IZB - 4, PORTBASE - 1, 4, &vac);
    if (den < 0) den = 0;
    ucell(y, x, u_ramp[den], COLOR_MAGENTA, COLOR_MAGENTA, A_BOLD);
    return;
  }

  if (t >= PORTBASE && t <= LASTPORT) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_CYAN, 0); return; }
  if (t >= RADAR0 && t <= RADAR7) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_WHITE, 0); return; }
  if (t >= AIRPORTBASE && t < COALBASE) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_WHITE, 0); return; }
  if (t >= COALBASE && t <= LASTPOWERPLANT) { ucell(y, x, "\342\226\223", COLOR_YELLOW, COLOR_RED, A_BOLD); return; }
  if (t >= FIRESTBASE && t < POLICESTBASE) { ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_RED, 0); return; }
  if (t >= POLICESTBASE && t < STADIUMBASE) { ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_BLUE, 0); return; }
  if (t >= FOOTBALLGAME1 && t <= FOOTBALLGAME2 + 7) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_WHITE, 0); return; }
  if (t >= STADIUMBASE && t < NUCLEARBASE) { ucell(y, x, "\342\226\222", COLOR_BLACK, COLOR_WHITE, 0); return; }
  if (t >= NUCLEARBASE && t <= LASTZONE) { ucell(y, x, "\342\230\242", COLOR_YELLOW, COLOR_GREEN, A_BOLD); return; }

  if ((t >= HBRDG0 && t <= HBRDG3) || (t >= VBRDG0 && t <= VBRDG3)) {
    ucell(y, x, "\342\224\201", COLOR_WHITE, COLOR_BLUE, 0);	/* open drawbridge */
    return;
  }
  if (t >= FOUNTAIN && t <= INDBASE2) { ucell(y, x, "\342\226\221", COLOR_WHITE, COLOR_GREEN, A_BOLD); return; }
  if (t >= TINYEXP && t <= LASTTINYEXP) { ucell(y, x, "\342\226\210", COLOR_YELLOW, COLOR_RED, A_BOLD); return; }
  if (t >= SMOKEBASE && t < TINYEXP) { ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_BLACK, A_BOLD); return; }
  if (t >= COALSMOKE1 && t <= COALSMOKE4 + 3) { ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_RED, A_BOLD); return; }

  ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_BLACK, 0);	/* unknown tile */
}

/* overlay cell: shading ramp drawn bright-on-its-own-color, so intensity
 * reads as brightness; the power grid keeps its wires as line drawing */
static void
u_over(y, x, tx, ty, v)
int y;
int x;
int tx;
int ty;
int v;
{
  int c = val_color[v];
  int t = Map[tx][ty] & LOMASK;

  if (v >= POW_ON) {
    if (nc_transit_class(t) == 3) {
      ucell(y, x, u_wire[nc_transit_mask(tx, ty, 3)],
	    (v == POW_ON) ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, A_BOLD);
      return;
    }
    if (v == POW_ON) { ucell(y, x, "\342\226\210", COLOR_RED, COLOR_RED, A_BOLD); return; }
    if (v == POW_OFF) { ucell(y, x, "\342\226\221", COLOR_CYAN, COLOR_CYAN, A_BOLD); return; }
    ucell(y, x, "\342\226\222", COLOR_WHITE, COLOR_BLACK, A_BOLD);	/* conductive */
    return;
  }
  ucell(y, x, val_tex[v], c, c, A_BOLD);
}

/* editor viewport cue: yellow corners over the grid.  The editor's extents
 * are recomputed from the layout formula (nc_draw_editor, nc_render.c) with
 * the MinimapW we just set, because the minimap draws before the editor and
 * the EdW/EdH globals still hold the previous frame's values. */
static void
u_viewport()
{
  int ew = COLS - ToolbarW - MinimapW - 1;
  int eh = LINES - 2;
  int vt = ew / Gfx->tilew;
  int x0 = ViewPanX * GW / WORLD_X;
  int y0 = ViewPanY * GH / WORLD_Y;
  int x1 = (ViewPanX + vt) * GW / WORLD_X - 1;
  int y1 = (ViewPanY + eh) * GH / WORLD_Y - 1;

  if (x1 >= GW) x1 = GW - 1;
  if (y1 >= GH) y1 = GH - 1;
  if (x1 < x0) x1 = x0;
  if (y1 < y0) y1 = y0;
  attrset(NC_CP(COLOR_YELLOW, COLOR_BLACK) | A_BOLD);
  mvaddstr(GTop + y0, GLeft + x0, "\342\224\217");
  mvaddstr(GTop + y0, GLeft + x1, "\342\224\223");
  mvaddstr(GTop + y1, GLeft + x0, "\342\224\227");
  mvaddstr(GTop + y1, GLeft + x1, "\342\224\233");
}

/* legend items: glyph + label pairs, clipped at the panel edge */
static int LegY, LegX, LegEnd;

static void
leg(s, cols, fg, attr)
char *s;
int cols;
int fg;
int attr;
{
  if (LegX + cols > LegEnd) return;
  attrset(NC_CP(fg, COLOR_BLACK) | attr);
  mvaddstr(LegY, LegX, s);
  LegX += cols;
}

static void
u_legend(state, y, left, w)
int state;
int y;
int left;
int w;
{
  LegEnd = left + w - 1;

  LegY = y;
  LegX = left + 2;
  switch (state) {
  case ALMAP:
    leg("\360\237\217\240", 2, COLOR_WHITE, 0); leg("Res ", 4, COLOR_CYAN, A_BOLD);
    leg("\360\237\217\254", 2, COLOR_WHITE, 0); leg("Com ", 4, COLOR_BLUE, A_BOLD);
    leg("\360\237\217\255", 2, COLOR_WHITE, 0); leg("Ind", 3, COLOR_MAGENTA, A_BOLD);
    break;
  case RGMAP:
    leg("\360\237\223\210 ", 3, COLOR_WHITE, 0);
    leg("\342\226\222", 1, COLOR_GREEN, 0); leg("\342\226\210", 1, COLOR_GREEN, A_BOLD);
    leg(" Growing", 8, COLOR_WHITE, 0);
    break;
  case PRMAP:
    leg("\342\232\241 ", 3, COLOR_WHITE, 0);
    leg("\342\226\210", 1, COLOR_RED, A_BOLD);
    leg(" Powered", 8, COLOR_WHITE, 0);
    break;
  default:			/* the four-step density ramp overlays */
    leg("\342\226\221", 1, COLOR_WHITE, 0); leg(" Low  ", 6, COLOR_WHITE, 0);
    leg("\342\226\222", 1, COLOR_YELLOW, 0); leg(" Medium", 7, COLOR_WHITE, 0);
    break;
  }

  LegY = y + 1;
  LegX = left + 2;
  switch (state) {
  case ALMAP:
    /* even label widths keep every emoji on the tile-grid parity */
    leg("\360\237\214\262", 2, COLOR_WHITE, 0); leg("Park  ", 6, COLOR_GREEN, A_BOLD);
    leg("\360\237\214\212", 2, COLOR_WHITE, 0); leg("Water ", 6, COLOR_CYAN, 0);
    leg("\360\237\224\245", 2, COLOR_WHITE, 0); leg("Fire", 4, COLOR_RED, A_BOLD);
    break;
  case RGMAP:
    leg("\360\237\223\211 ", 3, COLOR_WHITE, 0);
    leg("\342\226\222", 1, COLOR_RED, 0); leg("\342\226\210", 1, COLOR_YELLOW, A_BOLD);
    leg(" Declining", 10, COLOR_WHITE, 0);
    break;
  case PRMAP:
    leg("\342\235\214 ", 3, COLOR_WHITE, 0);
    leg("\342\226\221", 1, COLOR_CYAN, A_BOLD);
    leg(" No power", 9, COLOR_WHITE, 0);
    break;
  default:
    leg("\342\226\223", 1, COLOR_RED, 0); leg(" High ", 6, COLOR_WHITE, 0);
    leg("\342\226\210", 1, COLOR_RED, A_BOLD); leg(" Peak", 5, COLOR_WHITE, 0);
    break;
  }
}

/*
 * Panel layout: heavy box; the map grid fills rows 1..h-6, then a separator
 * and a two-row legend above the bottom border.  Title (with the mode emoji)
 * and the close button are embedded in the top border.
 */
static void
draw_uni(top, left, w, h)
int top;
int left;
int w;
int h;
{
  int iw = w - 2, gh = h - 5;
  int cx, cy, tx, ty, v, i, n, m;
  int state = modes[MinimapMode].state;
  char buf[80];

  /* below 13 wide the truncated title would collide with the close button */
  if (iw < 11 || gh < 3) { draw_ascii(top, left, w, h); return; }
  GTop = top + 1; GLeft = left + 1; GW = iw; GH = gh;

  /* chrome */
  attrset(NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD);
  mvaddstr(top, left, "\342\224\217");
  for (i = 1; i < w - 1; i++) addstr("\342\224\201");
  addstr("\342\224\223");
  for (i = 1; i < h - 1; i++) {
    mvaddstr(top + i, left, "\342\224\203");
    mvaddstr(top + i, left + w - 1, "\342\224\203");
  }
  mvaddstr(top + 1 + gh, left, "\342\224\243");
  for (i = 1; i < w - 1; i++) addstr("\342\224\201");
  addstr("\342\224\253");
  mvaddstr(top + h - 1, left, "\342\224\227");
  for (i = 1; i < w - 1; i++) addstr("\342\224\201");
  addstr("\342\224\233");
  attrset(NC_CP(COLOR_WHITE, COLOR_BLACK));
  for (i = 1; i < w - 1; i++) {			/* clear the legend rows */
    mvaddch(top + 2 + gh, left + i, ' ');
    mvaddch(top + 3 + gh, left + i, ' ');
  }

  /* title, with the emoji when it fits */
  attrset(NC_CP(COLOR_WHITE, COLOR_BLACK) | A_BOLD);
  n = (int)strlen(modes[MinimapMode].label);
  if (n + 7 <= w - 8) {
    sprintf(buf, "\342\224\253 %s %s \342\224\243", modes[MinimapMode].emoji,
	    modes[MinimapMode].label);
  } else {
    m = w - 12;
    if (m > n) m = n;
    if (m < 1) m = 1;
    sprintf(buf, "\342\224\253 %.*s \342\224\243", m, modes[MinimapMode].label);
  }
  mvaddstr(top, left + 2, buf);

  CloseY = top; CloseX = left + w - 6; CloseW = 4;
  CloseX += ((CloseX + 1) ^ EdLeft) & 1;	/* land on the tile-grid parity */
  mvaddstr(CloseY, CloseX, "\342\224\253\342\235\214\342\224\243");

  /* the map grid */
  for (cy = 0; cy < gh; cy++) {
    ty = cy * WORLD_Y / gh;
    if (ty >= WORLD_Y) ty = WORLD_Y - 1;
    for (cx = 0; cx < iw; cx++) {
      tx = cx * WORLD_X / iw;
      if (tx >= WORLD_X) tx = WORLD_X - 1;
      v = overlay_val(state, tx, ty);
      if (v > 0) u_over(GTop + cy, GLeft + cx, tx, ty, v);
      else       u_base(GTop + cy, GLeft + cx, tx, ty);
    }
  }

  u_viewport();
  u_legend(state, top + 2 + gh, left, w);
  attrset(A_NORMAL);
}

static void
draw_grid(top, left, w, h)
int top;
int left;
int w;
int h;
{
  PTop = top; PLeft = left; PW = w; PH = h;
  GW = 0; CloseW = 0;
  if (Gfx->emojiui) draw_uni(top, left, w, h);
  else draw_ascii(top, left, w, h);
}

/*
 * Public entry: lay out and draw the minimap if active.  Sets MinimapW so the
 * editor viewport shrinks to make room (side panel) when the terminal is wide.
 * On a narrow terminal the panel is a centered overlay ON TOP of the editor,
 * so it cannot be painted here (the editor draws after us and would repaint
 * over it); nc_minimap_late() draws it once the editor is done.
 */
static int LatePending;

void
nc_draw_minimap()
{
  int rows, cols, pw, ph, maxw;

  MinimapW = 0;
  LatePending = 0;
  if (MinimapMode < 0) { PW = 0; return; }

  getmaxyx(stdscr, rows, cols);

  if (cols >= 70) {
    /* side panel on the right */
    pw = cols / 3;
    maxw = Gfx->emojiui ? (WORLD_X / 2 + 2) : 44;
    if (pw > maxw) pw = maxw;
    if (pw < 22) pw = 22;
    /* panel edge on the tile-grid parity, so later popups that overlap the
     * panel never bisect its emoji chrome (title, close, legend) */
    if (Gfx->tilew >= 2 && ((cols - pw - EdLeft) & 1)) pw++;
    MinimapW = pw;
    ph = rows - 2;			/* menu (1) + status line (1) */
    draw_grid(1, cols - pw, pw, ph);
  } else {
    LatePending = 1;
  }
}

void
nc_minimap_late()
{
  int rows, cols, px, pw, ph;

  if (!LatePending) return;

  /* narrow terminal: centered overlay over the editor */
  getmaxyx(stdscr, rows, cols);
  px = 1;
  pw = cols - 2;
  ph = rows - 5;
  if (ph < 5) ph = 5;
  nc_popup_snap(&px, &pw);
  draw_grid(1, px, pw, ph);
}
