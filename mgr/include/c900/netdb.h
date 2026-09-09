/*
 * netdb.h -- host and service lookup for the MGR tree.
 *
 * Two behaviours, selected by MGR_REAL_NETDB.
 *
 * Without it (the default, and what the server builds with) gethostbyname()
 * is a macro yielding a typed null.  The server's only caller is
 * src/mgr/mgrlogin.c:443, which decorates the lock screen with the machine's
 * official name and already handles a null answer by keeping the name
 * gethostname() gave it.  The server links against libmgr and libc900 and
 * nothing else -- no libsocket, no inet daemon -- so a real lookup would be an
 * undefined symbol at link time and, on a machine whose daemon is not running,
 * a failed FIFO rendezvous at run time for a cosmetic string.
 *
 * With MGR_REAL_NETDB the lookups are the ones in libsocket (net/netdb.c):
 * /etc/hosts and /etc/services, a dotted quad accepted directly, no DNS.  A
 * client compiled this way MUST be linked against net/libsocket.a.  The
 * declarations are repeated here rather than reached through
 * -Ios/net/include, because that directory also holds an errno.h carrying the
 * stack's internal NEGATIVE errno values, and putting it on a client's include
 * path replaces the system errno.h with it.
 *
 * struct hostent and struct servent are laid out exactly as net/netdb.h
 * lays them out; that file is what fills them in.
 */
#ifndef NETDB_H
#define NETDB_H

struct	hostent {
	char	*h_name;		/* official name			*/
	char	**h_aliases;		/* alias list (always empty here)	*/
	int	h_addrtype;		/* AF_INET				*/
	int	h_length;		/* 4					*/
	char	**h_addr_list;		/* addresses, null terminated		*/
#define	h_addr	h_addr_list[0]
};

struct	servent {
	char	*s_name;		/* official service name		*/
	char	**s_aliases;		/* alias list (always empty here)	*/
	int	s_port;			/* port, in NETWORK byte order		*/
	char	*s_proto;		/* "tcp" or "udp"			*/
};

#define	HOST_NOT_FOUND	1
#define	TRY_AGAIN	2
#define	NO_RECOVERY	3
#define	NO_DATA		4

#ifdef MGR_REAL_NETDB

extern int h_errno;

struct hostent *gethostbyname();	/* (char *name)				*/
struct hostent *gethostbyaddr();	/* (char *addr, int len, int type)	*/
struct servent *getservbyname();	/* (char *name, char *proto)		*/
struct servent *getservbyport();	/* (int port, char *proto)		*/

#else

#define gethostbyname(x) (struct hostent *)0

#endif /* MGR_REAL_NETDB */

#endif /* NETDB_H */
