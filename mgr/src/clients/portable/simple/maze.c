/*{{{}}}*/
/*{{{  Notes*/
/*                        Copyright (c) 1987 Bellcore
 *                            All Rights Reserved
 *       Permission is granted to copy or use this program, EXCEPT that it
 *       may not be sold for profit, the copyright notice must be reproduced
 *       on copies, and credit should be given to Bellcore where it is due.
 *       BELLCORE MAKES NO WARRANTY AND ACCEPTS NO LIABILITY FOR THIS PROGRAM.
 */
/* mgr version */
/* *************************************************************\

	maze.c

	Author: JGosling
	Information Technology Center
	Carnegie-Mellon University

	(c) Copyright IBM Corporation, 1985
	Written: 28.July.1984


\* ************************************************************ */

/* A simple maze wars game to test out user level graphics */
/*}}}  */
/*{{{  How the players find each other*/
/*
 *	maze [peer]
 *
 * Every player sends its own position and heading as one datagram after each
 * move, and draws the players whose datagrams arrive.  There is no server and
 * no game state anywhere: the maze is compiled in, and a player exists to the
 * others exactly as long as its datagrams keep arriving.
 *
 * The datagrams go to ONE address on the `mazewar' udp port, and every player
 * binds that same port, so everyone sees everyone.  Which address that is:
 *
 *	no argument	this machine's own address, from gethostname() +
 *			gethostbyname().  The stack loops a packet addressed
 *			to its own interface back internally (net/inet/
 *			generic/ip_write.c), so this reaches every player
 *			ON THIS MACHINE and needs no wire, no slip and no
 *			peer -- two windows, two players.
 *
 *	an argument	a dotted quad or an /etc/hosts name.  Point each of two
 *			machines at the other, or both at 255.255.255.255 to
 *			broadcast.
 *
 * The original sent to inet_makeaddr(inet_netof(myaddr), INADDR_ANY) -- the
 * 4.2BSD all-zeroes broadcast address.  This stack answers EBADDEST for a
 * destination whose host part is zero (ip_write.c: "Zero host part"), it
 * recognises only the all-ones form as broadcast, and a broadcast leaves down
 * the wire without being looped back, so that address reaches nobody here.
 *
 * Two players on this machine bind the same local port at once.  The stack
 * permits that -- udp.c's conflict test answers EADDRINUSE only when the two
 * sockets ask for DIFFERENT access modes -- and udp_arrived enqueues a copy of
 * an arriving datagram on every socket that matches, so both players receive
 * every datagram, including their own.  A player ignores its own id.
 *
 * One socket per player, bound and sending, rather than the original's
 * separate send and receive sockets: each socket is a channel to the inet
 * daemon and costs it two file descriptors, of which it has seven pairs to
 * give the whole machine.
 */
/*}}}  */
/*{{{  #includes*/
#include <mgr/mgr.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
/*}}}  */

int Redraw = 1;
int debug = 0;
int _func = -1;

extern int errno;

#define dprintf		if(debug)fprintf
#define SERVER	"mazewar"

/* The port when /etc/services names no `mazewar' service.  hunt(6) has 26740
 * and 26741; this is the next free one in that block. */
#define MAZEPORT	26742

#define M_func(n)	(_func!=n ? (m_func(n),_func=n) : 0)

#define MazeWidth 15
#define MazeHeight ((int)(sizeof MazeWalls/MazeWidth))
char MazeWalls[] = {
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
1,1,0,1,0,1,0,1,1,1,0,1,1,0,1,
1,0,0,1,1,1,0,0,1,0,0,0,0,1,1,
1,0,1,0,0,0,1,1,1,0,1,1,0,0,1,
1,0,1,0,1,1,0,0,0,0,1,0,1,0,1,
1,0,0,0,0,1,1,0,1,1,1,0,0,0,1,
1,1,0,1,0,1,1,0,1,1,0,0,1,0,1,
1,1,0,1,0,1,0,0,0,0,0,1,0,0,1,
1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,
1,0,0,0,1,0,0,0,0,0,1,1,0,1,1,
1,0,1,1,1,1,0,1,1,0,0,0,0,1,1,
1,0,0,0,1,0,0,0,1,1,1,1,0,1,1,
1,1,1,0,0,0,1,1,1,1,1,0,0,0,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

struct DirectionOffsets {
	char left, right, forward, backward;
} DirectionOffsets[4] = {
	{ -1, 1, -MazeWidth, MazeWidth }, /* facing up */
	{ -MazeWidth, MazeWidth, 1, -1 }, /* facing right */
	{ 1, -1, MazeWidth, -MazeWidth }, /* facing down */
	{ MazeWidth, -MazeWidth, -1, 1 }  /* facing left */
};

/*
 * The wire record, and the only thing the players ever say to each other.
 *
 * id is a long because it must be unique across every player reachable from
 * here, and int is 16 bits: its low half is the sender's process id and its
 * high half the low half of the sender's address, so two players on one
 * machine differ in the pid and two machines differ in the address.
 */
struct state {
    long id;
    short position, direction;
};

struct state me, him;
#define HashSize 47
struct state others[HashSize];

/* One slot longer than the table it indexes: the walkers below stop on a null
 * entry, so the last used slot must still be followed by one. */
struct state *SlotsUsed[HashSize+1];
int NOthers;

int direction = 1;
int position = MazeWidth+1;
int BogyDistance = 999;
struct state *BogyId;
int BogyRDir;

int swidth, sheight;
int fwidth, fheight;

int displaystate[MazeWidth+1];
int MaxDepth;

int cx, cy;

int Socket;			/* bound, and sends: see the note above	*/
struct sockaddr_in sock_in;	/* what this player listens on		*/
struct sockaddr_in sout;	/* where its datagrams go		*/

#define ForwardFrom(p) ((p)+DirectionOffsets[direction].forward)
#define BackwardFrom(p) ((p)+DirectionOffsets[direction].backward)
#define LeftFrom(p) ((p)+DirectionOffsets[direction].left)
#define RightFrom(p) ((p)+DirectionOffsets[direction].right)

#define At(p) MazeWalls[p]
#define AtForward(p) MazeWalls[ForwardFrom(p)]
#define AtBackward(p) MazeWalls[BackwardFrom(p)]
#define AtLeft(p) MazeWalls[LeftFrom(p)]
#define AtRight(p) MazeWalls[RightFrom(p)]

DrawFrom (p, w, h)
int p, w, h;
{
    int     depth = 0;
    int     nmax = 0;
    if (BogyId) {
	DrawEye (BogyDistance, BogyRDir);
	BogyDistance = 9999;
	BogyId = 0;
    }
    M_func(BIT_INVERT);
    while (!At (p) || depth <= MaxDepth) {
	int iw = w * 4 / 5;
	int ih = h * 4 / 5;
	int ThisMask = 0;
	int DoMask;
	if (depth && BogyId == 0) {
	    register struct state **f;
	    for (f = SlotsUsed; *f; f++)
		if ((*f) -> position == p) {
		    DrawEye (depth, BogyRDir = (*f) -> direction - direction);
		    BogyDistance = depth;
		    BogyId = *f;
		}
	}
	if (!At (p)) {
	    nmax = depth;
	    if (AtLeft (p))
		ThisMask |= 1 << 0;
	    if (AtRight (p))
		ThisMask |= 1 << 1;
	    if (AtForward (p))
		ThisMask |= 1 << 2;
	    if (!AtLeft (p) && AtForward (LeftFrom (p)))
		ThisMask |= 1 << 3;
	    if (!AtRight (p) && AtForward (RightFrom (p)))
		ThisMask |= 1 << 4;
	    if (((AtRight (p) + AtForward (p) + AtRight (ForwardFrom (p))) & 1)
		    || (AtRight (p) && AtForward (p)))
		ThisMask |= 1 << 5;
	    if (((AtLeft (p) + AtForward (p) + AtLeft (ForwardFrom (p))) & 1)
		    || (AtLeft (p) && AtForward (p)))
		ThisMask |= 1 << 6;
	    p = ForwardFrom (p);
	}
	DoMask = depth > MaxDepth ? ThisMask : displaystate[depth] ^ ThisMask;
	displaystate[depth] = ThisMask;

/****************************************************************/

	if (DoMask & (1 << 0)) {
	    m_go (cx - w + 1, cy - h + 1);
	    m_draw (cx - iw, cy - ih);
	    m_go (cx - w + 1, cy + h - 1);
	    m_draw (cx - iw, cy + ih);
	}
	if (DoMask & (1 << 1)) {
	    m_go (cx + w - 1, cy - h + 1);
	    m_draw (cx + iw, cy - ih);
	    m_go (cx + w - 1, cy + h - 1);
	    m_draw (cx + iw, cy + ih);
	}
	if (DoMask & (1 << 2)) {
	    m_go (cx - iw, cy - ih);
	    m_draw (cx + iw, cy - ih);
	    m_go (cx - iw, cy + ih);
	    m_draw (cx + iw, cy + ih);
	}
	if (DoMask & (1 << 3)) {
	    m_go (cx - w + 1, cy - ih);
	    m_draw (cx - iw, cy - ih);
	    m_go (cx - w + 1, cy + ih);
	    m_draw (cx - iw, cy + ih);
	}
	if (DoMask & (1 << 4)) {
	    m_go (cx + w - 1, cy - ih);
	    m_draw (cx + iw, cy - ih);
	    m_go (cx + w - 1, cy + ih);
	    m_draw (cx + iw, cy + ih);
	}
	if (DoMask & (1 << 5)) {
	    m_go (cx + iw, cy - ih);
	    m_draw (cx + iw, cy + ih);
	}
	if (DoMask & (1 << 6)) {
	    m_go (cx - iw, cy - ih);
	    m_draw (cx - iw, cy + ih);
	}
	w = iw;
	h = ih;
	depth++;
    }
    MaxDepth = nmax;
}

FlagRedraw () {
    Redraw++;
}

/* How many cells ahead the given cell is, or 0 if a wall hides it. */
CanSee (target)
int target;
{
    register int p;
    register int depth = 1;
    for (p = position; ((p = ForwardFrom (p)), p>0 && p<MazeWidth*MazeWidth && !At (p)); depth++)
	if (p == target)
	    return depth;
    return 0;
}

DrawEye (depth, rdir)
int depth, rdir;
{
    int r = cy < cx ? cy : cx;
    int sr;
    while (--depth >= 0)
	r = r * 4 / 5;
    sr = r / 3;
    m_go (cx - sr, cy - r);
    m_draw (cx + sr, cy - r);
    m_draw (cx + r, cy - sr);
    m_draw (cx + r, cy + sr);
    m_draw (cx + sr, cy + r);
    m_draw (cx - sr, cy + r);
    m_draw (cx - r, cy + sr);
    m_draw (cx - r, cy - sr);
    m_draw (cx - sr, cy - r);
    while (rdir < 0)
	rdir += 4;
    switch (rdir) {
	case 2:
	    m_go (cx - r, cy);
	    m_draw (cx, cy - sr);
	    m_draw (cx + r, cy);
	    m_draw (cx, cy + sr);
	    m_draw (cx - r, cy);
	    break;
	case 3:
	    m_go (cx - r, cy - sr);
	    m_draw (cx, cy);
	    m_draw (cx - r, cy + sr);
	    break;
	case 1:
	    m_go (cx + r, cy - sr);
	    m_draw (cx, cy);
	    m_draw (cx + r, cy + sr);
	    break;
    }
}

DrawMaze (xo, yo, width, height)
int xo, yo, width, height;
{
    int x,y;
    M_func(BIT_OR);
    for (x = 0; x < MazeWidth; x++)
	for (y = 0; y < MazeHeight; y++)
	    if (At (y * MazeWidth + x))
                m_bitwrite(xo + x * width / MazeWidth,
			yo + y * height / MazeHeight,
			(x + 1) * width / MazeWidth - x * width / MazeWidth ,
			(y + 1) * height / MazeHeight - y * height / MazeHeight );
}

DrawAllArrows () {
    register struct state **f;
    DrawArrow (position,direction);
    for (f = SlotsUsed; *f; f++)
    DrawArrow ((*f)->position,(*f)->direction);
}

/*
 * The plan view arrow for one player, centred on the cell DrawMaze drew for
 * that position.
 *
 * The cell's two edges are scaled separately and the centre taken between
 * them, so the largest product is (MazeWidth * swidth) rather than the
 * (2*MazeWidth-1) * swidth of a single midpoint expression.  int is 16 bits
 * here: at the 1024-pixel screen this is built for, the midpoint form reaches
 * 29,696 of the 32,767 an int holds, and any wider window overflows it.
 */
DrawArrow (position, direction)
int position, direction;
{
    static char *arrow[4] = {
	"^", ">", "v", "<"
    };
    register int x = position % MazeWidth;
    register int y = position / MazeWidth;
    int cellx, celly;

    cellx = (x * swidth / MazeWidth + (x + 1) * swidth / MazeWidth) >> 1;
    celly = (y * cy / MazeHeight + (y + 1) * cy / MazeHeight) >> 1;
    M_func(BIT_XOR);
    m_moveprint (cellx, celly + cy * 2 + (fheight>>1), arrow[direction]);
    m_movecursor(0,0);
}

UpdateOther () {
    register struct state  *s = &others[(int)((unsigned long)him.id % HashSize)];
    register int dist;
    while (s -> id && s -> id != him.id) {
	s--;
	if (s < others)
	    s = others + HashSize - 1;
    }
    M_func(BIT_XOR);
    if (s -> id == 0) {
	if (NOthers >= HashSize)
	    return;			/* the table is full */
	SlotsUsed[NOthers++] = s;
    }
    else {
	DrawArrow (s -> position, s -> direction);
	if (s == BogyId) {
	    DrawEye (BogyDistance, BogyRDir);
	    BogyDistance = 9999;
	    BogyId = 0;
	}
    }
    if ((dist = CanSee (him.position)) && dist<BogyDistance) {
	BogyDistance = dist;
	BogyId = s;
	DrawEye (BogyDistance, BogyRDir = him.direction - direction);
    }
    *s = him;
    DrawArrow (s -> position, s -> direction);
}

/*
 * This machine's own address, and the id that address plus our pid makes.
 * Both come from gethostname() + gethostbyname(), i.e. /etc/hostname and
 * /etc/hosts, since there is no address the kernel could be asked for.
 */
static in_addr_t
MyAddress ()
{
    char host[64];
    struct hostent *hp;
    in_addr_t a;

    if (gethostname (host, sizeof (host) - 1) < 0) {
	perror ("gethostname");
	return (in_addr_t)0;
    }
    host[sizeof (host) - 1] = '\0';
    if ((hp = gethostbyname (host)) == (struct hostent *)0) {
	fprintf (stderr, "maze: no address for host %s\n", host);
	return (in_addr_t)0;
    }
    memcpy ((char *)&a, hp->h_addr, sizeof (a));
    return a;
}

main( argc, argv )
int	argc;
char	*argv[];
{
    char *getenv();
    struct servent *sp;
    in_addr_t myaddr, peer;
    int port;
    fd_set readfds;

    ckmgrterm( *argv );

    if (getenv("DEBUG")) {
       debug++;
       }

    m_setup(0);
    m_push(P_FLAGS|P_EVENT);
    m_setmode(M_ABS);
    m_setmode(M_OVERSTRIKE);
    M_func(BIT_SRC);
    m_setevent(REDRAW,"R");
    m_setevent(RESHAPE,"R");
    m_ttyset();
    m_setraw();
    m_setnoecho();

    myaddr = MyAddress ();
    peer = argc > 1 ? inet_addr (argv[1]) : myaddr;
    if (peer == (in_addr_t)INADDR_NONE && argc > 1) {
	struct hostent *hp;

	if ((hp = gethostbyname (argv[1])) == (struct hostent *)0) {
	    fprintf (stderr, "maze: unknown host %s\n", argv[1]);
	    peer = myaddr;
	}
	else
	    memcpy ((char *)&peer, hp->h_addr, sizeof (peer));
    }

    if ((sp = getservbyname (SERVER, "udp")) != (struct servent *)0)
	port = sp->s_port;		/* already network order */
    else
	port = htons (MAZEPORT);

    if ((Socket = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror ("socket");
	m_ttyreset ();
	exit (1);
    }

    /* Advisory here: this stack holds no port after a close, so a rebind
     * never has to wait, and libsocket accepts the option and does nothing. */
    setsockopt (Socket, SOL_SOCKET, SO_REUSEADDR, (char *)0, 0);

    memset ((char *)&sock_in, 0, sizeof (sock_in));
    sock_in.sin_family = AF_INET;
    sock_in.sin_port = port;
    sock_in.sin_addr.s_addr = INADDR_ANY;
    if (bind (Socket, (struct sockaddr *)&sock_in, sizeof (sock_in)) < 0) {
	perror ("bind");
	m_ttyreset ();
	exit (1);
    }

    memset ((char *)&sout, 0, sizeof (sout));
    sout.sin_family = AF_INET;
    sout.sin_port = port;
    sout.sin_addr.s_addr = peer;

    dprintf (stderr, "maze: port %d peer %s\n", ntohs (port),
	     inet_ntoa (sout.sin_addr));

    me.id = ((long)(myaddr & 0xffffL) << 16) | (long)(getpid () & 0x7fff);
    dprintf (stderr, "maze: id %ld\n", me.id);

    FlagRedraw ();
    while (1) {
	    me.position = position;
	    me.direction = direction;
	    if (sendto (Socket, (char *) &me, sizeof me, 0,
			(struct sockaddr *)&sout, sizeof sout) != sizeof me)
		perror ("sendto");
            else
                dprintf(stderr,"Sent %ld %d %d\n",
                        me.id,me.direction,me.position);
	if (Redraw) {
            m_getwindowsize(&swidth,&sheight);
            m_getfontsize(&fwidth,&fheight);
	    M_func(BIT_SET);
	    cx = swidth / 2;
	    cy = sheight / 3;
	    m_clear();
	    DrawMaze (0, 2 * cy, swidth, cy);
	    Redraw = 0;
	    MaxDepth = -1;
	}
	DrawFrom (position, cx, cy);
	M_func(BIT_OR);
	DrawAllArrows ();
	{
	    register    op = position;
	    register    c;
	    while (1) {
		int     n;
		m_flush();
		FD_ZERO (&readfds);
		FD_SET (fileno (m_termin), &readfds);
		FD_SET (Socket, &readfds);
                dprintf(stderr,"Select..."); fflush(stderr);
		select (FD_SETSIZE, &readfds, (fd_set *)0, (fd_set *)0,
			(struct timeval *)0);
                dprintf(stderr," got %lx\n",readfds.fds_bits);
		if (FD_ISSET (Socket, &readfds)) {
		    /* recvfrom, not read: a datagram arrives from the stack
		     * behind a udp_io_hdr naming its sender, and only
		     * recvfrom() strips that header off. */
		    n = recvfrom (Socket, (char *)&him, sizeof him, 0,
				  (struct sockaddr *)0, (int *)0);
		    if (n < 0)
			perror ("recvfrom");
		    else if (n == sizeof him) {
                        dprintf(stderr,"Got %ld (%ld) %d %d\n",
                                him.id,me.id,him.direction,him.position);
			if (him.id != me.id && him.direction < 4)
			    UpdateOther ();
		    }
		}
		if (Redraw || FD_ISSET (fileno (m_termin), &readfds))
		    break;
	    };
	    if (!Redraw) {
		c = getc (m_termin);
	        M_func(BIT_OR);
		DrawAllArrows ();
	    }
	    else
		if (errno != EINTR)
		    break;
		else {
		    errno = 0;
		    continue;
		}
	    switch (c & 0177) {
		case ' ':
		case 'f':
		case '8':
		    position = ForwardFrom (position);
		    break;
		case 'l':
		case '4':
		    if (--direction < 0)
			direction = 3;
		    break;
		case 'r':
		case '6':
		    if (++direction > 3)
			direction = 0;
		    break;
		case 'b':
		case '2':
		    position = BackwardFrom (position);
		    break;
                case 'R':
                    FlagRedraw();
                    break;
		case 'q':
		case 3: 	/* ^C */
		    m_ttyreset();
                    m_clear();
                    m_pop();
		    exit (1);
		    break;
	    }
	    if (At (position))
		position = op;
	}
    }
}
