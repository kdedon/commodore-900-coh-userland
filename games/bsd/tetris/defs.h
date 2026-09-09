/*
 * Copyright 1989 Phill Everson & Martyn Shortley
 * Copyright 1989 Roberto Biancardi
 *
 * This notice and any statement of authorship must be reproduced on all
 * copies.  The full terms, including the restrictions on sale and on charging
 * a fee, are in COPYRIGHT beside this file; COHERENT port by Matt Kimmel and
 * Udo Munk.
 */
#include <stdio.h>

#define UWIDTH          10
#define UHEIGHT         22

#define WHITE	1
#define BLACK	2
#define RED	3
#define ORANGE	4
#define YELLOW	5
#define GREEN	6
#define BLUE	7
#define CYAN	8
#define VIOLET	9
#define LSIDE	10
#define RSIDE	11
#define BOTTOM	12
#define SHADOW	13
#define ULCORN	14
#define URCORN	15
#define BLCORN	16
#define BRCORN	17

/*
 * These were plain definitions in this header, so each of the five objects that
 * include it defined its own copy: ld reported six "redefined" symbols and the
 * shape table define_shapes() filled was not necessarily the one create_shape()
 * read.  Declared here, defined once in support.c.
 */
extern int	score_position;	/* position of this game in the hiscore tab */
extern int	shape_no;	/* the dripping shape */
extern int	xpos, ypos, rot;/* x, y, rotation of shape_no */
extern long	score;		/* current score (a long game passes 32767) */
extern int	rows;		/* number of rows deleted */
extern int	next_no;	/* next shape */
extern int	next_rot;	/* rotation of next shape */
extern char	*name;		/* username */

extern unsigned char grid[UWIDTH][UHEIGHT];

struct shape_table {
        int     table[4][4];
        int     width;
        int     height;
        int     offset;
        int     pointv[4];
        char    color;
};
extern struct shape_table shape[7];

struct shape {
        int     shape;
        int     rot;
        int     width;
        int     height;
        int     offset;
        int     pointv;
        char    color;
        int     was_shown;
        int     was_shadowed;
} ;
