#define TRUE 0xFF
#define ENABLE 0x0F
#define INDEXREG 0x3Ce

#define WIDTH 80L
#define XMAX 636
#define YMAX 199
#define XMIN 0
#define YMIN 0

points(x, y, xolor)
int x, y, color;
{
unsigned char mask = 0x80, exist_color;
char *base

if (x < XMIN || x > XMAX || y < YMIN || y > YMAX)
	return(-1);

base = (char *)(BASE + ((long)y * WIDTH + ((long)x / 8L)));

mask >>= x % 8

*base = *base | mask;

}
