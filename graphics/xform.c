#include <math.h>

xform(x, y)
double *x, *y;
{
double x_temp;
extern double scale_factor;

*x *= scale_factor;
*y *= scale_factor;

}
