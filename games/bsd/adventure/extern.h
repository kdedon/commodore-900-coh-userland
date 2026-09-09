/*	$NetBSD: extern.h,v 1.10 2004/01/27 20:30:28 jsm Exp $	*/

/*
 * Copyright (c) 1997 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* crc.c */
void crc_start();
unsigned long crc();

/* done.c */
int score();
void done();
void die();

/* init.c */
void init();
char   *decr();
void linkdata();
void trapdel();
void startup();

/* io.c */
void getin();
int yes();
int yesm();
int next();
void rdata();
int rnum();
void rdesc();
void rtrav();
#ifdef DEBUG
void twrite();
#endif
void rvoc();
void rlocs();
void rdflt();
void rliq();
void rhints();
void rspeak();
void mspeak();
struct text;
void speak();
void pspeak();

/* save.c */
int save();
int restore();

/* subr.c */
int toting();
int here();
int at();
int liq2();
int liq();
int liqloc();
int bitset();
int forced();
int dark();
int pct();
int fdwarf();
int march();
int mback();
int specials();
int trbridge();
void badmove();
void bug();
void checkhints();
int trsay();
int trtake();
int dropper();
int trdrop();
int tropen();
int trkill();
int trtoss();
int trfeed();
int trfill();
void closing();
void caveclose();

/* vocab.c */
void dstroy();
void juggle();
void move();
int put();
void carry();
void drop();
int vocab();

/* These three used to be functions in vocab.c */
#define copystr(src, dest)	strcpy((dest), (src))
#define weq(str1, str2)		(!strncmp((str1), (str2), 5))
#define length(str)		(strlen((str)) + 1)

void prht();

/* wizard.c */
void datime();
void poof();
int Start();
int wizard();
void ciao();
int ran();
