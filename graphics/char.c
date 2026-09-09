

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

readchar(x, y, character, color)
int x, y, character, color;
{
	int i = 0;

while ((font.whole.word = stroke[character][i++]) != 0xFFFF)
	line(x + font.part.row1, y + font.part.col1,
	      x + font.part.row2, y + font.part.col2); /* ,
	      color, SOLID, NORMIT, ON); */
}
