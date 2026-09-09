/*
 * segindex.c -- does an indexed reference to a static array reach the right
 * segment?
 *
 * huntd faults reading Maze[y][x].  The instruction is
 *
 *	SLL  R0,6			y * WIDTH2
 *	ADD  R0,x
 *	LD   R12,R0
 *	CPB  0x05:0x1841(R12),#0x20	Maze[y][x] == ' '
 *
 * -- the indexed X-mode form, where the static base becomes the instruction's
 * displacement and one register carries the combined index.  The offset is
 * right; the SEGMENT is 5, and the process has its data in segment 4.  A
 * pointer to the same array held in a register reads 04:1843, so the two
 * addressing forms disagree about which segment the array is in.
 *
 * This reproduces the shape in isolation: a 2-D static array, indexed with a
 * computed row and column, next to the same access made through a pointer.
 * If they disagree the second print is wrong or the program faults.
 *
 * It is deliberately SMALL.  huntd's data is 61906 bytes, close to a full
 * segment, and a bug that only appears near the boundary is a different bug
 * from one that appears always -- this says which.
 */
#include <stdio.h>

#define ROWS	23
#define COLS	64		/* a power of two, as hunt's WIDTH2 is */

char	grid[ROWS][COLS];
int	fails;

static void ck(what, got, want)
char *what;
int got, want;
{
	printf("segindex: %-30s got %3d want %3d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

/*
 * The neighbour scan, in the shape hunt's remap() has it: every subscript
 * biased off a common index.
 *
 * NOTE WHAT THIS DOES NOT COVER.  huntd faults because the linker adds a
 * NEGATIVE addend to a segmented address as a flat 24-bit number, so the
 * borrow runs out of the 16-bit offset and into the segment field.
 * Reaching that needs the array to sit at offset 0 of its section, so
 * that `base - 1' wraps to 0xFFFF; here `grid' is at 0x470 and `grid - 1' is
 * just 0x46F, which relocates correctly.  This test exercises the addressing
 * forms and would catch a plain indexing error -- it does not yet reproduce
 * the relocation bug, and passing it does not mean that bug is gone.
 *
 * Returns the number of disagreements with what the pointer form reads.
 */
static int scan()
{
	int y, x, bad;
	char *sp;

	bad = 0;
	for (y = 1; y < ROWS - 1; y++)
		for (x = 1; x < COLS - 1; x++) {
			sp = &grid[y][x];
			if (grid[y][x - 1] != *(sp - 1))
				bad++;
			if (grid[y][x + 1] != *(sp + 1))
				bad++;
			if (grid[y - 1][x] != *(sp - COLS))
				bad++;
			if (grid[y + 1][x] != *(sp + COLS))
				bad++;
		}
	return bad;
}

int main(argc, argv)
int argc;
char **argv;
{
	int y, x;
	char *p;

	for (y = 0; y < ROWS; y++)
		for (x = 0; x < COLS; x++)
			grid[y][x] = (char)((y * 7 + x) & 0x7F);

	/* Indexed static reference -- the form huntd faults on. */
	y = 5; x = 9;
	ck("grid[5][9] indexed", grid[y][x], (5 * 7 + 9) & 0x7F);

	y = 22; x = 63;
	ck("grid[22][63] indexed (last)", grid[y][x], (22 * 7 + 63) & 0x7F);

	/* The same byte through a pointer: a different addressing form, so if
	 * the two disagree about the segment this is where it shows. */
	y = 5; x = 9;
	p = &grid[y][x];
	ck("grid[5][9] via pointer", *p, (5 * 7 + 9) & 0x7F);

	/* And a write through the indexed form, read back through the
	 * pointer: a wrong segment on the write would go somewhere else
	 * entirely and this would read the old value. */
	grid[y][x] = 99;
	ck("write indexed, read pointer", *p, 99);

	/*
	 * A NEGATIVE bias on the index -- which is what huntd actually faults
	 * on.  remap() reads Maze[y][x - 1], and the compiler folds the -1
	 * into the static base: the far address of grid, minus one.
	 *
	 * A virtual address is not a number.  Its bits 16..23 are a hole, so a
	 * displacement has to wrap inside the 16-bit offset; added as a plain
	 * 24-bit number, -1 becomes 0xFFFF and carries into the SEGMENT.  In
	 * huntd that turned 04:1842 into 05:1841 -- right offset, wrong
	 * segment, and segment 5 is one the process does not have.
	 */
	ck("scan with [x-1] and [x+1]", scan(), 0);

	printf("segindex: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
