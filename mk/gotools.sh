# mk/gotools.sh -- resolve the c900oses/gotools tree.  SOURCE this file
# (`. "$C900_ROOT/mk/gotools.sh"'); do not exec it.
#
# The shell twin of hostbuild/gotools.mk, for tests under test that are
# shell rather than make.  Same reason that file gives: hostfsd, kdbg and
# loutdis import the private c900/z8000 simulator checkouts, which
# commodore-900-toolchain (public) must not need, so they live in
# c900oses/gotools instead.
#
# $C900_ROOT must already name this repository's root.
#
# They are Go, so the Go compiler is a dependency of this repository's HOST
# instruments and of nothing it ships; gotools_need says so by name rather
# than letting `go: not found' come back out of a recipe two makefiles down.
#
#   $C900_GOTOOLS   the gotools tree, or empty when none resolves.  Unset,
#                   the first candidate with a Makefile in it wins: this
#                   repository staged inside c900oses/repos/, then a
#                   c900oses checked out beside it, then one level further
#                   out either way -- the same three paths gotools.mk
#                   searches, restated from $C900_ROOT instead of from
#                   hostbuild.
#
# gotools_need <tool> <why> -- build <tool> through gotools' own Makefile
# (it decides whether anything needs recompiling, not this file) and set
# $GOTOOLS_BIN to the result, or refuse by name and exit 2.

if [ -z "${C900_ROOT:-}" ] || [ ! -d "$C900_ROOT/mk" ]; then
	echo "gotools.sh: \$C900_ROOT must name the repository root before sourcing" >&2
	exit 2
fi

C900_GT_SEARCH="$(cd "$C900_ROOT/../.." 2>/dev/null && pwd)/gotools
$(cd "$C900_ROOT/.." 2>/dev/null && pwd)/c900oses/gotools
$(cd "$C900_ROOT/../.." 2>/dev/null && pwd)/c900oses/gotools"
C900_GOTOOLS=${C900_GOTOOLS:-}
if [ -z "$C900_GOTOOLS" ]; then
	for c in $C900_GT_SEARCH; do
		[ -f "$c/Makefile" ] && { C900_GOTOOLS=$c; break; }
	done
fi

gotools_need() {
	_gn_tool=$1; _gn_why=$2
	if [ -z "$C900_GOTOOLS" ]; then
		echo "*** cannot $_gn_why without the gotools tree." >&2
		echo "*** no C900_GOTOOLS resolved; tried:" >&2
		for c in $C900_GT_SEARCH; do echo "***     $c" >&2; done
		echo "*** Clone c900oses to one of those, or set C900_GOTOOLS." >&2
		exit 2
	fi
	if ! command -v go >/dev/null 2>&1; then
		echo "*** cannot $_gn_why without go on the path." >&2
		echo "*** $_gn_tool is a Go program, built from $C900_GOTOOLS by" >&2
		echo "*** its own Makefile.  Nothing this repository SHIPS needs" >&2
		echo "*** go; this host instrument does." >&2
		exit 2
	fi
	make -s -C "$C900_GOTOOLS" "$_gn_tool" >&2 || exit 2
	GOTOOLS_BIN=$C900_GOTOOLS/build/$_gn_tool
	unset _gn_tool _gn_why
}
