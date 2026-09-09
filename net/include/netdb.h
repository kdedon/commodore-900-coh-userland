/*
 * netdb.h -- host and service lookup, for programs written against BSD.
 *
 * COHERENT 3.x predates networking, so there is no system netdb.h and no
 * resolver; these live in libsocket with the rest of the BSD veneer.  Lookups
 * read /etc/hosts and /etc/services, the files a Unix of this era would have,
 * and fall back to a small built-in table when the file is missing so that a
 * machine with no /etc/services can still reach the well-known ports.
 *
 * There is no DNS.  gethostbyname() resolves a dotted quad or a name in
 * /etc/hosts and nothing else; h_errno is set, and HOST_NOT_FOUND is the
 * honest answer for anything else rather than a lookup that appears to hang.
 */
#ifndef NETDB_H
#define NETDB_H

struct hostent {
	char	*h_name;		/* official name			*/
	char	**h_aliases;		/* alias list (always empty here)	*/
	int	h_addrtype;		/* AF_INET				*/
	int	h_length;		/* 4					*/
	char	**h_addr_list;		/* addresses, null terminated		*/
};
#define	h_addr	h_addr_list[0]		/* the BSD compatibility spelling	*/

struct servent {
	char	*s_name;		/* official service name		*/
	char	**s_aliases;		/* alias list (always empty here)	*/
	int	s_port;			/* port, in NETWORK byte order		*/
	char	*s_proto;		/* "tcp" or "udp"			*/
};

#define	HOST_NOT_FOUND	1
#define	TRY_AGAIN	2
#define	NO_RECOVERY	3
#define	NO_DATA		4

extern int h_errno;

struct hostent *gethostbyname();	/* (char *name)				*/
struct hostent *gethostbyaddr();	/* (char *addr, int len, int type)	*/
struct servent *getservbyname();	/* (char *name, char *proto)		*/
struct servent *getservbyport();	/* (int port, char *proto)		*/

int gethostname();			/* (char *name, int len)		*/
int sethostname();			/* (char *name, int len)		*/

#define	_PATH_HOSTS	"/etc/hosts"
/*
 * This machine's own name.  There is no hostname anywhere in the kernel -- no
 * syscall, no uname -- so it lives in a file, as it did on the BSDs before
 * uname(2) spread.
 */
#define	_PATH_HOSTNAME	"/etc/hostname"
#define	_PATH_SERVICES	"/etc/services"

#endif /* NETDB_H */
