# cmdflags.sh -- the per-command include paths, defines and extra link inputs
# a base/cmd source needs.  SOURCE this file, then call `cmdflags <name>'; it
# sets $IORD and $XLIB, or returns 1 for a command this sweep cannot build.
#
#	. "$OS/hostbuild/cmdflags.sh"
#	cmdflags ls || continue
#	$CCZ -s -i $IORD -o out ls.c $XLIB
#
# A copy of build-userland.sh's table.  $OS, $TCB and $KINC must be set.
cmdflags() {
	IORD="-I $OS/base/cmd"
	XLIB=""
	case "$1" in
	# Built elsewhere with flags of their own, or not commands at all.
	clear|top|ps|pr|tr|cgrep|env|execvep|lock)	return 1;;
	date|sort|tail|units)	IORD="$IORD -D COHERENT=1";;
	factor)			XLIB="$TCB/libm-z8001/libm-z8001.a";;
	init)			XLIB="$OS/base/cmd/lock.c";;
	dcheck|icheck|ncheck)
		IORD="$IORD -I $OS/base/lib/libfs"
		XLIB="$(ls "$OS"/base/lib/libfs/*.c | tr '\n' ' ')";;
	load|uload|mount|sa|time|fdformat)
		[ -n "${KINC:-}" ] || return 1
		IORD="$IORD -I $KINC -I $KINC/sys";;
	esac
	return 0
}
