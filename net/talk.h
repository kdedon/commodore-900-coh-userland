/* talk.h Copyright Michael Temari 07/22/1996 All Rights Reserved */

/*
 * The ntalk(518/udp) rendezvous protocol, as /bin/talk speaks it.
 *
 * Every field is on the wire, so the widths here are the wire's and not this
 * machine's convenience: u8_t/u16_t/u32_t come from <sys/types.h>.  `struct
 * osockaddr' is the 4.2BSD sockaddr the protocol carries -- a 16-bit family
 * followed by port and address in NETWORK order -- which is why the family is
 * compared through ntohs() and not against AF_INET directly.
 *
 * A request is 84 bytes and a reply 24.  The service checks the datagram's
 * length against sizeof(struct talk_request) before it looks at anything, so a
 * padding difference between this and cmd/talk's identical copy shows up as a
 * refused datagram rather than as misread fields.  Members word-align on this
 * machine and every one of these is already on its natural boundary.
 */

#define	USER_SIZE	12
#define	TTY_SIZE	16
#define	HOST_SIZE	255

struct osockaddr {
	u16_t sa_family;
	u16_t sin_port;
	u32_t sin_addr;
	char junk[8];
};

struct talk_request {
	u8_t version;
	u8_t type;
	u8_t answer;
	u8_t junk;
	u32_t id;
	struct osockaddr addr;
	struct osockaddr ctl_addr;
	long pid;
	char	luser[USER_SIZE];
	char	ruser[USER_SIZE];
	char	rtty[TTY_SIZE];
};

struct talk_reply {
	u8_t version;
	u8_t type;
	u8_t answer;
	u8_t junk;
	u32_t id;
	struct osockaddr addr;
};

#define	TALK_VERSION	1

/* message type values */
#define LEAVE_INVITE	0	/* leave invitation with server */
#define LOOK_UP		1	/* check for invitation by callee */
#define DELETE		2	/* delete invitation by caller */
#define ANNOUNCE	3	/* announce invitation by caller */

/* answer values */
#define SUCCESS		0	/* operation completed properly */
#define NOT_HERE	1	/* callee not logged in */
#define FAILED		2	/* operation failed for unexplained reason */
#define MACHINE_UNKNOWN	3	/* caller's machine name unknown */
#define PERMISSION_DENIED 4	/* callee's tty doesn't permit announce */
#define UNKNOWN_REQUEST	5	/* request has invalid type value */
#define	BADVERSION	6	/* request has invalid protocol version */
#define	BADADDR		7	/* request has invalid addr value */
#define	BADCTLADDR	8	/* request has invalid ctl_addr value */

#define MAX_LIFE	60	/* max time daemon saves invitations */
#define RING_WAIT	30	/* time to wait before resending invitation */
