/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * test/mkfsgetlink/getlink.c -- the name-matching loop inside mkfs(1M)'s
 * getlink(), on the host, over a directory built for the cases where the
 * two versions of mkfs disagreed.
 *
 * getlink() resolves a path in the PROTOTYPE filesystem mkfs is building, so
 * a wrong answer links a file to the wrong inode -- or reports a name that is
 * there as missing -- in a filesystem that is then written out and checks
 * clean.  There is no wrong-looking output to notice.
 *
 * The subject is the inner loop only.  Everything around it -- the xnode
 * table, the '/' descent, the assertions -- is modelled just far enough to
 * drive the loop with real directories and real names.
 *
 * A directory entry's name is a NUL-terminated string (struct entre holds a
 * char *), and a name matches an entry when both end at the same character.
 * A name ends at NUL or at '/'; an entry's name ends at its NUL or at DIRSIZ,
 * because that is all a directory record holds.
 *
 * The loop to run is argv[1] -- `merged', `stock' or `base', merged by
 * default.  It is an argument and not an environment variable because the
 * emulator's process runner passes argv to the guest and does not pass the
 * environment.
 *
 * `merged' is the one mkfs carries.  `stock' and `base' are the two that were
 * in the tree, kept here as the negative controls: each of them must fail at
 * least one case, or this file is measuring nothing.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DIRSIZ	14

struct entre {
	int	 e_ino;
	char	*e_name;
};

struct xnode {
	int		 x_isdir;
	int		 x_nent;
	struct entre	*x_ents;
};

/* The inode table getlink() indexes; index 0 is inode 1. */
static struct xnode *X[64];

enum { L_MERGED, L_STOCK, L_BASE };
static int loop;

static int
getlink(inum, name)
int inum;
char *name;
{
	int i, nent;
	struct entre *ep;
	struct xnode *xp;

	xp = X[inum-1];
nextname:
	if (xp == NULL)
		return (-1);
	ep = xp->x_ents;
	nent = xp->x_nent;
	while (*name == '/')
		name += 1;
	if (*name == 0)
		return (inum);
	while (--nent >= 0) {
		if (ep->e_ino == 0) {
			ep += 1;
			continue;
		}
		if (loop == L_MERGED) {
			for (i = 0; ; i += 1) {
				int nc, ec;

				nc = (name[i] == '\0' || name[i] == '/')
				   ? 0 : name[i];
				ec = (i == DIRSIZ) ? 0 : ep->e_name[i];
				if (nc != ec)
					break;
				if (nc == 0) {
					inum = ep->e_ino;
					if (name[i] == 0)
						return (inum);
					name += i;
					xp = X[inum-1];
					if (xp == NULL || !xp->x_isdir)
						return (0);
					goto nextname;
				}
			}
		} else if (loop == L_STOCK) {
			for (i = 0; ; i += 1) {
				if (i == DIRSIZ || ep->e_name[i] == 0) {
					inum = ep->e_ino;
					if (name[i] == 0)
						return (inum);
					if (name[i] == '/') {
						name += i;
						xp = X[inum-1];
						if (xp == NULL || !xp->x_isdir)
							return (0);
						goto nextname;
					}
					return (0);
				}
				if (name[i] != ep->e_name[i])
					break;
			}
		} else {
			for (i = 0; ; i += 1) {
				if (name[i] != '\0' && name[i] != '/'
				  && name[i] != ep->e_name[i])
					break;
				if (i == DIRSIZ || ep->e_name[i] == 0) {
					inum = ep->e_ino;
					if (name[i] == 0)
						return (inum);
					if (name[i] == '/') {
						name += i;
						xp = X[inum-1];
						if (xp == NULL || !xp->x_isdir)
							return (0);
						goto nextname;
					}
					return (0);
				}
			}
		}
		ep += 1;
	}
	return (0);
}

static int fails;

static void
expect(what, path, want)
char *what, *path;
int want;
{
	char buf[128];
	int got;

	/*
	 * The name is copied into a buffer whose tail is NUL, which is the
	 * layout a caller in mkfs supplies: this is what lets a loop that
	 * runs past the terminator come back with a plausible answer rather
	 * than a crash, and it is the case that must be caught by its ANSWER.
	 */
	memset(buf, 0, sizeof buf);
	strcpy(buf, path);
	got = getlink(1, buf);
	if (got == want)
		printf("  ok   %-46s -> %d\n", what, got);
	else {
		printf("  FAIL %-46s -> %d, want %d\n", what, got, want);
		fails += 1;
	}
}

static struct xnode *
dir(n, ents)
int n;
struct entre *ents;
{
	struct xnode *xp = (struct xnode *)malloc(sizeof *xp);

	xp->x_isdir = 1;
	xp->x_nent = n;
	xp->x_ents = ents;
	return xp;
}

static struct xnode *
plain()
{
	struct xnode *xp = (struct xnode *)malloc(sizeof *xp);

	xp->x_isdir = 0;
	xp->x_nent = 0;
	xp->x_ents = NULL;
	return xp;
}

main(argc, argv)
int argc;
char **argv;
{
	char *w = (argc > 1) ? argv[1] : NULL;
	static struct entre root[] = {
		{ 2, "ab" },
		{ 3, "abc" },
		{ 0, "hole" },
		{ 4, "sub" },
		{ 5, "abcdefghijklmn" },		/* exactly DIRSIZ */
		{ 6, "z" },
		/*
		 * The longer name FIRST.  The order matters: a loop that
		 * only breaks on a character it can see -- and so runs on
		 * past the end of the shorter name -- reaches the end of the
		 * entry and takes it.  With the shorter entry first the same
		 * loop matches it before the hazard is reached and looks
		 * right.
		 */
		{ 9, "qrs" },
		{ 10, "qr" },
		{ 11, "abcdefghijklm" },		/* DIRSIZ-1 */
	};
	static struct entre sub[] = {
		{ 7, "in" },
		{ 8, "inner" },
		{ 12, "deeper" },
		{ 13, "deep" },
	};

	loop = L_MERGED;
	if (w != NULL && strcmp(w, "stock") == 0)
		loop = L_STOCK;
	else if (w != NULL && strcmp(w, "base") == 0)
		loop = L_BASE;
	printf("loop=%s\n", w == NULL ? "merged" : w);

	X[0] = dir(9, root);			/* inode 1, the root */
	X[1] = plain();				/* 2 ab */
	X[2] = plain();				/* 3 abc */
	X[3] = dir(4, sub);			/* 4 sub */
	X[4] = plain();				/* 5 abcdefghijklmn */
	X[5] = plain();				/* 6 z */
	X[6] = plain();				/* 7 sub/in */
	X[7] = plain();				/* 8 sub/inner */
	X[8] = plain();				/* 9 qrs */
	X[9] = plain();				/* 10 qr */
	X[10] = plain();			/* 11 abcdefghijklm */
	X[11] = plain();			/* 12 sub/deeper */
	X[12] = plain();			/* 13 sub/deep */

	/* 1. the plain case, and the entry is not the first one */
	expect("exact match, later entry", "z", 6);
	/* 2. a name that is a PREFIX of an entry is not that entry */
	expect("`ab' must not match the entry `abc'", "ab", 2);
	/* 3. and the entry it does match sits BEFORE the longer one */
	expect("`abc' past the shorter entry `ab'", "abc", 3);
	/* 4. a name no entry holds */
	expect("no such name", "nosuch", 0);
	/* 5. a hole in the directory is skipped, not matched */
	expect("hole entry not matched", "hole", 0);
	/* 6. descent through a directory component */
	expect("descend: sub/inner", "sub/inner", 8);
	/* 7. and the same prefix hazard one level down */
	expect("descend: sub/in, the shorter of the two", "sub/in", 7);
	/* 8. a component ending at '/' matches an entry ending at NUL */
	expect("trailing slash on a directory", "sub/", 4);
	/* 9. a name of exactly DIRSIZ characters */
	expect("exactly DIRSIZ characters", "abcdefghijklmn", 5);
	/* 10. one character more than the entry holds is not that entry */
	expect("DIRSIZ+1 characters must not match", "abcdefghijklmno", 0);
	/* 11. descending through something that is not a directory */
	expect("`ab' is not a directory", "ab/x", 0);
	/* 12. the shorter name, with the LONGER entry ahead of it */
	expect("`qr' past the longer entry `qrs'", "qr", 10);
	/* 13. the same one level down */
	expect("descend: sub/deep past sub/deeper", "sub/deep", 13);
	/* 14. and at the DIRSIZ boundary, where the bound ends the entry */
	expect("DIRSIZ-1 past the DIRSIZ-long entry", "abcdefghijklm", 11);

	if (fails == 0)
		printf("PASS\n");
	else
		printf("FAIL: %d case(s)\n", fails);
	return fails != 0;
}
