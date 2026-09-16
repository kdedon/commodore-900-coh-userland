#!/bin/sh
# build.sh - host-build sweep for the C900 Coherent forward-port tree.
#
# Drives the Linux-hosted MWC Z8001 cross toolchain (the commodore-900-toolchain
# checkout; see hostbuild/toolchain.sh) over this tree's kernel and userland
# source and classifies every
# file: which stage passes, which fails, and why.  This is the iteration
# baseline for the forward-port -- not yet a system build.
#
# Usage:
#   build.sh cmd    sweep userland base/cmd (0.7.3 base): compile each .c; fully
#                   link single-file commands against crt0+libc
#   build.sh sys    sweep the kernel: sys/z8001 + sys/drv/ker (0.7.3 MD, flat
#                   headers) and sys/coh (3.2 MI, sys/-prefixed headers)
#   build.sh all    both
#
# Results: hostbuild/logs/<sweep>.tsv (file, stage-reached, first error line)
# and a summary on stdout.
#
# Prereq: the toolchain is built ($C900_TOOLCHAIN/host: build-cc.sh, build-as.sh,
# build-ld.sh, build-libc-z8001.sh -- or `make toolchain' here).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CC0="$TCB/z8001/cc0-z8001"; CC1="$TCB/z8001/cc1-z8001"
CC2="$TCB/z8001/cc2-z8001"; LD="$TCB/ld-z8001"
LIBC="$TCB/libc-z8001"
VAR="${CCZ_VAR:-800000020800}"		# VLARGE + VREADONLY (ctype.h `readonly')
LOGS="$HERE/logs"; mkdir -p "$LOGS"
[ -x "$CC0" ] || { echo "toolchain not built: no $CC0 (run \`make toolchain')" >&2; exit 1; }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# compile one .c: try_c <src> <incflags> ; echoes "<stage>\t<first error>"
# stage: OK | cc0 | cc1 | cc2
try_c() {
	src=$1; incs=$2; stem=$tmp/$(basename "$src" .c)
	err=$("$CC0" $VAR "$src" "$stem.z0" $incs 2>&1)
	[ $? -eq 0 ] || { echo "cc0	$(echo "$err" | head -1)"; return 1; }
	err=$("$CC1" $VAR "$stem.z0" "$stem.z1" 2>&1)
	[ $? -eq 0 ] || { echo "cc1	$(echo "$err" | head -1)"; return 1; }
	err=$("$CC2" 0010 "$stem.z1" "$stem.o" "$stem.scr" 0 2>&1)
	[ $? -eq 0 ] || { echo "cc2	$(echo "$err" | head -1)"; return 1; }
	echo "OK	"
}

# sweep <name> <incflags> <dir> [maxdepth]
sweep() {
	name=$1; incs=$2; dir=$3; depth=${4:-1}
	log="$LOGS/$name.tsv"; : > "$log"
	ok=0; fail=0
	for f in $(find "$dir" -maxdepth "$depth" -name '*.c' | sort); do
		res=$(try_c "$f" "$incs")
		stage=${res%%	*}
		echo "$f	$res" >> "$log"
		if [ "$stage" = OK ]; then ok=$((ok+1)); else fail=$((fail+1)); fi
	done
	echo "== $name: $ok ok, $fail fail  ($log)"
	[ "$fail" -gt 0 ] && awk -F'	' '$2!="OK"{print "   " $2 "  " $1 "  " $3}' "$log" | head -20
	return 0
}

do_cmd() {
	# userland base: 0.7.3 headers
	sweep cmd-flat "" "$OS/base/cmd" 1
	# subdirectory tools (multi-file programs), object mode only.
	# as/ld are the commodore-900-toolchain repository's; base/cmd has no
	# as/ or ld/ directory for this loop to match, so there is no special
	# case for them here.
	for d in "$OS"/base/cmd/*/; do
		b=$(basename "$d")
		I2="-I$d"
		sweep "cmd-$b" "$I2" "$d" 2
	done
	# full-link smoke: every flat command that compiled OK
	log="$LOGS/cmd-link.tsv"; : > "$log"; ok=0; fail=0
	while IFS='	' read -r f stage rest; do
		[ "$stage" = OK ] || continue
		stem=$tmp/$(basename "$f" .c)
		"$CC0" $VAR "$f" "$stem.z0" >/dev/null 2>&1 &&
		"$CC1" $VAR "$stem.z0" "$stem.z1" >/dev/null 2>&1 &&
		"$CC2" 0010 "$stem.z1" "$stem.o" "$stem.scr" 0 >/dev/null 2>&1
		err=$("$LD" -o "$stem.out" "$LIBC/crt0.o" "$stem.o" "$LIBC/libc-z8001.a" 2>&1)
		if [ $? -eq 0 ]; then ok=$((ok+1)); echo "$f	OK	" >> "$log"
		else fail=$((fail+1)); echo "$f	ld	$(echo "$err" | head -1)" >> "$log"; fi
	done < "$LOGS/cmd-flat.tsv"
	echo "== cmd-link: $ok ok, $fail fail  ($log)"
	awk -F'	' '$2!="OK"{print "   ld  " $1 "  " $3}' "$log" | head -20
}

# sys/z8001, sys/drv, sys/ker, sys/coh and sys/z8001/h (the 3.2 contract
# headers + Z8001 machine tail that md.s would assemble against) are
# commodore-900-coh-kernel3's; this repository has no sys tree to sweep.
# Left as a silent sweep, `find' on each missing directory prints nothing,
# the loop body never runs, and the summary line reads "0 ok, 0 fail" four
# times over: that is indistinguishable from a clean pass and is exactly
# the defect class this script exists to catch, so do_sys refuses by name
# instead (name exactly what cannot build and why, at the point the caller
# would expect it to run) rather than being removed outright: `build.sh sys`
# and `build.sh all sys` remain valid things to type, and should say why
# they do nothing rather than fail to parse.
do_sys() {
	echo "== sys: SKIPPED -- sys/z8001, sys/drv, sys/ker, sys/coh and sys/z8001/h" >&2
	echo "==      are commodore-900-coh-kernel3's." >&2
	echo "==      This repository has no kernel sources to sweep; build the kernel" >&2
	echo "==      from that repository instead." >&2
	return 1
}

case "${1:-all}" in
cmd) do_cmd;;
sys) do_sys;;
all) do_sys; do_cmd;;
*) echo "usage: build.sh [cmd|sys|all]" >&2; exit 2;;
esac
