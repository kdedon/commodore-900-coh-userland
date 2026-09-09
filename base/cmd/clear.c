/*
 * clear.c
 * 3/18/91
 * Usage: clear
 * cc -o clear clear.c -lterm
 * Clear the screen (stdout) using termcap.
 */

#include <stdio.h>

#define	TCBUFLEN	1024
#define TCAPSLEN	315
#define	NLINES		22

extern	char	*getenv();
extern	int	tgetent();
extern	char	*tgetstr();
extern	char	*tgoto();
extern	int	ttputc();

void	fatal();
int	ttputc();

char	tcbuf[TCBUFLEN];
char	tcapbuf[TCAPSLEN];

/*
 * The three globals libterm reads and does not define: tputs pads with PC, and
 * tgoto backs up with BC or moves up with UP when it has to synthesise a motion.
 * A termcap caller has always had to supply them (libcurses does; this does not
 * link libcurses).  Left null and zero, which asks for no padding and tells
 * tgoto it has no backspace or up-line string to build with -- true for the
 * absolute `cm' this uses.  ospeed is libterm's own.
 */
char	PC;
char	*BC;
char	*UP;

main()
{
	register char *clear, *move, *tv_stype, *p;

	p = tcapbuf;
	if ((tv_stype=getenv("TERM")) == NULL)
		fatal("enviroment variable TERM not defined");
	else if (tgetent(tcbuf, tv_stype) != 1)
		fatal("unknown terminal type \"%s\"", tv_stype);
	else if ((clear=tgetstr("cd", &p)) == NULL
	      || (move =tgetstr("cm", &p)) == NULL)
		fatal("incomplete termcap entry");
	tputs(tgoto(move, 0, 0), 1, ttputc);
	tputs(clear, 1, ttputc);
	exit(0);
}

/* VARARGS */
void
fatal(x) char *x;
{
	register int i;

	fprintf(stderr, "clear: %r\n", &x);
	for (i = 1; i <= NLINES; i++)
		putchar('\n');
	exit(1);
}

int
ttputc(c) int c;
{
	return fputc(c, stdout);
}

/* end of clear.c */
