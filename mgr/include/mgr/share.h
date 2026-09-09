#ifndef _MGR_SHARE_H
#define _MGR_SHARE_H

#ifdef MOVIE

#include <mgr/bitblit.h>
#include <stdio.h>

/* types */

#define OP_MASK		0xF		/* opcode part of type */
#define TYPE_MASK	0xF0

#define T_NOP		0x00		/* do a nop */
#define T_BLIT		0x10		/* do a bit-blt */
#define T_WRITE		0x20		/* do a bit-blt */
#define T_LINE		0x30		/* do a line */
#define T_POINT		0x40		/* do a point */
#define T_DATA		0x50		/* send some data */
#define T_KILL		0x60		/* destroy a bitmap */
#define T_SCREEN	0x70		/* define the screen */
#define T_TIME		0x80		/* current time (100'th of a second) */
#define T_BLIT2		0x90		/* compressed bit-blit for chars */
#define T_MARK		0xa0      	/* network marks */
#define T_BYTESCROLL	0xb0		/* do scrolling using fast scroll */

struct share_msg
{
	unsigned short type;		/* message type */
	unsigned short stuff[8];	/* other stuff */
};

/* stuff for getting bitmap id's */

#define MAX_MAPS	4000		/* max # bitmap id's */

extern int log_noinitial;

void log_blit();
void log_line();
void log_point();
void log_open();
void log_destroy();
void log_alloc();
void log_create();
void send_sync();
void log_time();
int log_start();
int log_end();

#endif

#endif /* MGR_SHARE_H */
