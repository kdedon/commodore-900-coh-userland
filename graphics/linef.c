#define TRUE -1
#define FALSE 0
#define max(x,y) (((x) > (y)) ? (x) : (y))
#define abs(x) (((x) < 0) ? -(x) : (x))
#define sign(x) ((x) > 0 ? 1 : ((x) == 0 ? 0 : (-1)))

linef(x1, y1, x2, y2, color, style, orxor, first_on)
unsigned style;
int x1, y1, x2, y2, color, orxor, first_on;
{

int ix, iy, i, inc, x, y, dx, dy, plot, plotx, ploty;
unsigned style_mask;

style = (style == 0) ? 0xFFFF : style;
style_mask = style;

dx = x2 - x1;
dy = y2 - y1;
ix = abs(dx);
iy = abs(dy);
inc = max(ix, iy);

plotx = x1;
ploty = y1;
x = y = 0;

if (first_on)
	point(plotx, ploty);

for (i = 0; i <= inc; ++i)
	{
	if (style_mask == 0) style_mask = style;
	x += ix;
	y += iy;
	plot = FALSE;

	if (x > inc)
		{
		plot = TRUE;
		x -= inc;
		plotx += sign(dx);
		}

	if (y > inc)
		{
		plot = TRUE;
		y -= inc;
		ploty += sign(dy);
		}

	if (plot && style_mask & 0x0001)
		point(plotx, ploty);

	style_mask >>= 1;
	}

}
