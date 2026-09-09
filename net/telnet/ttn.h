/*
ttn.h
*/

#ifndef TTN_H
#define TTN_H

#define IAC		255
#define IAC_SE		240
#define IAC_NOP		241
#define IAC_DataMark	242
#define IAC_BRK		243
#define IAC_IP		244
#define IAC_AO		245
#define IAC_AYT		246
#define IAC_EC		247
#define IAC_EL		248
#define IAC_GA		249
#define IAC_SB		250
#define IAC_WILL	251
#define IAC_WONT	252
#define IAC_DO		253
#define IAC_DONT	254

#define OPT_ECHO	1
#define OPT_SUPP_GA	3
#define OPT_TERMTYPE	24

#define TERMTYPE_SEND	1
#define TERMTYPE_IS	0

#define FALSE	0
#define TRUE	(!(FALSE))

/* One pair per telnet option: whether it is negotiated, and whether this end
 * will allow it.  Both names of a pair must differ within the first SIXTEEN
 * characters, which is all the object format keeps -- SGA and TTYPE are spelled
 * short for that reason and not for brevity.  Spelled out, DO_suppress_go_ahead
 * and DO_suppress_go_ahead_allowed were one 16-character symbol and therefore
 * one int, as were the two WILL_terminal_type names.
 *
 * WILL_sga is the offer to suppress go-aheads in THIS direction, which this
 * client does not implement: a DO SUPPRESS-GO-AHEAD from the server falls to
 * do_option's default arm and is answered WONT.  The pair is declared and never
 * defined, so it costs nothing until somebody implements it. */
extern int DO_echo;
extern int DO_echo_allowed;
extern int WILL_ttype;
extern int WILL_ttype_allowed;
extern int DO_sga;
extern int DO_sga_allowed;
extern int WILL_sga;
extern int WILL_sga_allowed;

#endif /* TTN_H */
