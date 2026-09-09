/*
 * random()/srandom() over rand()/srand().  <c900/c900.h> makes these macros
 * for the sources that include it; the clients under src/clients do not, so
 * the names have to exist as functions too.  rand() here returns a 15-bit
 * value, which is what a caller of random() gets.
 *
 * The return type is int, not the long of the BSD original: no caller in
 * this tree declares random(), so a long return would be truncated through
 * the defaulted int anyway, and 15 bits fit an int exactly.
 */

int
random()
{
	return (rand());
}

void
srandom(seed)
unsigned seed;
{
	srand(seed);
}
