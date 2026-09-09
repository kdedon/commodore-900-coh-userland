/*
 * netdb_priv.h -- what netdb.c and gethostby.c share, and nothing else does.
 *
 * One host lookup lives in two translation units so that the DNS resolver is
 * linked only into programs that resolve a name (see netdb.c's header).  This
 * header carries the seam, and nothing outside the library includes it.
 *
 * BOTH DECLARATIONS RETURN A POINTER AND BOTH ARE LOAD-BEARING.  Without them a
 * K&R compile defaults the function to int, and a struct hostent * here is a
 * 32-bit far pointer: the segment half is dropped and the caller dereferences an
 * offset in segment zero.
 */
#ifndef NETDB_PRIV_H
#define NETDB_PRIV_H

/*
 * Fill the shared static hostent from a name and an address (host order for the
 * name's sake, network order for the address) and return it.
 */
struct hostent *_host_answer();		/* (char *name, unsigned long addr)	*/

/*
 * Scan /etc/hosts.  Search by name, or -- with a null name -- by address.
 * Returns null when the file has no answer or does not exist.
 */
struct hostent *_hosts_lookup();	/* (char *name, unsigned long addr)	*/

#endif /* NETDB_PRIV_H */
