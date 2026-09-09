# sigsel.awk -- print the signal definitions that this machine actually
# compiles, for sigconv.awk to turn into sigdesc.h.
#
# <signal.h> carries two sets of signal numbers, an _I386 one and the one
# every other port uses, and sigconv.awk reads a header as plain text: fed the
# file whole it takes both, and the second set wins wherever the numbers
# collide.  They collide badly here -- 4 is SIGILL in the _I386 set and SIGALRM
# in ours, 5 is SIGTRAP there and SIGTERM here -- so `kill -TERM' off an
# unfiltered table sends signal 15, which on this system is nothing at all.
#
# This drops the _I386 arm, nested conditionals and all, and leaves the rest.

BEGIN			{ skip = 0; depth = 0 }

/^#[ \t]*ifdef[ \t]+_I386/	{ skip = 1; depth = 1; next }

skip && /^#[ \t]*if/	{ depth++; next }
skip && /^#[ \t]*endif/	{ if (--depth == 0) skip = 0; next }
skip && /^#[ \t]*else/	{ if (depth == 1) skip = 0; next }

			{ if (!skip) print }
