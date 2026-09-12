#!/bin/sh
# build-cmd.sh <dir> -- compile all .c in a multi-file command directory and
# link the native binaries it holds.  Separated-I/D + large model.
# Skips *.host / setup / snscore-style aux; libc + the games-lib gap fills.
#
# One DIRECTORY is not one PROGRAM.  A dozen of these directories hold two or
# more programs, each with its own main(), sharing a support file: the whole
# glob on one link line gives `Ld: symbol main_: redefined' and a binary whose
# main() is whichever the linker happened to keep.  Such a directory names its
# extra programs in $AUX and its own sources in $SRCS; see the case below.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
DIR="$1"; name="$(basename "$DIR")"
# libc-z8001.a supplies string routines and getopt.
# games/lib/src supplies err(3) and fgetln(3).
GLIB=""
INC="-I $DIR -I $OS/include -I $OS/include/sys"
mkdir -p "$BIN"
# yyparse.c is the yacc parser SKELETON (has a `$A' action placeholder, installed to
# /lib, never compiled directly), so drop it as well as the y.tab/setup/.host aux.
SRCS=$(ls "$DIR"/*.c 2>/dev/null | grep -vE '\.host|/setup\.|/y\.tab|/yyparse\.c' | tr '\n' ' ')
[ -n "$SRCS" ] || { echo "$name: no .c"; exit 1; }
# Per-command extra sources / includes the plain one-directory glob misses:
#   knapsack -- multiple-precision lib (xgcd/...), like bc.
#   diff -- diffh.c is a SEPARATE program installed as /usr/lib/diffh, which
#           diff execv()s for -h and for -d on a large file.  It supplies its
#           own diff() and diffh(), so compiled beside diff1.c the link carries
#           two definitions of each and /usr/lib/diffh does not exist at all.
#
# $AUX holds one record per EXTRA program in the directory, `name src...', with
# records separated by a bare `;'.  Each record links its own binary; $EXTRA is
# added to every one of them, because a directory's private library (libmp for
# knapsack) is wanted by each program in it.  $OUT names the binary built from
# $SRCS when it is not the directory's own name -- cmd/knapsack builds enroll,
# xencode and xdecode and no `knapsack' at all.
EXTRA=""
AUX=""
OUT=""
case "$name" in
diff)	SRCS="$DIR/diff1.c $DIR/diff2.c"
	AUX="diffh $DIR/diffh.c $DIR/diff2.c";;
# cu -- the directory holds THREE programs' worth of source.  cuxcvr.c is the
#	transceiver cu runs on the REMOTE machine to move a file: cu types
#	"/etc/cuxcvr " down the line (cu.c:731), so it is a separate binary with
#	its own main(), sharing only cudld.c, the packet layer.  cun.c is a debug
#	copy of cu.c (one added printf) and defines every one of cu's symbols a
#	second time.  Globbing the directory therefore links six duplicate
#	symbols, main() among them, and ships no /etc/cuxcvr at all.
cu)	SRCS="$DIR/cu.c $DIR/cudld.c"
	AUX="cuxcvr $DIR/cuxcvr.c $DIR/cudld.c";;
# The rest of the multi-program directories.  Each split is the one the
# directory's OWN build recipe makes -- cmd/<name>/run, or cmd/knapsack/makefile
# -- not a guess at which main() looks most important.
#
# dump(1)/restor(1) are the filesystem dumper and its restorer and share
# discbuf.c, the raw-device block cache; dumpdate(1) and dumpdir(1) are the two
# report programs and are one file each (cmd/dump/run).
dump)	SRCS="$DIR/dump.c $DIR/discbuf.c"
	AUX="restor $DIR/restor.c $DIR/discbuf.c
	     ; dumpdate $DIR/dumpdate.c
	     ; dumpdir $DIR/dumpdir.c";;
# knapsack is the public-key mail package: enroll(1) makes a user's key pair,
# xencode(1)/xdecode(1) encrypt and decrypt with it.  gpph.c/knapsack.c are the
# knapsack arithmetic, public.c/pkio.c the key file.  Per cmd/knapsack/makefile
# xdecode does NOT link public.c/pkio.c -- it reads the key off its stdin.
knapsack)
	EXTRA="$(ls "$OS"/base/lib/libmp/*.c 2>/dev/null | tr '\n' ' ')"
	KCOM="$DIR/gpph.c $DIR/knapsack.c"
	OUT=enroll
	SRCS="$KCOM $DIR/public.c $DIR/pkio.c $DIR/enroll.c"
	AUX="xencode $KCOM $DIR/public.c $DIR/pkio.c $DIR/xencode.c
	     ; xdecode $KCOM $DIR/xdecode.c";;
# lpr is FOUR programs (cmd/lpr/run): lpr(1) queues a job into /usr/spool/lpd,
# lpd is the daemon that prints it (lpd1.c main + lpd2.c + print.c) and is
# installed as /usr/lib/lpd, lpskip(1) aborts the current page, opr(1) is the
# stand-alone printer copy.  Globbed into one binary the main() the linker kept
# was lpd1.c's, so /bin/lpr was originally the daemon.
lpr)	SRCS="$DIR/lpr.c"
	AUX="lpd $DIR/lpd1.c $DIR/lpd2.c $DIR/print.c
	     ; lpskip $DIR/lpskip.c
	     ; opr $DIR/opr.c";;
# spell(1) proper is /usr/lib/spell, the hashed-dictionary lookup that
# cmd/spell/spellcmd (shipped as /bin/spell) pipes deroff|sort into; spellin(1)
# builds that hashed list from a word list.  Both link spell2.c, the hash.
spell)	SRCS="$DIR/spell.c $DIR/spell2.c"
	AUX="spellin $DIR/spellin.c $DIR/spell2.c";;
# db(1) is the symbolic debugger, and the one command whose source is split by
# MACHINE: trace0.c-trace6.c are machine-independent and the Z8001 half -- the
# instruction table, the ptrace() layer and the stack unwinder -- lives in
# z8001/, which the directory's own glob does not reach.  <mtrace.h> and
# "z8001.h" are found on the include path, so the machine directory joins it.
db)	SRCS="$(ls "$DIR"/trace[0-6].c "$DIR"/z8001/*.c | tr '\n' ' ')"
	# db reads the traced process's u-area and its registers out of it, so
	# it compiles against the KERNEL's headers: <uproc.h> pulls in
	# <sys/proc.h>, which pulls in <sys/timeout.h>, and <sys/machine.h>
	# pulls in <sys/machz8001.h> -- neither exists in this tree or the
	# toolchain.  Empty $KINC means no kernel resolved, and db refuses by
	# name rather than compiling against something else.
	if [ -z "${KINC:-}" ]; then
		echo "db: FAIL -- needs the kernel headers; sh mk/deps.sh -n kernel"
		exit 1
	fi
	INC="$INC -I $DIR/z8001 -I $KINC -I $KINC/sys";;
# egrep carries MSDOS/GEMDOS/COHERENT arms; COHERENT selects <access.h> and the
# -A option's call out to me(1).  Without it the -A path has no buffer declared.
egrep)	INC="$INC -DCOHERENT";;
# These build from a yacc grammar and belong to build-yacc-cmd.sh, which
# regenerates the parser from the .y.  This script has no yacc pass and would
# link a parserless binary over the staged one -- refuse rather than produce it.
sh|rsh|find|awk)
	echo "$name: use build-yacc-cmd.sh (yacc grammar)"; exit 1;;
# bc's parser comes from gram.y through a yacc invocation with hand-set table
# sizes and a sed pass over the terminal-name table, and dc links six of bc's
# objects, so the two are one build: build-bc.sh.  Globbing cmd/bc here links a
# bc with no yyparse at all.
bc|dc)	echo "$name: use build-bc.sh (yacc grammar; bc and dc together)"; exit 1;;
esac
SRCS="$SRCS $EXTRA"
: "${OUT:=$name}"
mkdir -p "$HERE/build"
# `Ld: symbol main_: redefined' is the diagnostic a two-program directory gives,
# and none of the patterns here used to match it -- not `undefined', not `not
# defined' -- so every one of these failures printed "FAIL -- " with the reason
# blank and the log was the only place the cause existed.
reason() {	# reason <log>
	grep -iE 'error|undefined|not defined|redefined|no match|Internal|^Ld:' "$1" |
		grep -v 'Strict\|Warning' | head -1
}
# Link to a side name and rename into place.  A FAILING link must leave the
# previous build's binary alone: build/bin is what dist staging reads, and
# deleting the product on failure meant one pre-existing failure destroyed the
# last binary that worked -- telnet and clear both had to be recovered out of an
# old disk image.  What stops a stale binary shipping is this script's non-zero
# exit status, which build-all-userland.sh carries into `make userland'.
link_one() {	# link_one <outname> <log> <src...>
	lname="$1"; llog="$2"; shift 2
	if CCZ_VAR=800000020800 "$CCZ" -s -i -L $INC -o "$BIN/.$lname.new" \
	   "$@" $GLIB >"$llog" 2>&1; then
		mv -f "$BIN/.$lname.new" "$BIN/$lname"
		return 0
	fi
	rm -f "$BIN/.$lname.new"
	return 1
}
# The log lives under build/ so concurrent lanes and reruns do not share one
# /tmp name; the FAIL line quotes it because a compile can fail with a
# diagnostic none of reason()'s patterns match.
LOG="$HERE/build/$OUT.log"
if ! link_one "$OUT" "$LOG" $SRCS; then
	echo "$name: FAIL -- $(reason "$LOG")"
	echo "$name: see $LOG"
	exit 1
fi
size="$(wc -c < "$BIN/$OUT") B"
[ "$OUT" = "$name" ] || size="$OUT $size"
# The other programs in the directory.  A failure here is the command's failure
# and must not be reported after an OK line: the caller classifies this script's
# whole output by looking for "OK", so an OK printed before the aux links would
# count a half-built command as built.
#
# Records are separated by a bare `;' and the last one has no terminator, so a
# token is accumulated and the record is linked either at a `;' or when the
# arguments run out.
if [ -n "$AUX" ]; then
	set -- $AUX
	aname=""; asrc=""
	while [ $# -gt 0 ]; do
		tok="$1"; shift
		if [ "$tok" != ";" ]; then
			if [ -z "$aname" ]; then aname="$tok"; else asrc="$asrc $tok"; fi
			[ $# -gt 0 ] && continue
		fi
		[ -n "$aname" ] || continue
		ALOG="$HERE/build/$aname.log"
		if link_one "$aname" "$ALOG" $asrc $EXTRA; then
			size="$size + $aname $(wc -c < "$BIN/$aname") B"
		else
			echo "$name: FAIL -- $aname: $(reason "$ALOG")"
			echo "$name: see $ALOG"
			exit 1
		fi
		aname=""; asrc=""
	done
fi
echo "$name: OK ($size)"
