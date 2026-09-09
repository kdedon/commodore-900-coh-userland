

#include "include.h"


int	errors = 0;
int	line = 1;

#include "grammar.h"
#define YYCLEARIN yychar = -1000
#define YYERROK yyerrflag = 0
extern int yychar;
extern short yyerrflag;
#ifndef YYMAXDEPTH
#define YYMAXDEPTH 150
#endif
YYSTYPE yyval, yylval;



void
check_edge(x, y)
	int x, y;
{
	if (!(x == 0) && !(x == sp->width - 1) && 
	    !(y == 0) && !(y == sp->height - 1))
		yyerror("edge value not on edge.");
}

void
check_point(x, y)
	int x, y;
{
	if (x < 1 || x >= sp->width - 1)
		yyerror("X value out of range.");
	if (y < 1 || y >= sp->height - 1)
		yyerror("Y value out of range.");
}

void
check_linepoint(x, y)
	int x, y;
{
	if (x < 0 || x >= sp->width)
		yyerror("X value out of range.");
	if (y < 0 || y >= sp->height)
		yyerror("Y value out of range.");
}

void
check_line(x1, y1, x2, y2)
	int x1, y1, x2, y2;
{
	int	d1, d2;

	check_linepoint(x1, y1);
	check_linepoint(x2, y2);

	d1 = ABS(x2 - x1);
	d2 = ABS(y2 - y1);

	if (!(d1 == d2) && !(d1 == 0) && !(d2 == 0))
		yyerror("Bad line endpoints.");
}

int
yyerror(s)
	 char *s;
{
	fprintf(stderr, "\"%s\": line %d: %s\n", file, line, s);
	errors++;

	return (errors);
}

void
check_edir(x, y, dir)
	int x, y, dir;
{
	int	bad = 0;

	if (x == sp->width - 1)
		x = 2;
	else if (x != 0)
		x = 1;
	if (y == sp->height - 1)
		y = 2;
	else if (y != 0)
		y = 1;
	
	switch (x * 10 + y) {
	case 00: if (dir != 3) bad++; break;
	case 01: if (dir < 1 || dir > 3) bad++; break;
	case 02: if (dir != 1) bad++; break;
	case 10: if (dir < 3 || dir > 5) bad++; break;
	case 11: break;
	case 12: if (dir > 1 && dir < 7) bad++; break;
	case 20: if (dir != 5) bad++; break;
	case 21: if (dir < 5) bad++; break;
	case 22: if (dir != 7) bad++; break;
	default:
		yyerror("Unknown value in checkdir!  Get help!");
		break;
	}
	if (bad)
		yyerror("Bad direction for entrance at exit.");
}

int
checkdefs()
{
	int	err = 0;

	if (sp->width == 0) {
		yyerror("'width' undefined.");
		err++;
	}
	if (sp->height == 0) {
		yyerror("'height' undefined.");
		err++;
	}
	if (sp->update_secs == 0) {
		yyerror("'update' undefined.");
		err++;
	}
	if (sp->newplane_time == 0) {
		yyerror("'newplane' undefined.");
		err++;
	}
	if (err)
		return (-1);
	else
		return (0);
}
#ifdef YYTNAMES
readonly struct yytname yytnames[20] =
{
	"$end", -1, 
	"error", -2, 
	"HeightOp", 256, 
	"WidthOp", 257, 
	"UpdateOp", 258, 
	"NewplaneOp", 259, 
	"DirOp", 260, 
	"ConstOp", 261, 
	"LineOp", 262, 
	"AirportOp", 263, 
	"BeaconOp", 264, 
	"ExitOp", 265, 
	"'='", 61, 
	"';'", 59, 
	"':'", 58, 
	"'('", 40, 
	"')'", 41, 
	"'['", 91, 
	"']'", 93, 
	NULL
} ;
#endif
#include <action.h>
unsigned char yypdnt[31] = {
0, 3, 1, 2, 2, 5, 5, 5,
5, 6, 7, 9, 8, 4, 4, 10,
10, 10, 10, 11, 11, 15, 12, 12,
16, 14, 14, 17, 13, 13, 18 
};
unsigned char yypn[31] = {
2, 0, 3, 2, 1, 1, 1, 1,
1, 4, 4, 4, 4, 2, 1, 4,
4, 4, 4, 2, 1, 4, 2, 1,
5, 2, 1, 5, 2, 1, 10 
};
unsigned char yypgo[19] = {
0, 0, 2, 6, 8, 12, 14, 16,
18, 20, 22, 24, 28, 32, 36, 40,
42, 44, 46 
};
unsigned int yygo[48] = {
-1000, 5, 7, 18, -1000, 6, -1000, 17,
28, 37, -1000, 27, -1000, 7, -1000, 8,
-1000, 9, -1000, 10, -1000, 11, -1000, 28,
46, 58, -1000, 45, 49, 61, -1000, 48,
40, 52, -1000, 39, 43, 55, -1000, 42,
-1000, 46, -1000, 49, -1000, 43, -1000, 40
};
unsigned short yypa[78] = {
0, 10, 14, 18, 22, 26, 30, 32,
42, 44, 46, 48, 50, 54, 58, 62,
66, 70, 80, 82, 86, 90, 94, 98,
102, 106, 110, 114, 116, 126, 128, 130,
132, 134, 138, 142, 146, 150, 152, 156,
160, 164, 168, 172, 176, 180, 184, 188,
192, 196, 200, 204, 206, 208, 212, 214,
216, 220, 222, 224, 228, 230, 232, 236,
240, 244, 248, 252, 256, 258, 262, 266,
268, 270, 274, 278, 282, 286 
};
unsigned int yyact[288] = {
1, 256, 2, 257, 3, 258, 4, 259,
24576, -1000, 12, 61, 24576, -1000, 13, 61,
24576, -1000, 14, 61, 24576, -1000, 15, 61,
24576, -1000, 16, -1, 24576, -1000, 8193, -1000,
1, 256, 2, 257, 3, 258, 4, 259,
8196, -1000, 8197, -1000, 8198, -1000, 8199, -1000,
8200, -1000, 19, 261, 24576, -1000, 20, 261,
24576, -1000, 21, 261, 24576, -1000, 22, 261,
24576, -1000, 16384, -1, 24576, -1000, 23, 262,
24, 263, 25, 264, 26, 265, 24576, -1000,
8195, -1000, 29, 59, 24576, -1000, 30, 59,
24576, -1000, 31, 59, 24576, -1000, 32, 59,
24576, -1000, 33, 58, 24576, -1000, 34, 58,
24576, -1000, 35, 58, 24576, -1000, 36, 58,
24576, -1000, 8194, -1000, 23, 262, 24, 263,
25, 264, 26, 265, 8206, -1000, 8203, -1000,
8204, -1000, 8201, -1000, 8202, -1000, 38, 91,
24576, -1000, 41, 40, 24576, -1000, 44, 40,
24576, -1000, 47, 40, 24576, -1000, 8205, -1000,
50, 40, 24576, -1000, 51, 59, 24576, -1000,
38, 91, 8221, -1000, 53, 261, 24576, -1000,
54, 59, 24576, -1000, 41, 40, 8218, -1000,
56, 261, 24576, -1000, 57, 59, 24576, -1000,
44, 40, 8212, -1000, 59, 261, 24576, -1000,
60, 59, 24576, -1000, 47, 40, 8215, -1000,
62, 261, 24576, -1000, 8209, -1000, 8220, -1000,
63, 261, 24576, -1000, 8210, -1000, 8217, -1000,
64, 261, 24576, -1000, 8207, -1000, 8211, -1000,
65, 261, 24576, -1000, 8208, -1000, 8214, -1000,
66, 261, 24576, -1000, 67, 260, 24576, -1000,
68, 41, 24576, -1000, 69, 260, 24576, -1000,
70, 41, 24576, -1000, 71, 41, 24576, -1000,
8213, -1000, 72, 41, 24576, -1000, 73, 40,
24576, -1000, 8219, -1000, 8216, -1000, 74, 261,
24576, -1000, 75, 261, 24576, -1000, 76, 41,
24576, -1000, 77, 93, 24576, -1000, 8222, -1000
};
/* (-lgl
 * 	COHERENT Version 3.2.2
 * 	Copyright (c) 1982, 1992 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * /lib/yyparse.c
 */

#define	YYNOCHAR	(-1000)
#define	yyerrok		yyerrflag=0
#define	yyclearin	yylval=YYNOCHAR

int	yychar;
short	yyerrflag;
int	*yys;
int	yystack[YYMAXDEPTH];
YYSTYPE	yyvstack[YYMAXDEPTH];
YYSTYPE	*yyv;

#ifdef	YYDEBUG
int	yydebug = 1;	/* No sir, not in the BSS */
#include <stdio.h>
#endif

yyparse()
{
	register YYSTYPE *yypvt;
	int act;
	register unsigned *ip, yystate;
	int pno;

	yystate = 0;
	yychar = YYNOCHAR;
	yyv = &yyvstack[-1];
	yys = &yystack[-1];

stack:
	if( ++yys >= &yystack[YYMAXDEPTH] ) {
		write(2, "Stack overflow\n", 15);
		exit(1);
	}
	*yys = yystate;
	*++yyv = yyval;
#ifdef YYDEBUG
	if( yydebug )
		fprintf(stdout, "Stack state %d, char %d\n", yystate, yychar);
#endif

read:
	ip = &yyact[yypa[yystate]];
	if( ip[1] != YYNOCHAR ) {
		if( yychar == YYNOCHAR ) {
			yychar = yylex();
#ifdef YYDEBUG
			if( yydebug )
				fprintf(stdout, "lex read char %d, val %d\n", yychar, yylval);
#endif
		}
		while (ip[1]!=YYNOCHAR) {
			if (ip[1]==yychar)
				break;
			ip += 2;
		}
	}
	act = ip[0];
	switch( act>>YYACTSH ) {
	case YYSHIFTACT:
		if( ip[1]==YYNOCHAR )
			goto YYerract;
		if( yychar != -1 )
			yychar = YYNOCHAR; /* dont throw away EOF */
		yystate = act&YYAMASK;
		yyval = yylval;
#ifdef YYDEBUG
		if( yydebug )
			fprintf(stdout, "shift %d\n", yystate);
#endif
		if( yyerrflag )
			--yyerrflag;
		goto stack;

	case YYACCEPTACT:
#ifdef YYDEBUG
		if( yydebug )
			fprintf(stdout, "accept\n");
#endif
		return(0);

	case YYERRACT:
	YYerract:
		switch (yyerrflag) {
		case 0:
			yyerror("Syntax error");

		case 1:
		case 2:

			yyerrflag = 3;
			while( yys >= & yystack[0] ) {
				ip = &yyact[yypa[*yys]];
				while( ip[1]!=YYNOCHAR )
					ip += 2;
				if( (*ip&~YYAMASK) == (YYSHIFTACT<<YYACTSH) ) {
					yystate = *ip&YYAMASK;
					goto stack;
				}
#ifdef YYDEBUG
				if( yydebug )
					fprintf(stderr, "error recovery leaves state %d, uncovers %d\n", *yys, yys[-1]);
#endif
				yys--;
				yyv--;
			}
#ifdef YYDEBUG
			if( yydebug )
				fprintf(stderr, "no shift on error; abort\n");
#endif
			return(1);

		case 3:
#ifdef YYDEBUG
			if( yydebug )
				fprintf(stderr, "Error recovery clobbers char %o\n", yychar);
#endif
			if( yychar==YYEOFVAL )
				return(1);
			yychar = YYNOCHAR;
			goto read;
		}

	case YYREDACT:
		pno = act&YYAMASK;
#ifdef YYDEBUG
		if( yydebug )
			fprintf(stdout, "reduce %d\n", pno);
#endif
		yypvt = yyv;
		yyv -= yypn[pno];
		yys -= yypn[pno];
		yyval = yyv[1];
		switch(pno) {

case 1: {

 if (checkdefs() < 0) return (errors); }break;

case 2: {

 
		if (sp->num_exits + sp->num_airports < 2)
			yyerror("Need at least 2 airports and/or exits.");
		return (errors);
		}break;

case 9: {


		if (sp->update_secs != 0)
			return (yyerror("Redefinition of 'update'."));
		else if (yypvt[-1].ival < 1)
			return (yyerror("'update' is too small."));
		else
			sp->update_secs = yypvt[-1].ival;
		}break;

case 10: {


		if (sp->newplane_time != 0)
			return (yyerror("Redefinition of 'newplane'."));
		else if (yypvt[-1].ival < 1)
			return (yyerror("'newplane' is too small."));
		else
			sp->newplane_time = yypvt[-1].ival;
		}break;

case 11: {


		if (sp->height != 0)
			return (yyerror("Redefinition of 'height'."));
		else if (yypvt[-1].ival < 3)
			return (yyerror("'height' is too small."));
		else
			sp->height = yypvt[-1].ival; 
		}break;

case 12: {


		if (sp->width != 0)
			return (yyerror("Redefinition of 'width'."));
		else if (yypvt[-1].ival < 3)
			return (yyerror("'width' is too small."));
		else
			sp->width = yypvt[-1].ival; 
		}break;

case 13: {

}break;

case 14: {

}break;

case 15: {

}break;

case 16: {

}break;

case 17: {

}break;

case 18: {

}break;

case 19: {

}break;

case 20: {

}break;

case 21: {


		if (sp->num_beacons % REALLOC == 0) {
			if (sp->beacon == NULL)
				sp->beacon = (BEACON *) malloc((sp->num_beacons
					+ REALLOC) * sizeof (BEACON));
			else
				sp->beacon = (BEACON *) realloc(sp->beacon,
					(sp->num_beacons + REALLOC) * 
					sizeof (BEACON));
			if (sp->beacon == NULL)
				return (yyerror("No memory available."));
		}
		sp->beacon[sp->num_beacons].x = yypvt[-2].ival;
		sp->beacon[sp->num_beacons].y = yypvt[-1].ival;
		check_point(yypvt[-2].ival, yypvt[-1].ival);
		sp->num_beacons++;
		}break;

case 22: {

}break;

case 23: {

}break;

case 24: {


		int	dir;

		if (sp->num_exits % REALLOC == 0) {
			if (sp->exit == NULL)
				sp->exit = (EXIT *) malloc((sp->num_exits + 
					REALLOC) * sizeof (EXIT));
			else
				sp->exit = (EXIT *) realloc(sp->exit,
					(sp->num_exits + REALLOC) * 
					sizeof (EXIT));
			if (sp->exit == NULL)
				return (yyerror("No memory available."));
		}
		dir = dir_no(yypvt[-1].cval);
		sp->exit[sp->num_exits].x = yypvt[-3].ival;
		sp->exit[sp->num_exits].y = yypvt[-2].ival;
		sp->exit[sp->num_exits].dir = dir;
		check_edge(yypvt[-3].ival, yypvt[-2].ival);
		check_edir(yypvt[-3].ival, yypvt[-2].ival, dir);
		sp->num_exits++;
		}break;

case 25: {

}break;

case 26: {

}break;

case 27: {


		int	dir;

		if (sp->num_airports % REALLOC == 0) {
			if (sp->airport == NULL)
				sp->airport=(AIRPORT *)malloc((sp->num_airports
					+ REALLOC) * sizeof(AIRPORT));
			else
				sp->airport = (AIRPORT *) realloc(sp->airport,
					(sp->num_airports + REALLOC) * 
					sizeof(AIRPORT));
			if (sp->airport == NULL)
				return (yyerror("No memory available."));
		}
		dir = dir_no(yypvt[-1].cval);
		sp->airport[sp->num_airports].x = yypvt[-3].ival;
		sp->airport[sp->num_airports].y = yypvt[-2].ival;
		sp->airport[sp->num_airports].dir = dir;
		check_point(yypvt[-3].ival, yypvt[-2].ival);
		sp->num_airports++;
		}break;

case 28: {

}break;

case 29: {

}break;

case 30: {


		if (sp->num_lines % REALLOC == 0) {
			if (sp->line == NULL)
				sp->line = (LINE *) malloc((sp->num_lines + 
					REALLOC) * sizeof (LINE));
			else
				sp->line = (LINE *) realloc(sp->line,
					(sp->num_lines + REALLOC) *
					sizeof (LINE));
			if (sp->line == NULL)
				return (yyerror("No memory available."));
		}
		sp->line[sp->num_lines].p1.x = yypvt[-7].ival;
		sp->line[sp->num_lines].p1.y = yypvt[-6].ival;
		sp->line[sp->num_lines].p2.x = yypvt[-3].ival;
		sp->line[sp->num_lines].p2.y = yypvt[-2].ival;
		check_line(yypvt[-7].ival, yypvt[-6].ival, yypvt[-3].ival, yypvt[-2].ival);
		sp->num_lines++;
		}break;

		}
		ip = &yygo[ yypgo[yypdnt[pno]] ];
		while( *ip!=*yys && *ip!=YYNOCHAR )
			ip += 2;
		yystate = ip[1];
		goto stack;
	}
}

/* end of /lib/yyparse.c */
