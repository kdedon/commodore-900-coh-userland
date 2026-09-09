
typedef union {
	int	ival;
	char	cval;
} YYSTYPE;
#define HeightOp 256
#define WidthOp 257
#define UpdateOp 258
#define NewplaneOp 259
#define DirOp 260
#define ConstOp 261
#define LineOp 262
#define AirportOp 263
#define BeaconOp 264
#define ExitOp 265
#ifdef YYTNAMES
extern readonly struct yytname
{
	char	*tn_name;
	int	tn_val;
} yytnames[13];
#endif
extern	YYSTYPE	yylval;
