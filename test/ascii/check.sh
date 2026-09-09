#!/bin/sh
# test/ascii/check.sh -- fail if any non-ASCII byte reaches a file the target
# toolchain reads or that ships onto the machine.
#
# These sources must eventually compile under the native K&R toolchain, which
# reads bytes; every byte in the checked set must be 0x00-0x7F.
#
# Checked: *.c *.h *.s *.t *.y *.l, the dist description files (*.list *.dist
# *.devices *.media), Makefiles, *.sh, *.py, *.yml.  Enumeration is `git
# ls-files -co --exclude-standard' (find without git), so untracked work is
# checked and ignored build output is not.  dist/files -- the packaging
# overlay this used to also check -- moved to a separate repository and is
# checked there now.  Not checked: the archival trees in ARCHIVAL, the
# non-ASCII DATA files in EXEMPT, and *.go (UTF-8 by language spec, never fed
# to the native toolchain; -s adds it).
#
# The detector is one perl pass, not `grep -P': where /usr/bin/grep is ugrep,
# `grep -cP' reports zero for a file holding a raw high byte that `grep -nP'
# finds.  The perl exit status says whether the scan RAN, the report says what
# it found, and the file count must reach FLOOR: an empty report from a scan
# that did not run, or an empty file list, is a harness failure, not a pass.
#
# Usage: sh check.sh [-s|--strict] [-q|--quiet]
# Exit 0 = clean, 1 = offending bytes found, 2 = harness problem.

# Floor on the file count: the tree holds thousands, so a list under this
# means the enumeration collapsed, not that the tree shrank.
FLOOR=200

STRICT=0
QUIET=0
for a in "$@"; do
	case "$a" in
	-s|--strict)	STRICT=1 ;;
	-q|--quiet)	QUIET=1 ;;
	-h|--help)	sed -n '2,24p' "$0"; exit 0 ;;
	*)		echo "check.sh: unknown option $a" >&2; exit 2 ;;
	esac
done

HERE=$(cd "$(dirname "$0")" && pwd)
# The root must be THIS repository and nothing above it.  A relative walk is
# not an acceptable fallback: this script lives three levels down, so `../../..'
# names whatever directory happens to contain the checkout -- on a machine that
# keeps its repositories side by side that is the parent of all of them, and the
# gate then scans and fails on other people's trees.  Refuse instead.
ROOT=$(cd "$HERE" && git rev-parse --show-toplevel 2>/dev/null)
[ -n "$ROOT" ] && [ -d "$ROOT/base" ] && [ -d "$ROOT/dist" ] || {
	echo "check.sh: cannot locate the repository root." >&2
	echo "  git rev-parse --show-toplevel found: ${ROOT:-<nothing>}" >&2
	echo "  This gate scans the whole tree, so it will not guess one." >&2
	exit 2
}
cd "$ROOT" || exit 2

# Deliberate non-ASCII DATA (not comments): games/ttycity's curses front end
# renders the city with Unicode box-drawing, braille, block and emoji glyphs,
# and those string literals ARE the tile vocabulary.  Comments in these files
# are still held to ASCII by review.
#
# Every path below is relative to this repository's root; check any addition
# against `git ls-files', since a pattern that matches nothing silently
# returns its tree to the checked set (or drops an exemption).
EXEMPT='
games/ttycity/nc_gfx.c
games/ttycity/nc_input.c
games/ttycity/nc_minimap.c
games/ttycity/nc_render.c
'

# Known source corruption inherited from the C900 disk: reported as a WARNING
# on every run, not a failure, because repairing it is a code change and not
# this check's business.  src/kernel was removed and is in a separate
# repository, so the corrupt copy is not in this tree to warn about any more.
# Empty until this tree's own source turns up a corruption of its kind.
KNOWN_CORRUPT='
'

# Vendored / archival trees: never rewritten, so never checked.  tools/ is
# host-side Python 3 image tooling -- nothing in it is read by the target
# toolchain or ships onto the machine.  src/ is NOT excluded: the 1985
# compiler does read it.  sys/ref (the vendored holdings kept for diffing
# against the kernel) is in a separate repository and is not here anymore.
ARCHIVAL='^man-4\.2/
^mgr/
^base/cmd/badscan\.c$
^base/cmd/enable\.c$
^base/cmd/unmkfs\.c$
^base/cmd/more/
^base/lib/libcurses/
^base/lib/libterm/
^base/lib/misc/
^base/lib/ndir/
^base/lib/regexp/
^tools/
^scratch/
^\.claude/'

if git rev-parse --git-dir >/dev/null 2>&1; then
	list_all() { git ls-files -co --exclude-standard; }
else
	list_all() { find . -type f | sed 's|^\./||'; }
fi

# The checked set: by extension, by basename, or by shipping location.
select_paths() {
	if [ "$STRICT" = 1 ]; then
		ext='c|h|s|t|y|l|list|dist|devices|media|sh|py|yml|go'
	else
		ext='c|h|s|t|y|l|list|dist|devices|media|sh|py|yml'
	fi
	list_all \
	| grep -Ev "$(echo "$ARCHIVAL" | grep . | paste -sd'|')" \
	| grep -E "\.($ext)\$|(^|/)([Mm]akefile)[^/]*\$|\.mk\$" \
	| while read -r f; do
		case "$EXEMPT" in
		*"$f"*)	;;
		*)	[ -f "$f" ] && echo "$f" ;;
		esac
	done
}

TMP=${TMPDIR:-/tmp}/ascii-check.$$
trap 'rm -f "$TMP" "$TMP.paths"' 0 1 2 15
select_paths > "$TMP.paths"
TOTAL=$(wc -l < "$TMP.paths" | tr -d ' ')

if [ "$TOTAL" -lt "$FLOOR" ]; then
	echo "ascii: HARNESS -- the file list holds $TOTAL entries, fewer than the" >&2
	echo "ascii: floor of $FLOOR.  Nothing was scanned; this is not a clean run." >&2
	exit 2
fi

# One perl pass: classify every byte, print file:line: with the offenders
# marked.  The exit status says whether the scan RAN, not what it found; a
# file that cannot be opened is a harness failure, not a skip.
perl -e '
	my $seen = 0;
	open(my $L, "<", $ARGV[0]) or exit 3;
	while (my $p = <$L>) {
		chomp $p;
		next if $p eq "";
		open(my $h, "<", $p) or exit 3;
		binmode $h;
		my $ln = 0;
		while (my $line = <$h>) {
			$ln++;
			next unless $line =~ /[^\x00-\x7f]/;
			chomp $line;
			$line =~ s/([^\x09\x20-\x7e])/sprintf("<%02X>", ord($1))/ge;
			print "$p:$ln: $line\n";
		}
		close $h;
		$seen++;
	}
	close $L;
	exit($seen > 0 ? 0 : 3);
' "$TMP.paths" > "$TMP"
BAD=$?

if [ "$BAD" != 0 ]; then
	echo "ascii: HARNESS -- the scan did not complete (perl exit $BAD)." >&2
	echo "ascii: An empty report from a scan that did not run is not a pass." >&2
	exit 2
fi

# Split the report into known-corruption warnings and real failures.
: > "$TMP.warn"
: > "$TMP.fail"
while IFS= read -r line; do
	f=${line%%:*}
	case "$KNOWN_CORRUPT" in
	*"$f"*)	echo "$line" >> "$TMP.warn" ;;
	*)	echo "$line" >> "$TMP.fail" ;;
	esac
done < "$TMP"
NWARN=$(wc -l < "$TMP.warn" | tr -d ' ')
NFAIL=$(wc -l < "$TMP.fail" | tr -d ' ')
rm -f "$TMP.warn.x"

if [ "$NWARN" -gt 0 ]; then
	echo "ascii: WARNING -- $NWARN line(s) of known C900-disk source corruption:"
	[ "$QUIET" = 1 ] || cat "$TMP.warn"
	echo "ascii: (inherited from the C900 disk; repairing these is a code change)"
fi
if [ "$NFAIL" = 0 ]; then
	echo "ascii: OK -- $TOTAL files checked, 0 non-ASCII lines"
	rm -f "$TMP.warn" "$TMP.fail"
	exit 0
fi
[ "$QUIET" = 1 ] || cat "$TMP.fail"
echo "ascii: FAIL -- $NFAIL non-ASCII line(s) of $TOTAL files checked."
echo "ascii: replace the bytes and keep the meaning: em dash -> --, section sign"
echo "ascii: -> sec., arrow -> ->, curly quotes -> plain, multiply -> x."
rm -f "$TMP.warn" "$TMP.fail"
exit 1
