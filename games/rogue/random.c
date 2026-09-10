static long rntb[32] = {
	         3L, 0x9a319039L, 0x32d9c024L, 0x9b663182L, 0x5da1f342L, 
	0xde3b81e0L, 0xdf0a6fb5L, 0xf103bc02L, 0x48f340fbL, 0x7449e56bL,
	0xbeb1dbb0L, 0xab5c5918L, 0x946554fdL, 0x8c2e680fL, 0xeb3d799fL,
	0xb11ee0b7L, 0x2d436b86L, 0xda672e2aL, 0x1588ca88L, 0xe369735dL,
	0x904f35f7L, 0xd7158fd6L, 0x6fa6f051L, 0x616e6b96L, 0xac94efdcL, 
	0x36413f93L, 0xc622c298L, 0xf5a42ab8L, 0x8a88d77bL, 0xf5ad9d0eL,
	0x8999220bL, 0x27fb47b9L
};

static long *fptr = &rntb[4];
static long *rptr = &rntb[1];
static long *state = &rntb[1];
static int rand_type = 3;
static int rand_deg = 31;
static int rand_sep = 3;
static long *end_ptr = &rntb[32];

srrandom(x)
int x;
{
	register int i;
	long rrandom();

	state[0] = (long) x;
	if (rand_type != 0) {
		for (i = 1; i < rand_deg; i++) {
			state[i] = 1103515245L * state[i - 1] + 12345L;
		}
		fptr = &state[rand_sep];
		rptr = &state[0];
		for (i = 0; i < 10*rand_deg; i++) {
			(void) rrandom();
		}
	}
}

long
rrandom()
{
	long i;
	
	if (rand_type == 0) {
		i = state[0] = (state[0]*1103515245L + 12345L) & 0x7fffffffL;
	} else {
		*fptr += *rptr;
		i = (*fptr >> 1) & 0x7fffffffL;
		if (++fptr >= end_ptr) {
			fptr = state;
			++rptr;
		} else {
			if (++rptr >= end_ptr) {
				rptr = state;
			}
		}
	}
	return(i);
}

get_rand(x, y)
register int x, y;
{
	register int r, t;
	long lr;
	
	if (x > y) {
		t = y;
		y = x;
		x = t;
	}
	lr = rrandom();
	lr &= (long) 0x00007fffL;
	r = (int) lr;
	r = (r % ((y - x) + 1)) + x;
	return(r);
}

rand_percent(percentage)
register int percentage;
{
	return(get_rand(1, 100) <= percentage);
}

coin_toss()
{

	return(((rrandom() & 01) ? 1 : 0));
}
