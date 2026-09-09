/*
 * ptraudit.c -- does a 32-bit return value survive the call?
 *
 * On the Z8001 an int is 16 bits, a long is 32, and a pointer is a 32-bit
 * far seg:off.  The trap table (sys/z8001/src/tab.c) and the C ABI put a
 * 32-bit result in R0:R1 and a 16-bit one in R1 alone, so a function whose
 * return type the compiler does not know is read out of R1 only: a long
 * comes back modulo 65536, and a pointer comes back with its segment
 * replaced by zero.  K&R C has no prototypes, so "does not know" means
 * "has no declaration in scope".
 *
 * Each check below captures the result of one libc call and uses it.  A
 * check fails if the caller saw a truncated value, so the whole program is
 * a test of whether the declarations in <stdio.h>, <string.h>, <stdlib.h>,
 * <unistd.h>, <mtype.h> and <signal.h> are reachable on this target.
 *
 * Runs on the host under `n2z8001 -runexec ptraudit' (which emulates
 * open/creat/write/lseek/unlink) as well as on the C900.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <mtype.h>
#include <signal.h>

#define	TMPFILE	"ptraudit.tmp"
#define	FILELEN	70000L		/* > 65535, so a 16-bit result cannot hold it */

static int	fails;
static char	buf[64];

static void
ck(name, ok)
char	*name;
int	ok;
{
	printf("%-10s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok)
		fails++;
}

/*
 * A far pointer into static data has a nonzero segment; a truncated one
 * reads back as segment 0.  Only static and heap addresses qualify -- the
 * stack lives in segment 0, so an automatic's address is a false negative.
 */
static int
farptr(p)
char	*p;
{
	return ((long)p >> 16) != 0L;
}

static void
ckindex()
{
	char	*p, *q;

	strcpy(buf, "/usr/lib/x.a");
	p = index(buf, '/');
	q = rindex(buf, '/');
	ck("index", p == buf && farptr(p));
	ck("rindex", q == buf + 8 && farptr(q));

	/* the caller's usual next move: write through the result */
	*q = '\0';
	ck("rindex-w", strcmp(buf, "/usr/lib") == 0);
}

static void
cklseek()
{
	int	fd;
	long	off, i;
	char	blk[1000];

	if ((fd = creat(TMPFILE, 0666)) < 0) {
		ck("lseek", 0);
		return;
	}
	memset(blk, 'x', sizeof blk);
	for (i = 0; i < FILELEN; i += (long)sizeof blk)
		write(fd, blk, sizeof blk);
	off = lseek(fd, 0L, 2);
	ck("lseek-end", off == FILELEN);
	off = lseek(fd, 66000L, 0);
	ck("lseek-set", off == 66000L);
	close(fd);
	unlink(TMPFILE);
}

static	char	gb[32];

static void
ckcvt()
{
	char	*p;
	int	dec, sign;

	p = ecvt(1.5, 6, &dec, &sign);
	ck("ecvt", farptr(p) && *p == '1');
	p = fcvt(1.5, 4, &dec, &sign);
	ck("fcvt", farptr(p) && *p == '1');
	p = gcvt(1.5, 6, gb);
	ck("gcvt", p == gb && farptr(p) && *p == '1');
}

static int	caught;

static void
onalrm(n)
int	n;
{
	caught++;
}

static void
onalrm2(n)
int	n;
{
	caught += 2;
}

/*
 * signal() returns the previous handler.  The first install hands back
 * SIG_DFL, which is zero either way; the second hands back a real text
 * address, whose segment is the part that gets lost.
 */
static void
cksignal()
{
	void	(*old)();

	signal(SIGALRM, onalrm);
	old = signal(SIGALRM, onalrm2);
	ck("signal", old == onalrm && farptr((char *)old));
	signal(SIGALRM, SIG_DFL);
}

static	char	pat[16];

static void
ckmisc()
{
	char	*p;

	p = mtype(M_Z8001);
	ck("mtype", p != NULL && farptr(p) && *p == 'Z');

	strcpy(pat, "/tmp/paXXXXXX");
	p = mktemp(pat);
	ck("mktemp", p == pat && farptr(p) && index(p, 'X') == NULL);

	p = crypt("passwd", "aa");
	ck("crypt", farptr(p) && strlen(p) > 2);

	p = sprintf(buf, "%d-%s", 42, "x");
	ck("sprintf", p == buf && farptr(p) && strcmp(p, "42-x") == 0);
}

main()
{
	ckindex();
	cklseek();
	ckcvt();
	cksignal();
	ckmisc();
	printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
	return (fails != 0);
}
