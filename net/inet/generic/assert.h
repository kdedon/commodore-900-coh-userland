/*
assert.h

Copyright 1995 Philip Homburg
*/
#ifndef INET_ASSERT_H
#define INET_ASSERT_H

#if !NDEBUG

void bad_assertion();
void bad_compare();

/* Statement form, not a ternary against `(void) 0': the MWC compiler rejects
 * that arm ("illegal operation on void type"), so with NDEBUG clear -- which is
 * the interesting case, since const.h ties NDEBUG to CRAMPED and this target IS
 * cramped -- the original macros would not compile at all.  These are otherwise
 * identical, and still statements, which is how the stack uses them. */
#define assert(x)	do { if (!(x)) \
				bad_assertion(this_file, __LINE__, #x); \
			} while (0)
#define compare(a,t,b)	do { if (!((a) t (b))) \
				bad_compare(this_file, __LINE__, \
					(a), #a " " #t " " #b, (b)); \
			} while (0)

#else /* NDEBUG */

#define assert(x)		0
#define compare(a,t,b)		0

#endif /* NDEBUG */

#endif /* INET_ASSERT_H */


/*
 * $PchId: assert.h,v 1.4 1995/11/21 06:45:27 philip Exp $
 */
