/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
**	hrglyph -- load a character set into the hi-res console's loadable bank
**	and show it, then check the driver's answers at the edges of the
**	interface.  Nothing but ordinary bytes are written afterwards: the
**	glyphs are all that changed.
**
**	usage:	hrglyph		load, draw, and report
**		hrglyph -r	restore the built-in set and exit
**
**	The five shapes are deliberately unlike anything in gallant, so a raw
**	frame-buffer dump identifies them without a font matcher: a hollow
**	frame, horizontal stripes, vertical stripes, a solid block, and an X.
**	Every label is plain ASCII, which the console still draws from its own
**	read-only bank.
**
**	Glyph format, per hrtty/h/hrio.h: one `unsigned short' per scan line,
**	the top 12 bits are the pixels, most significant bit leftmost, 1 = ink,
**	25 lines top to bottom.
*/
#include <signal.h>

#define	HRGH		25		/* scan lines per glyph */
#define	HRGW		12		/* pixels per scan line */
#define	HRLOW		96		/* first loadable slot */
#define	HRNGLYPH	192		/* slots the console decodes */

#define	HRIOCSFONT	('h'<<8|1)
#define	HRIOCGFONT	('h'<<8|2)
#define	HRIOCRFONT	('h'<<8|3)

struct hrfont {
	unsigned short	hf_first;
	unsigned short	hf_count;
	unsigned short	*hf_bits;
};

/*
**	A run on a console that has no loadable font bank has not TESTED
**	anything, and a skip reported as a failure trains people to ignore
**	failures.  2 is what tests/termio's own not-a-terminal arm uses.
*/
#define	SKIP	2

/*
**	Every line of output here goes to the console with write(2), and a
**	console whose driver has stopped consuming (an XOFF that is never
**	answered, a font ioctl that left the controller mid-command) blocks the
**	writer for ever.  So does an ioctl into a driver that sleeps for a
**	vertical retrace that does not come.  Either way the run has no verdict,
**	which is worse than a failing one.
*/
#define	DEADLINE	30

#define	NG	5			/* glyphs this program builds */
#define	ALL	0xfff0			/* all HRGW pixels of one line */
#define	PIX(c)	(1 << (15 - (c)))

unsigned short	Glyph[NG][HRGH];	/* what we send */
unsigned short	Back[NG][HRGH];		/* what we read back */
unsigned short	Low[HRLOW][HRGH];	/* the read-only bank, read back */

int	fails;			/* the verdict, and the exit status */

/*
**	write(2) a NUL-terminated string, so stdio buffering cannot reorder the
**	labels against the glyph rows.
*/
say(s)
register char *s;
{
	register char *p;

	for (p = s; *p != '\0'; ++p)
		;
	write(1, s, p - s);
}

/*
**	Build the five shapes.
*/
build()
{
	register int r;
	register int c;

	for (r = 0; r < HRGH; ++r) {
		Glyph[0][r] = (r == 0 || r == HRGH-1) ?
			ALL : PIX(0)|PIX(HRGW-1);
		Glyph[1][r] = (r & 1) ? 0 : ALL;
		Glyph[2][r] = 0;
		for (c = 0; c < HRGW; c += 2)
			Glyph[2][r] |= PIX(c);
		Glyph[3][r] = ALL;
		c = (r * (HRGW-1)) / (HRGH-1);
		Glyph[4][r] = PIX(c) | PIX(HRGW-1-c);
	}
}

/*
**	One line of eight copies of code `code', labelled.
*/
show(code, name)
int code;
char *name;
{
	char row[16];
	register int i;

	for (i = 0; i < 8; ++i)
		row[i] = code;
	row[8] = '\0';
	say("  ");
	write(1, row, 8);
	say("  ");
	say(name);
	say("\r\n");
}

/*
**	The deadline expired.  write(2), not printf: this program has said
**	everything with write(2) so far because stdio buffering would reorder
**	the labels against the glyph rows, and a report that never leaves the
**	buffer is no report.
*/
hung()
{
	say("hrglyph: FAIL -- no verdict in 30 s; the console stopped\r\n");
	say("hrglyph: taking output, or a font ioctl never returned.\r\n");
	exit(1);
}

main(argc, argv)
int argc;
char **argv;
{
	struct hrfont hf;
	int i;
	int j;
	int bad;

	signal(SIGALRM, hung);
	alarm(DEADLINE);

	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'r') {
		if (ioctl(1, HRIOCRFONT, (char *)0) < 0) {
			say("SKIP hrglyph: no font ioctl -- not an HR console\r\n");
			exit(SKIP);
		}
		say("hrglyph: built-in character set restored\r\n");
		exit(0);
	}

	build();
	hf.hf_first = HRLOW;
	hf.hf_count = NG;
	hf.hf_bits = &Glyph[0][0];
	if (ioctl(1, HRIOCSFONT, (char *)&hf) < 0) {
		say("SKIP hrglyph: no font ioctl -- not an HR console\r\n");
		exit(SKIP);
	}
	say("hrglyph: 5 glyphs loaded at 0xa0..0xa4\r\n");
	show(0xa0, "frame");
	show(0xa1, "rows");
	show(0xa2, "columns");
	show(0xa3, "solid");
	show(0xa4, "cross");

	/*
	**	Read the same slots back and compare.
	*/
	hf.hf_first = HRLOW;
	hf.hf_count = NG;
	hf.hf_bits = &Back[0][0];
	if (ioctl(1, HRIOCGFONT, (char *)&hf) < 0) {
		say("readback: IOCTL FAILED\r\n");
		++fails;
	} else {
		bad = 0;
		for (i = 0; i < NG; ++i)
			for (j = 0; j < HRGH; ++j)
				if (Back[i][j] != Glyph[i][j])
					++bad;
		say(bad == 0 ? "readback: OK\r\n" : "readback: MISMATCH\r\n");
		if (bad != 0)
			++fails;
	}

	/*
	**	The read-only bank must be readable and must refuse a write.
	**
	**	Each of the three outcomes below was printed with the word `wrong'
	**	beside it and then thrown away: the program ended in an
	**	unconditional exit(0), so a console driver that accepted a write to
	**	the read-only bank and let a font write run past the last slot left
	**	status 0 behind for whatever ran it.  They are counted now.
	*/
	hf.hf_first = 0;
	hf.hf_count = HRLOW;
	hf.hf_bits = &Low[0][0];
	if (ioctl(1, HRIOCGFONT, (char *)&hf) < 0) {
		say("low bank read: REFUSED (wrong)\r\n");
		++fails;
	} else
		say("low bank read: OK\r\n");

	hf.hf_first = 0;
	hf.hf_count = 1;
	hf.hf_bits = &Glyph[0][0];
	if (ioctl(1, HRIOCSFONT, (char *)&hf) < 0)
		say("low bank write: REFUSED\r\n");
	else {
		say("low bank write: ACCEPTED (wrong)\r\n");
		++fails;
	}

	hf.hf_first = HRNGLYPH - 2;
	hf.hf_count = 4;
	hf.hf_bits = &Glyph[0][0];
	if (ioctl(1, HRIOCSFONT, (char *)&hf) < 0)
		say("past the end: REFUSED\r\n");
	else {
		say("past the end: ACCEPTED (wrong)\r\n");
		++fails;
	}

	say("hrglyph: `hrglyph -r' puts the built-in set back\r\n");
	say(fails ? "hrglyph: FAIL\r\n" : "hrglyph: PASS\r\n");
	exit(fails != 0);
}
