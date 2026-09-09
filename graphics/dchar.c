

unsigned stroke[128][36] = {
0x0005, 0x0516, 0x1626, 0x2635, 0x3532, 0x3546,
0x4656, 0x5665, 0x6560, 0x0161, 0x0262, 0xFFFF,
0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0
};

#define TRUE -1
#define FALSE 0
#define ON TRUE
#define OFF FALSE
#define SOLID 0xFFFF
#define NORMIT 0
#define YMAX 199
#define XMETRIC 0.237546468
#define YMETRIC 0.096601942


union
	{
	struct
		{
		unsigned col2 : 4;
		unsigned row2 : 4;
		unsigned col1 : 4;
		unsigned row1 : 4;
		} part;
	struct
		{
		unsigned word;
		} whole;
	} font;

dispchar(x, y, character, color)
double x, y;
int character, color;
{

	double x1, y1, x2, y2;
	int x1_out, y1_out, x2_out, y2_out;
	int i = 0;

while ((font.whole.word = stroke[character][i++]) != 0xFFFF)
	{
	x1 = x + font.part.col1;
	y1 = y - font.part.row1;
	x2 = x + font.part.col2;
	y2 = y - font.part.row2;

	xform(&x1, &y1);
	xform(&x2, &y2);

	x1_out = x1 * XMETRIC;
	y1_out = YMAX - y1 * YMETRIC;
	x2_out = x2 * XMETRIC;
	y2_out = YMAX - y2 * YMETRIC;

	/* linef(x1_out, y1_out, x2_out, y2_out, color, SOLID, NORMIT, ON);
*/
	line((int)x1_out, (int)y1_out, (int)x2_out, (int)y2_out);
	}
}
