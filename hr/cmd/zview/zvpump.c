/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * zvpump -- forward keyboard and mouse input to zview.
 *
 * Read CIOGETM events and write C_INPUT records to HR_CMDFD.
 * The driver draws the cursor.  A separate executable keeps the blocking
 * input process small while the server waits on its command pipe.
 */
#include "smgr.h"
#include "wire.h"

/* the driver's default arrow cursor sprite (zview keeps its own copy for
 * restoring the arrow after menu/drag cursors).  Two planes for CIOMOUSE:
 * ink (1 = black) then the opacity mask (ink | white outline) -- keep in
 * sync with zview.c DEF_MOUSE. */
static int DEF_MOUSE[] = { 0x0000, 0x7ffe, 0x7ffc, 0x7ff8,
			   0x7ff0, 0x7fe0, 0x7fe0, 0x7ff0,
			   0x7ff8, 0x7ffc, 0x7ffe, 0x79ff,
			   0x70ff, 0x407f, 0x003f, 0x001f,
			   /* mask */
			   0xffff, 0xffff, 0xffff, 0xfffe,
			   0xfffc, 0xfff8, 0xfff8, 0xfffc,
			   0xfffe, 0xffff, 0xffff, 0xffff,
			   0xffff, 0xf9ff, 0xe0ff, 0x007f };

/*
 * Translate keyboard scancodes to ASCII and HRK_* events.
 * Navigation keys and modified function keys use MicroEMACS chords;
 * keymap() lists the bindings.  F10 sends ^X ^C.
 *
 * F1-F10 and Help (0x54) are known on hardware.  The other C900 specials
 * use gfx/kbd.h slots 0x5A..0x5D, with 0x5F as alternate Help; those
 * assignments work in the emulator but need hardware confirmation.
 */
#define KB_KEYUP	0x80
#define KB_KEYSC	0x7f
#define KB_LSHIFT	(0x2a-1)
#define KB_RSHIFT	(0x36-1)
#define KB_CTRL		(0x1d-1)
#define KB_ALT		(0x38-1)
#define KB_CAPLOCK	(0x3a-1)
#define KB_SRS	0x01
#define KB_SLS	0x02
#define KB_CTS	0x04
#define KB_ALS	0x08
#define KB_CPLS	0x10
#define KB_NMLS	0x20
#define KB_SHFT	0x80
#define KB_SES	(KB_SLS|KB_SRS)
#define KB_SS1	(KB_SLS|KB_SRS|KB_CTS)
#define KB_LET	(KB_SLS|KB_SRS|KB_CPLS|KB_CTS)
#define XXX	0377
#define SPC	0376

static unsigned char lmaptab[] ={
	     '\33',  '1',  '2',  '3',  '4',  '5',  '6',
	 '7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',
	 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
	 'o',  'p',  '[',  ']', '\r',  XXX,  'a',  's',
	 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
	 '\'', '`',  XXX,  '\\',  'z',  'x',  'c',  'v',
	 'b',  'n',  'm',  ',',  '.',  '/',  XXX,  SPC,
	 XXX,  ' ',  XXX, '\201','\202','\203','\204','\205',
	'\206','\207','\210','\211','\212', SPC,  SPC, '\001',
	'\020','\032', '-', '\002', SPC, '\006', '+', '\005',
	'\016','\026', SPC,  DEL, '\213', DEL,  SPC,  SPC,
	 SPC,  SPC, '\214','\215','\216','\217', '\r','\213',
	 SPC,  SPC,  SPC,  XXX,  XXX
};
static unsigned char umaptab[] ={
	     '\33',  '!',  '@',  '#',  '$',  '%',  '^',
	 '&',  '*',  '(',  ')',  '_',  '+', '\b', '\t',
	 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
	 'O',  'P',  '{',  '}', '\r',  XXX,  'A',  'S',
	 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
	 '"',  '~',  XXX,  '|',  'Z',  'X',  'C',  'V',
	 'B',  'N',  'M',  '<',  '>',  '?',  XXX,  SPC,
	 XXX,  ' ',  XXX, '\201','\202','\203','\204','\205',
	'\206','\207','\210','\211','\212', SPC,  SPC, '\001',
	'\020','\032', '-', '\002', SPC, '\006', '+', '\005',
	'\016','\026', SPC,  DEL, '\213', DEL,  SPC,  SPC,
	 SPC,  SPC, '\214','\215','\216','\217', '\r','\213',
	 SPC,  SPC,  SPC,  XXX,  XXX
};
#define SS0	0
#define SS1	(KB_SLS|KB_SRS|KB_CTS)
#define SES	(KB_SLS|KB_SRS)
#define LET	(KB_SLS|KB_SRS|KB_CPLS|KB_CTS)
#define KEY	(KB_SLS|KB_SRS|KB_NMLS|0x40)
#define SHFT	KB_SHFT
static unsigned char smaptab[] ={
	       SS0,  SES,  SS1,  SES,  SES,  SES,  SS1,
	 SES,  SES,  SES,  SES,  SS1,  SES,  SS0,  SS0,
	 LET,  LET,  LET,  LET,  LET,  LET,  LET,  LET,
	 LET,  LET,  SS1,  SS1,  SS0, SHFT,  LET,  LET,
	 LET,  LET,  LET,  LET,  LET,  LET,  LET,  SES,
	 SES,  SS1, SHFT,  SS1,  LET,  LET,  LET,  LET,
	 LET,  LET,  LET,  SES,  SES,  SES, SHFT,  SS0,
	SHFT,  SS1, SHFT,  SS0,  SS0,  SS0,  SS0,  SS0,
	 SS0,  SS0,  SS0,  SS0,  SS0,  SS0,  KEY,  KEY,
	 KEY,  KEY,  SS0,  KEY,  KEY,  KEY,  SS0,  KEY,
	 KEY,  KEY,  KEY,  KEY,  SS0,  SS0,  SS0,  SS0,
	 SS0,  SS0,  SS0,  SS0,  SS0,  SS0,  SS0,  SS0,
	 SS0,  SS0,  SS0,  LET,  LET
};
#undef SHFT

static int kbshift = 0;

/* Translate a raw scancode to ASCII; return -1 for releases, modifiers and
 * dead/special keys (which the server ignores). */
static
keymap(r)
int r;
{
	register int c, s;

	r &= 0xff;
	if ( r == 0xff )
		return -1;
	c = (r & KB_KEYSC) - 1;
	if ( c < 0 || c >= sizeof(smaptab) )
		return -1;
	s = smaptab[c];
	if ( s & KB_SHFT )
	{
		if ( r & KB_KEYUP )
		{
			if ( c == KB_RSHIFT ) kbshift &= ~KB_SRS;
			else if ( c == KB_LSHIFT ) kbshift &= ~KB_SLS;
			else if ( c == KB_CTRL ) kbshift &= ~KB_CTS;
			else if ( c == KB_ALT ) kbshift &= ~KB_ALS;
		}
		else
		{
			if ( c == KB_LSHIFT ) kbshift |= KB_SLS;
			else if ( c == KB_RSHIFT ) kbshift |= KB_SRS;
			else if ( c == KB_CTRL ) kbshift |= KB_CTS;
			else if ( c == KB_ALT ) kbshift |= KB_ALS;
			else if ( c == KB_CAPLOCK ) kbshift ^= KB_CPLS;
		}
		return -1;
	}
	if ( r & KB_KEYUP )
		return -1;
	/* The Shift and Ctrl layers of the nav/function keys return a
	 * CHORD-MARKED code: 0x200 = ESC prefix, 0x400 = ^X prefix, low
	 * byte the second key (main() emits two IN_KEY records).  Each case
	 * below names the me(1) command it stands for. */
	if ( (kbshift & KB_CTS) == 0 && (kbshift & KB_SES) != 0 )
	{
		switch ( c )
		{		/* the whole SHIFT layer */
		case 0x48-1:	return 0x200 | '<';	/* S+Up = top      */
		case 0x50-1:	return 0x200 | '>';	/* S+Down = end    */
		case 0x4b-1:	return 0x200 | 'b';	/* S+Left = word<  */
		case 0x4d-1:	return 0x200 | 'f';	/* S+Right = word> */
		case 0x3b-1:	return 0x200 | '!';	/* F1 reposition   */
		case 0x3c-1:	return 0x400 | 027;	/* F2 save as      */
		case 0x3d-1:	return 0x400 | 'b';	/* F3 new / use
							 * buffer          */
		case 0x3e-1:	return 0x400 | 030;	/* F4 swap mark    */
		case 0x3f-1:	return 0x400 | 025;	/* F5 upper region */
		case 0x40-1:	return 0x400 | 014;	/* F6 lower region */
		case 0x41-1:	return 022;		/* F7 search back  */
		case 0x42-1:	return 0x200 | 'd';	/* F8 delete word  */
		case 0x43-1:	return 0x400 | '(';	/* F9 begin macro  */
		case 0x5a-1:	return 0x200 | '>';	/* Clear/Home =
							 * end of buffer   */
		case 0x5b-1:	return 0x400 | 'p';	/* Pop/Push =
							 * prev me window  */
		case 0x5c-1:	return 0x400 | 032;	/* Scrn/Prt =
							 * shrink window   */
		case 0x5d-1:	return 0x400 | 'e';	/* Stop/CONTINUE =
							 * execute macro   */
		}
	}
	if ( (kbshift & KB_CTS) == 0 && c == 0x49-1 )
		return 0x200 | 'v';	/* PgUp = M-v page up -- NOT ^Z,
					 * which is me(1)'s save-and-exit */
	if ( kbshift & KB_CTS )
	{
		if ( s == KB_SS1 || s == KB_LET )
			c = umaptab[c] & 0x1f;
		else if ( c == 0x48-1 )		/* Ctrl+Up    = PgUp = M-v */
			return 0x200 | 'v';
		else if ( c == 0x50-1 )		/* Ctrl+Down  = PgDn */
			c = 'V' & 0x1f;
		else if ( c == 0x4b-1 )		/* Ctrl+Left  = Home */
			c = 'A' & 0x1f;
		else if ( c == 0x4d-1 )		/* Ctrl+Right = End  */
			c = 'E' & 0x1f;
		else if ( c == 0x3b-1 )		/* Ctrl+F1 = capitalise word */
			return 0x200 | 'c';
		else if ( c == 0x3c-1 )		/* Ctrl+F2 = set file name  */
			return 0x400 | ('F' & 0x1f);
		else if ( c == 0x3d-1 )		/* Ctrl+F3 = revert (read)  */
			return 0x400 | ('R' & 0x1f);
		else if ( c == 0x3e-1 )		/* Ctrl+F4 = upper word     */
			return 0x200 | 'u';
		else if ( c == 0x3f-1 )		/* Ctrl+F5 = split window   */
			return 0x400 | '2';
		else if ( c == 0x40-1 )		/* Ctrl+F6 = one window     */
			return 0x400 | '1';
		else if ( c == 0x41-1 )		/* Ctrl+F7 = lower word     */
			return 0x200 | 'l';
		else if ( c == 0x42-1 )		/* Ctrl+F8 = del word back  */
			return 0x200 | 010;
		else if ( c == 0x43-1 )		/* Ctrl+F9 = end macro      */
			return 0x400 | ')';
		else if ( c == 0x44-1 )		/* Ctrl+F10 = quickexit     */
			return 032;
		else if ( c == 0x5a-1 )		/* Ctrl+Clear/Home =
						 * kill buffer              */
			return 0x400 | 'k';
		else if ( c == 0x5b-1 )		/* Ctrl+Pop/Push =
						 * enlarge window           */
			return 0x400 | 'z';
		else if ( c == 0x5c-1 )		/* Ctrl+Scrn/Prt =
						 * show position            */
			return 0x400 | '=';
		else if ( c == 0x5d-1 )		/* Ctrl+Stop/Cont =
						 * list buffers             */
			return 0x400 | 002;
		else
			return -1;
	}
	else if ( s &= kbshift )
	{
		if ( kbshift & KB_SES )
			c = (s & (KB_CPLS|KB_NMLS)) ? lmaptab[c] : umaptab[c];
		else
			c = (s & (KB_CPLS|KB_NMLS)) ? umaptab[c] : lmaptab[c];
	}
	else
		c = lmaptab[c];
	if ( c == XXX || c == SPC )
		return -1;
	return c & 0xff;
}

main()
{
	int fd;
	MESSAGE m;
	WMSG c;

	fd = open("/dev/smgr", 2);
	if ( fd < 0 )
		_exit(1);
	ioctl(fd, CIOEVMGR);
	ioctl(fd, CIOMOUSE, DEF_MOUSE);
	ioctl(fd, CIOMSEON, (char *)0);

	c.wm_wid = 0;
	c.wm_type = C_INPUT;
	for (;;)
	{
		if ( ioctl(fd, CIOGETM, &m) < 0 )
			continue;
		if ( m.msg_Cmd == SM_MOUSE )
		{
			c.wm_arg[0] = IN_MOVE;
			c.wm_arg[1] = m.msg_Data[1] & 0x1fff;
			c.wm_arg[2] = m.msg_Data[2] & 0x1fff;
		}
		else if ( m.msg_Cmd == SM_MKEY )
		{
			c.wm_arg[0] = IN_BUTTON;
			c.wm_arg[1] = m.msg_Data[1] & 0x1fff;	/* x            */
			c.wm_arg[2] = m.msg_Data[2] & 0x1fff;	/* y            */
			c.wm_arg[3] = m.msg_Data[2] & 0xe000;	/* buttons down */
			c.wm_arg[4] = m.msg_Data[1] & 0xe000;	/* changed bits */
		}
		else if ( m.msg_Cmd == SM_KKEY )
		{
			int a = keymap(m.msg_Data[1]);
			if ( a < 0 )
				continue;	/* release / modifier / dead key */
			c.wm_arg[0] = IN_KEY;
			if ( a & 0x600 )
			{	/* chord-marked (see keymap): prefix record,
				 * then the low byte as the second key */
				c.wm_arg[1] = (a & 0x400) ? ('X' & 0x1f) : 033;
				write(HR_CMDFD, &c, sizeof(c));
				a &= 0xff;
			}
			else if ( (kbshift & KB_ALS) && a >= HRK_F1 && a <= HRK_F5 )
				a += HRK_ALTFN;	/* Alt+F1..F5: window-op
						 * shortcuts (wire.h HRK_AF*) */
			else if ( a == HRK_F10 )
			{		/* F10 = the MicroEMACS quit chord ^X ^C
				 * (see the keymap comment above) */
				c.wm_arg[1] = 'X' & 0x1f;
				write(HR_CMDFD, &c, sizeof(c));
				a = 'C' & 0x1f;
			}
			c.wm_arg[1] = a;
		}
		else
			continue;
		write(HR_CMDFD, &c, sizeof(c));
	}
}
