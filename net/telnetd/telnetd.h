/*
 * TNET		A server program for MINIX which implements the TCP/IP
 *		suite of networking protocols.  It is based on the
 *		TCP/IP code written by Phil Karn et al, as found in
 *		his NET package for Packet Radio communications.
 *
 *		Definitions for the TELNET server.
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#include <ansi.h>

extern int opt_d;			/* debugging flag		*/
extern char *pty_reason;		/* why get_pty() failed		*/

_PROTOTYPE(int get_pty, (int *, char **));
_PROTOTYPE(void term_init, (int netfd));
_PROTOTYPE(void term_inout, (int net_in, int net_out, int net_is_sock,
							int pty_fd));
_PROTOTYPE(void tel_init, (void));
_PROTOTYPE(void telopt, (int fdout, int what, int option));
_PROTOTYPE(int tel_in, (int fdout, int telout, char *buffer, int len));
_PROTOTYPE(int tel_out, (int fdout, char *buf, int size));
