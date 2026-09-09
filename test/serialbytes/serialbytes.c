/*
 * serialbytes.c -- is a serial line byte-exact, all 256 values, both ways?
 *
 *	serialbytes out <dev>		write 0x00..0xFF to <dev>
 *	serialbytes in  <dev> <n>	read n bytes from <dev>, report on stdout
 *
 * The point is the values >= 0x80: SLIP frames are delimited by exactly 0xC0
 * (END) and 0xDB (ESC), and a transport that runs bytes through a rune/UTF-8
 * conversion turns 0xC0 into the two bytes 0xC3 0x80.  This is the guest
 * half of the check; the host half reads/writes the bytes over the serial
 * API and compares.
 *
 * /dev/tty51 is the line to use: altty[] gives minor 0 base 0x100 (channel B,
 * the console) and minor 1 base 0x120, so minor 1 is SCC 0 channel A -- the one
 * free serial port.
 *
 * RAW mode is essential.  In cooked mode the tty layer maps CR/NL, expands tabs
 * and treats 0x04/0x1A as control, so a byte-exactness test would be measuring
 * the line discipline rather than the wire.
 *
 * THE READING DIRECTIONS HAVE NO OTHER END OF THEIR OWN.  `in' and `echo' both
 * block on a line whose writer is a host-side script, so every way that script
 * can fail -- not started, wrong channel, killed, or the API gap this program
 * exists to measure swallowing the bytes -- is a read that never returns.  The
 * guest half then holds the whole host-driven check open for ever with nothing
 * printed.  DEADLINE reports the short count instead, which is also the useful
 * answer: WHICH values arrived before it stopped.
 */
#include <sys/types.h>
#include <sgtty.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define DEADLINE 120			/* seconds waiting for the host */

static int	got;			/* bytes in, for the deadline report */
static int	want;

/*
 * The deadline expired: report how far the transfer got.  A short count with
 * its values is the measurement; a hang is nothing at all.
 */
static
hung()
{
	printf("serialbytes: FAIL -- %d of %d bytes in %d s; the other end of"
		" the line\n", got, want, DEADLINE);
	printf("serialbytes: never finished sending.  A test that hangs has no"
		" verdict.\n");
	fflush(stdout);
	exit(1);
}

static rawtty(fd)
int fd;
{
	struct sgttyb sg;

	if (gtty(fd, &sg) < 0) { perror("serialbytes: gtty"); return -1; }
	sg.sg_ispeed = B9600;
	sg.sg_ospeed = B9600;
	sg.sg_flags = RAW;
	if (stty(fd, &sg) < 0) { perror("serialbytes: stty"); return -1; }
	return 0;
}

main(argc, argv)
int argc;
char **argv;
{
	unsigned char buf[256];
	int fd, i, n;

	signal(SIGALRM, hung);

	if (argc < 3)
	{
		fprintf(stderr, "usage: serialbytes out|in|echo <dev> [count]\n");
		return 1;
	}
	if ((fd = open(argv[2], O_RDWR)) < 0)
	{
		perror("serialbytes: open");
		return 1;
	}
	if (rawtty(fd) < 0)
		return 1;

	if (strcmp(argv[1], "out") == 0)
	{
		want = 256;
		alarm(DEADLINE);	/* a flow-controlled line blocks here */
		for (i = 0; i < 256; i++)
			buf[i] = i;
		/* One write, so the host sees a single contiguous burst. */
		n = write(fd, (char *)buf, 256);
		printf("serialbytes: wrote %d\n", n);
		return (n == 256) ? 0 : 1;
	}

	if (strcmp(argv[1], "in") == 0)
	{
		want = (argc > 3) ? atoi(argv[3]) : 256;
		if (want > 256)
			want = 256;
		alarm(DEADLINE);
		/* Announce that the line is OPEN before blocking on it, and
		 * flush so the announcement is not still sitting in stdio.
		 *
		 * A host injecting on a timer instead of on this marker races
		 * the open, and the race is not harmless: alclose() sets WR1 to
		 * 0, so between readers the channel's receive interrupts are
		 * DISABLED.  Bytes arriving then are assembled into the FIFO and
		 * announced to nobody -- they appear only when the next open
		 * re-enables interrupts, or are overwritten once the 3-deep FIFO
		 * fills.  That looked exactly like the driver losing characters. */
		printf("serialbytes: ready\n");
		fflush(stdout);
		/* The line delivers in chunks; keep reading until we have all
		 * of it or the read fails. */
		for (got = 0; got < want; got += n)
		{
			n = read(fd, (char *)&buf[got], want - got);
			if (n <= 0)
				break;
		}
		printf("serialbytes: read %d of %d\n", got, want);
		/* Report as decimal on stdout (the console) so the host can
		 * compare without the console mangling anything. */
		for (i = 0; i < got; i++)
			printf("%d%s", buf[i], ((i % 16) == 15) ? "\n" : " ");
		if ((got % 16) != 0)
			printf("\n");
		return (got == want) ? 0 : 1;
	}

	if (strcmp(argv[1], "echo") == 0)
	{
		want = (argc > 3) ? atoi(argv[3]) : 256;
		if (want > 256)
			want = 256;
		alarm(DEADLINE);
		printf("serialbytes: ready\n");
		fflush(stdout);
		for (got = 0; got < want; got += n)
		{
			n = read(fd, (char *)&buf[got], want - got);
			if (n <= 0)
				break;
		}
		/* Send them straight back down the SAME line rather than
		 * reporting on the console.  Reporting 255 values as decimal is
		 * about a kilobyte of console traffic, which at 9600 baud takes
		 * long enough that the host was parsing a half-printed line and
		 * calling it zero values received.  Echoing keeps the whole
		 * check on the line under test, so the host compares the bytes
		 * it sent against the bytes it gets back and the console is not
		 * involved in the measurement at all.
		 *
		 * A short read still echoes what arrived: the host needs to see
		 * WHICH values survived, not just how many. */
		if (got > 0)
			(void)write(fd, (char *)buf, got);
		printf("serialbytes: echoed %d of %d\n", got, want);
		return (got == want) ? 0 : 1;
	}

	fprintf(stderr, "serialbytes: unknown direction %s\n", argv[1]);
	return 1;
}
