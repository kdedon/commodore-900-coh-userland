/*
 * atc scenario-file lexer.
 *
 * Replaces the upstream lex.l.  There is no lex(1) in this tree and the token
 * set does not need one: eight fixed keywords, unsigned decimal constants, the
 * nine direction letters, '#' comments to end of line, and every other
 * printable character returned as itself for the grammar's punctuation.  The
 * rules below are in the same order as lex.l's, and the same order matters for
 * one pair -- "line" is both a keyword and a prefix of nothing else, but the
 * direction class [wedcxzaq] must be tried only after the keywords, or the `e'
 * of "exit" would lex as a direction.  Keywords are matched whole-word for the
 * same reason.
 *
 * Input is the FILE *yyin the parser was pointed at; `line' counts newlines for
 * yyerror's message.
 */
#include "include.h"
#include "grammar.h"

extern int	line;

FILE		*yyin;
char		yytext[64];

/* Keyword table, longest match not needed: all are matched whole-word. */
static struct kw {
	char	*k_name;
	int	k_tok;
} keywords[] = {
	{ "height",	HeightOp },
	{ "width",	WidthOp },
	{ "newplane",	NewplaneOp },
	{ "update",	UpdateOp },
	{ "airport",	AirportOp },
	{ "line",	LineOp },
	{ "exit",	ExitOp },
	{ "beacon",	BeaconOp },
	{ (char *) 0,	0 }
};

yylex()
{
	int		c;
	int		n;
	struct kw	*kp;

	for (;;) {
		if ((c = getc(yyin)) == EOF)
			return (0);
		if (c == ' ' || c == '\t' || c == '\r')
			continue;
		if (c == '\n') {
			line++;
			continue;
		}
		if (c == '#') {			/* comment to end of line */
			while ((c = getc(yyin)) != EOF && c != '\n')
				;
			if (c == '\n')
				line++;
			continue;
		}
		break;
	}

	if (isdigit(c)) {
		n = 0;
		do {
			n = n * 10 + (c - '0');
		} while ((c = getc(yyin)) != EOF && isdigit(c));
		if (c != EOF)
			(void) ungetc(c, yyin);
		yylval.ival = n;
		return (ConstOp);
	}

	if (isalpha(c)) {
		n = 0;
		do {
			if (n < sizeof(yytext) - 1)
				yytext[n++] = c;
		} while ((c = getc(yyin)) != EOF && isalpha(c));
		if (c != EOF)
			(void) ungetc(c, yyin);
		yytext[n] = '\0';
		for (kp = keywords; kp->k_name != (char *) 0; kp++)
			if (strcmp(kp->k_name, yytext) == 0)
				return (kp->k_tok);
		/* A single letter that is not a keyword may be a direction. */
		if (n == 1 && index("wedcxzaq", yytext[0]) != (char *) 0) {
			yylval.cval = yytext[0];
			return (DirOp);
		}
		/* Anything else is a syntax error the grammar will report; hand
		 * back the first character so the position is right. */
		return (yytext[0]);
	}

	yytext[0] = c;
	yytext[1] = '\0';
	return (c);
}
