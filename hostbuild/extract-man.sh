#!/bin/sh
# extract-man.sh -- assemble the on-line manual at hostbuild/build/man, which
# build-image.sh populates onto the /usr/man partition (hd2).  The tree is
# `COHERENT.1/<entry>', `COHERENT.2/<entry>' and `man.index' -- exactly the
# layout /bin/man reads.
#
# The tree is build output and untracked; re-run this after a clean checkout.
#
# A MANUAL PAGE BELONGS WITH WHAT IT DOCUMENTS.  Every article is tracked in the
# component that builds its subject -- base/man, net/man, archive/man and the rest
# here; the C library, C language and compiler-pass articles in the toolchain's
# man; the device-driver, kernel-module and bare system-call articles in the
# kernel's os/man.  Each such tree is the same shape: COHERENT.1/, COHERENT.2/,
# its own man.index naming exactly its own pages, and its own COPYING stating
# the terms the pages travel under.  What an owner owns is read from its own
# index -- that repository's statement about its own pages -- and nothing here
# re-derives it.
#
# On top of those go the locally-written pages (man), and the renderings of the
# upstream troff pages that MGR, pico and screen carry beside their own source
# (man/VENDOR-DOCS).
#
# A PARTIAL MANUAL IS REFUSED.  A short extract looks exactly like a whole one
# from outside: the tree is there, the index parses, and the count is simply
# smaller.  So every source below says how many pages it delivered, the totals
# are printed together at the end, and anything that declared work it did not do
# fails the run -- with every such source named at once, rather than on the
# first one.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS="$HERE/.."
DST="$HERE/build/man"

mkdir -p "$HERE/build"
rm -rf "$DST"
mkdir -p "$DST/COHERENT.1" "$DST/COHERENT.2"
: > "$DST/man.index"
FAILED=

# The components of this repository, then the other repositories, resolved
# through mk/deps.sh.  An owner tree that is not there is a FAILURE and not a
# warning: the manual would be short by that owner's pages and nothing outside
# would say so.
LOCALOWNERS="base net editors mail archive comms games hr mgr test"

owner_man() {	# $1 = the owner's name; prints its manual tree, or nothing
	case $1 in
	toolchain)
		d=$(sh "$OS/mk/deps.sh" toolchain 2>/dev/null) || d=
		if [ -n "$d" ] && [ -f "$d/man/man.index" ]; then
			echo "$d/man"
		fi
		;;
	kernel)
		d=$(sh "$OS/mk/deps.sh" kernel 2>/dev/null) || d=
		# A checkout keeps the tree under os/; an unpacked release
		# archive has no os/ level.
		if [ -n "$d" ] && [ -f "$d/os/man/man.index" ]; then
			echo "$d/os/man"
		elif [ -n "$d" ] && [ -f "$d/man/man.index" ]; then
			echo "$d/man"
		fi
		;;
	*)
		[ -f "$OS/$1/man/man.index" ] && echo "$OS/$1/man"
		;;
	esac
	return 0
}

stage_owned() {	# $1 = an owner's manual tree, $2 = its name
	ns=0
	while IFS='	' read -r rel _rest; do
		case "$rel" in ''|'#'*) continue;; esac
		# An index row with no page of its own is the owner's statement
		# about its own manual and is carried as it stands; the
		# accounting at the foot counts it.
		if [ -f "$1/$rel" ]; then
			mkdir -p "$DST/$(dirname "$rel")"
			cp "$1/$rel" "$DST/$rel"
			ns=$((ns + 1))
		fi
	done < "$1/man.index"
	# The pages the index does not name are still the owner's pages, and
	# still go on the image: /bin/man finds an article by path as well as
	# through the index.
	for sect in COHERENT.1 COHERENT.2; do
		[ -d "$1/$sect" ] || continue
		for page in "$1/$sect"/*; do
			[ -f "$page" ] || continue
			rel="$sect/$(basename "$page")"
			[ -f "$DST/$rel" ] && continue
			mkdir -p "$DST/$sect"
			cp "$page" "$DST/$rel"
			ns=$((ns + 1))
		done
	done
	cat "$1/man.index" >> "$DST/man.index"
	# The terms travel with the pages.  The owner states them beside its own
	# tree and the manual carries that statement onto the image the pages
	# land on; a tree that does not state them is refused rather than
	# redistributed under nothing.
	[ -f "$1/COPYING" ] || {
		echo "extract-man.sh: $1 states no terms beside its pages" >&2
		echo "  (no COPYING), and they are not staged without them" >&2
		exit 1
	}
	cp "$1/COPYING" "$DST/COPYING.$2"
	echo "$2 manual: $ns page(s) from $1"
}

for own in $LOCALOWNERS toolchain kernel; do
	tree=$(owner_man "$own")
	if [ -n "$tree" ]; then
		stage_owned "$tree" "$own"
	else
		case $own in
		toolchain|kernel)
			FAILED="$FAILED
no $own manual tree: check out the repository, or set the
  variable \`sh mk/deps.sh -n $own' names"
			;;
		esac
	fi
done

# The locally-written pages, from man.  This tree ships commands the Lexicon
# never described -- /usr/bin/rsh, the restricted shell, had no page at all,
# which for a security feature means its limits were undocumented on the machine
# that offers it -- and it ships commands whose behaviour here is not what the
# article describes (reboot's warm restart, shutdown's boot and powerfail
# levels, nologin's lockout), for which the page written here stands in the
# article's place.
#
# Pages here are PLAIN TEXT, because /bin/man only cats an article to $PAGER and
# this distribution ships no nroff macro packages, so nothing could format one
# on the box.  Line 3 of the page is its one-line description: the index row
# takes it, whether the page fills a gap (the row is new) or stands in for an
# article (the row's own description would otherwise describe a page that is not
# there, and `man -k' reads that line).
#
# The index is scanned a line at a time by cmd/man.c, so appending is enough --
# it has no sort order to keep.
if [ -d "$OS/man" ]; then
	nloc=0
	for sect in "$OS"/man/*/; do
		[ -d "$sect" ] || continue
		s=$(basename "$sect")
		mkdir -p "$DST/$s"
		for page in "$sect"*; do
			[ -f "$page" ] || continue
			p=$(basename "$page")
			desc=$(sed -n 3p "$page")
			# The name is compared as a FIELD, not as a pattern: a
			# page called `[' is a command this system ships and is
			# also an unterminated bracket expression, so a grep for
			# it is an invalid regular expression whose failure would
			# read as `the manual does not carry this name'.
			if awk -F'\t' -v r="$s/$p" '$1 == r { found = 1 }
				END { exit !found }' "$DST/man.index"; then
				awk -F'\t' -v r="$s/$p" -v d="$desc" \
					'BEGIN{OFS="\t"} $1==r { $3=d } { print }' \
					"$DST/man.index" > "$DST/.idx" &&
					mv -f "$DST/.idx" "$DST/man.index"
			else
				printf '%s/%s\t%s\t%s\n' "$s" "$p" "$p" \
					"$desc" >> "$DST/man.index"
			fi
			cp "$page" "$DST/$s/$p"
			nloc=$((nloc+1))
		done
	done
	echo "local pages: $nloc from $OS/man"
fi

# VENDOR-DOCS: programs shipped with their OWN upstream troff page (MGR and
# its clients, pico, screen).  Rendered rather than typed, because nothing else
# in this tree can turn troff into what /bin/man cats.  A name the manual
# already carries is disambiguated by suffix below, not dropped.
VENDORLIST="$OS/man/VENDOR-DOCS"
if [ -f "$VENDORLIST" ]; then
	# man/vendor.tmac goes in front of every page: it defines the
	# COHERENT macros groff -man has not got.  A page is rendered from two
	# input files, which *roff concatenates, so nothing is written into the
	# document itself -- these pages are their authors' text and stay it.
	VTMAC="$OS/man/vendor.tmac"
	[ -f "$VTMAC" ] || {
		echo "extract-man.sh: no $VTMAC" >&2
		exit 1
	}
	if command -v groff >/dev/null 2>&1; then
		NROFF="groff -Tascii -P -c -man"
	elif command -v nroff >/dev/null 2>&1; then
		NROFF="nroff -man"
	else
		NROFF=
	fi
	nv=0
	want=$(grep -c '^[^#[:space:]]' "$VENDORLIST" || true)
	if [ -z "$NROFF" ]; then
		FAILED="$FAILED
VENDOR-DOCS: no groff or nroff on this machine, so $want page(s) (MGR
  clients, pico, screen) cannot be rendered.  Install groff"
	fi
	while IFS='	' read -r name src; do
		case "$name" in ''|'#'*) continue;; esac
		[ -n "$NROFF" ] || continue
		p="$OS/$src"
		# A row that names nothing is carried to the accounting at the
		# end rather than ending the run here.  Stopping mid-list leaves
		# every later row unstaged and the counts unprinted, which is a
		# partial manual that reports itself as a whole one.
		[ -f "$p" ] || {
			FAILED="$FAILED
VENDOR-DOCS: $name names $src, which does not exist"
			continue
		}
		case "$name" in
		[a-lA-L]*) sect=COHERENT.1 ;;
		*)         sect=COHERENT.2 ;;
		esac
		# A name the manual already files (close(1) the mgr client against
		# close() the system call) is staged at `NAME.c', the Lexicon's own
		# suffix for the command when an article of the same name documents
		# the function: kill.c/kill.s, write.c/write.s, mkdir.c, sync.c.
		# The index row keeps the bare title, so `man NAME' finds the command
		# and `man NAME()' the article -- /bin/man matches the title field
		# exactly and prints every hit.
		file=$name
		if grep -q "^$sect/$name	" "$DST/man.index"; then
			file=$name.c
		fi
		if grep -q "^$sect/$file	" "$DST/man.index"; then
			FAILED="$FAILED
VENDOR-DOCS: $src collides with $sect/$name and with $sect/$file"
			continue
		fi
		mkdir -p "$DST/$sect"
		# col -bx drops the bold/underline overstrike a *roff terminal driver
		# emits; without it every heading is doubled letters (`NNAAMMEE'),
		# which the local COHERENT.1/nawk-style pages never carry.
		if $NROFF "$VTMAC" "$p" 2>/dev/null | col -bx > "$DST/$sect/$file.tmp"; then
			mv -f "$DST/$sect/$file.tmp" "$DST/$sect/$file"
			# `.SH NAME' is the convention; pico.1 spells it `.SH Name'
			# and groff -man renders the heading exactly as given, so the
			# match is case-insensitive.
			desc=$(awk 'tolower($0) == "name" { getline;
				sub(/^[ \t]+/, ""); print; exit }' "$DST/$sect/$file")
			printf '%s/%s\t%s\t%s\n' "$sect" "$file" "$name" "$desc" \
				>> "$DST/man.index"
			nv=$((nv + 1))
		else
			rm -f "$DST/$sect/$file.tmp"
			FAILED="$FAILED
VENDOR-DOCS: $name did not render from $src"
		fi
	done < "$VENDORLIST"
	echo "vendor docs: $nv of $want page(s) staged${NROFF:+ ($NROFF)}"
	[ "$nv" -eq "$want" ] || [ -z "$NROFF" ] || FAILED="$FAILED
VENDOR-DOCS: $((want - nv)) of $want page(s) not staged"
fi

# THE ACCOUNTING.
np=$(find "$DST/COHERENT.1" "$DST/COHERENT.2" -type f | wc -l)
ni=$(cut -f1 "$DST/man.index" | sort -u | wc -l)
norphan=$(cut -f1 "$DST/man.index" | sort -u | while read -r rel; do
	[ -f "$DST/$rel" ] || echo "$rel"; done | wc -l)
nunindexed=$(cd "$DST" && find COHERENT.1 COHERENT.2 -type f | sort > .pages &&
	cut -f1 man.index | sort -u > .rows && comm -23 .pages .rows | wc -l)
rm -f "$DST/.pages" "$DST/.rows"
echo "manual: $np page(s), $ni indexed name(s), $norphan index row(s) with no" \
	"page, $nunindexed page(s) with no index row"
if [ -n "$FAILED" ]; then
	echo "extract-man.sh: the manual is INCOMPLETE:$FAILED" >&2
	exit 1
fi
