/*********************************************/
/* you just keep on pushing my luck over the */
/*           BOULDER        DASH             */
/*                                           */
/*     Jeroen Houttuin, ETH Zurich, 1990     */
/*********************************************/

/* define bit maps */
#ifdef X11
#include "bitmap/player.bits"
#include "bitmap/player2.bits"
#include "bitmap/wall.bits"
#include "bitmap/wall2.bits"
#include "bitmap/tinkle1.bits"
#include "bitmap/tinkle2.bits"
#include "bitmap/space.bits"
#include "bitmap/grass.bits"
#include "bitmap/diamond.bits"
#include "bitmap/diamond2.bits"
#include "bitmap/steel.bits"
#include "bitmap/boulder.bits"
#include "bitmap/explosion.bits"
#include "bitmap/rmonster.bits"
#include "bitmap/rmonster2.bits"
#include "bitmap/lmonster.bits"
#include "bitmap/lmonster2.bits"
#include "bitmap/nucbal.bits"
#include "bitmap/blob.bits"
#include "bitmap/blob2.bits"
#include "bitmap/eater.bits"
#include "bitmap/eater2.bits"
#include "bitmap/exit2.bits"

#define ELEM_XSIZE player_width
#define ELEM_YSIZE player_height
#endif

#define w 35
#define h 26
#define LEVELPREFIX "xbdlev"

#define SCOREFONT "-adobe-times-bold-r-normal--18-180-75-75-p-99-iso8859-1"
#define SCORESIZE 18

#define EVMASK KeyPressMask | ExposureMask | ButtonPressMask | PointerMotionMask | FocusChangeMask

/* direction masks */
#define N 0
#define E 1
#define S 2
#define W 3
#define NODIR 4

#define SPACEEXPLO 0
#define BOULDEXPLO 10
#define DIAEXPLO 20
#define PROPAGATED 10

#define PLAYER              'p'
#define SPACE               ' '
#define LMONSTER            'l'	/* Right turning monster */
#define RMONSTER            'r'
#define GRASS               'g'
#define WALL                'w'
#define MAGICWALL           'W'	/* Expanding wall */
#define DIAMOND             'd'
#define STEEL               'S'
#define BOULDER             'b'
#define EXPLOSION           'x'
#define EXIT                'E'
#define EATER               'e'
#define NUCBAL              'n'	/* Nuclear ballon */
#define BLOB                'B'	/* lava */
#define TINKLE              't'	/* Tinkle wall */

/* Every global below is DEFINED once, by the file that sets XBD_DEFINE, and
   declared everywhere else.  They were written as tentative definitions in
   this header, which four translation units each turned into a definition. */
#ifdef XBD_DEFINE
#define EXTERN
#else
#define EXTERN extern
#endif

#ifdef MGR
typedef enum { False, True } Bool;
typedef enum
{
  XK_question='?', XK_slash='/', XK_space=' ', XK_H='H',
  XK_h='h', XK_K='K', XK_k='k', XK_J='J', XK_j='j', XK_L='L', XK_l='l'
} KeySym;

EXTERN int bit_src_op,bit_and_op;
EXTERN int ELEM_XSIZE,ELEM_YSIZE;
#endif

#ifdef X11
EXTERN Font
#endif
#ifdef MGR
EXTERN int
#endif
scorefont;	/* Font used to display score */
#ifdef X11
EXTERN GC
#endif
#ifdef MGR
EXTERN int
#endif
                whitegc, scoregc, gc, Bgc, Bgc1, Bgc2, ngc, egc, egc1,
                egc2, Egc1, Wgc, Wgc2, Egc2, Egc, lgc, lgc1, lgc2, rgc,
                rgc1, rgc2, xgc, Sgc, bgc
               ,dgc, dgc1, dgc2, wgc, pgc, pgc1, pgc2, sgc, ggc, tgc, tgc1,
                tgc2, tgc3;
EXTERN char     filename[300];	/* Current file name of this level */
EXTERN char     levname[64];	/* Levelname */
EXTERN int      i, j, ii, jj, jjj;
EXTERN int      blobbreak;
EXTERN int      critical;
EXTERN int      time_tck;	/* Current clock tick number */
EXTERN int      blobcells;
EXTERN int      tinkdur;	/* Tinkle duration */
EXTERN Bool     tinkact;	/* Tinkle active   */
EXTERN Bool     levincreased;
EXTERN int      x, y, xin, yin, players, lives, levelnum, levelstart, speed,
                diareq, diapoints, extradiapoints;
EXTERN Bool     steal;		/* steal instead of go */
EXTERN Bool     stoplevel, blobcollapse;
/* MGR's build defines KILL as a server feature switch (src/mgr/mgr.c:622),
   which would replace this enumerator with its value. */
#undef KILL

enum directs
{
  STAND, UP, DOWN, LEFT, RIGHT, KILL
};

EXTERN enum directs curorder;	/* Current order which player has */
/* typed at the keyboard. */
struct cell
{
  char            content;
  Bool            changed;	/* has cell changed since last drawing */
  Bool            caught;	/* for BLOB */
  Bool            checked;	/* for BLOB algorithm */
  char            dir;
  short           speed;
  short           stage;	/* painting stage for blinking etc. */
};

EXTERN struct cell field[h][w];

EXTERN Bool     gamestop;
EXTERN Bool     scoreobs;	/* is score line obsolete ? */
EXTERN int      score;		/* Total score */

#ifdef X11
EXTERN Display *disp;		/* X11 display of client */
EXTERN Window   wind;		/* X11 window where game is displayed */

GC              makegc();
#endif
void            make_gcs();
void            init_level();
void            draw_score();
#ifdef X11
void            xstart();
void            xend();
#endif
#ifdef MGR
void		mgrstart();
void		mgrend();
#endif
void            draw_field();
void            set_cell();
void            move_cell();
void            explode();
Bool            move_monster();
Bool            search_destroy();
void            calculate_field();
void            add_score();
