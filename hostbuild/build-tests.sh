#!/bin/sh
# build-tests.sh -- cross-build the standalone guest test programs in test/.
#
# Each is one directory holding one self-contained program, test/<name>/<name>.c,
# that a dist list can stage with src=test/<name>/<name>.  They are guest-side
# halves of host-driven checks (test/serialbytes pairs with
# hostbuild/serial-bytes-test.py; test/sbrkzero proves sbrk memory is zero-filled
# and that the break is refused rather than wrapped past 64K), so they have to be
# built by the Z8001 toolchain like any other command.
#
# Discovery is by convention rather than a list: a directory whose name matches
# its single .c file is a program.  Directories with their own Makefile
# (test/multiseg) build themselves and are skipped; directories of shell input
# (test/ichan) have no .c at all.
#
# Without a real rule, make's builtin `%: %.c' would compile a Z8001 guest
# program with the HOST gcc -- an x86 binary staged into a Z8001 image, which
# fails only at run time on the target.  The Makefile cancels the builtin
# rules; this supplies the real ones.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
TESTS="$OS/test"
INC="-I $OS/include -I $OS/include/sys"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"

ok=0; fail=0; fl=""
for d in "$TESTS"/*/; do
	name=$(basename "$d")
	# A directory with its own Makefile builds itself, and `all' is named
	# rather than left to the default goal: test/bigtext includes
	# hostbuild/gotools.mk, whose $(LOUTDIS) rule is the first one read and
	# would otherwise be all a bare `make' there builds.
	# test/hostcheck is the HOST-side mutation gate: it compiles these same
	# sources with the host cc against a stand-in kernel, to prove each probe
	# can fail.  Nothing in it is a guest program, so it is not built here.
	# `make -C test/hostcheck check' is its entry point.
	if [ "$name" = hostcheck ]; then
		echo "  $name: skipped (host-only mutation gate)"
		continue
	fi
	if [ -f "${d}Makefile" ]; then
		if make -C "$d" all > "$LOGD/test-$name.log" 2>&1; then
			ok=$((ok+1)); echo "  $name: OK (own Makefile)"
		else
			fail=$((fail+1)); fl="$fl $name"
			echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD/test-$name.log" | grep -v 'Strict\|Warning' | head -1)"
		fi
		continue
	fi
	src="$d$name.c"
	[ -f "$src" ] || continue		# not a single-program directory
	# The probes that reach the KERNEL's own headers.  mouse wants
	# <sys/mouse.h>; stackhw wants <sys/proc.h> and <sys/uproc.h>, and
	# <sys/proc.h> in turn wants <sys/timeout.h>.  None of those exists in
	# this tree or the toolchain: $KINC is the kernel repository's include
	# directory (toolchain.sh), and empty means no kernel resolved, in which
	# case the probe refuses by name rather than compiling against
	# something else.
	XINC=""
	case "$name" in
	mouse|stackhw)
		if [ -z "$KINC" ]; then
			fail=$((fail+1)); fl="$fl $name"
			echo "  $name: FAIL -- needs the kernel's headers; sh mk/deps.sh -n kernel"
			continue
		fi
		XINC="-I $KINC -I $KINC/sys";;
	esac
	if "$CCZ" -s -i $INC $XINC -o "$d.$name.new" "$src" > "$LOGD/test-$name.log" 2>&1; then
		mv -f "$d.$name.new" "$d$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$d$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$d.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD/test-$name.log" | grep -v 'Strict\|Warning' | head -1)"
	fi
done
echo "tests: $ok ok, $fail failed${fl:+ --$fl}"
[ "$fail" -eq 0 ]
