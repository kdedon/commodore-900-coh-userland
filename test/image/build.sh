#!/bin/sh
# build.sh [OUT] -- pack the userland's test image: this repository's own
# component archives on the kernel, loader and filesystem tools their
# repositories publish.  Default OUT: hostbuild/build/test.bin.
#
# WHAT IT IS FOR.  The checks that need a booted system -- the test/cmd
# scripts, net/twohost, the mail and serial harnesses -- run here, before a
# release, against the programs this build made.  A distribution image is
# commodore-900-dist's product and is built FROM this repository's release,
# so booting one here would make this repository test itself through its own
# consumer.  This image is not a distribution and is never shipped.
#
# WHAT GOES IN, and where each comes from:
#
#   the -bin archives in hostbuild/build/packages (`make -C hostbuild
#   packages'), unpacked and given the modes their manifest.tab states --
#   the release artifacts themselves, including testing's own, so the checks
#   test what ships
#   lists/licences.list, resolved by hostbuild/component.py as a -bin package
#   is: /usr/licences is what the components are under, and no package
#   carries it
#   the kernel and the console drivers, from the kernel edge
#   the loader and <bootinfo.h>, from the kboot edge
#   /dev, from test/image/devices, a copy of dist's table (it says why)
#
# THE LAYOUT is the 21 MB drive's geometry and /tmp with its swap area, as
# commodore-900-dist's media/hd21.media has them, with /usr on the root
# filesystem rather than a partition of its own: a test image has no reason
# to be rationed, and one filesystem less is one mount less to go wrong.
#
#	part	slot	start	blocks	isize
#	boot	0	0	136	4	kboot as /coherent, kboot.cfg
#	root	4	136	30872	250	/, /usr and everything
#	tmp	3	31008	6511	127	swap 6512..10608 of the slot
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$ROOT/hostbuild/build/test.bin}
PKGDIR=${PKGDIR:-$ROOT/hostbuild/build/packages}
PYTHON=${PYTHON:-python3}

# Every component a distribution image of the whole system carries, less the
# graphical login and MGR (each a variant of what is here) and CP/M (which
# wants a partition of its own).  dist's coherent3-full-test is the same set.
COMPONENTS=${COMPONENTS:-"base runtime login-text editors archive games net-games net mail-light hr hr-clients testing"}

BS=512
TOTAL=41616			# 612 cylinders x 4 heads x 17 sectors
GEOM="612 4 17 128"
BOOT_AT=0;    BOOT_N=136;   BOOT_I=4
ROOT_AT=136;  ROOT_N=30872; ROOT_I=250
TMP_AT=31008; TMP_N=6511;   TMP_I=127
SWAP_BOT=6512; SWAP_TOP=10608
ROM_SAFE=68			# 4 heads x 17: what the ROM reads under any geometry

need() {	# need <edge> -- the resolved path, or the resolver's refusal
	_p=$(sh "$ROOT/mk/deps.sh" "$1")
	[ -n "$_p" ] || { sh "$ROOT/mk/deps.sh" -n "$1"; exit 2; }
	echo "$_p"
}
COHFS=$(need tools)/bin/cohfs
KB=$(need kboot)
K=$(need kernel)
# A kernel checkout keeps its build under os/, a release at its top.
if [ -d "$K/os/hostbuild" ]; then KOS=$K/os; else KOS=$K; fi
KOUT=$KOS/hostbuild/kobj/kernel.out
KDRV=$KOS/hostbuild/build/drv
for f in "$KB/kboot" "$KB/include/bootinfo.h" "$KOUT" "$KDRV/notty"; do
	[ -f "$f" ] || { echo "build.sh: $f is missing" >&2; exit 2; }
done

V=$(sh "$ROOT/hostbuild/version.sh")
W=$(mktemp -d)
trap 'rm -rf "$W"' 0 1 2 15
STAGE=$W/root
mkdir -p "$STAGE" "$W/boot" "$W/tmp" "$W/x"
: > "$W/rows"

# ---- the component archives ----
for c in $COMPONENTS; do
	a=$PKGDIR/c900-$c-bin-v$V.tar.gz
	if [ ! -f "$a" ]; then
		echo "build.sh: no $a" >&2
		echo "  The test image is packed from this build's component archives:" >&2
		echo "      make -C $ROOT/hostbuild all packages" >&2
		exit 2
	fi
	tar xzf "$a" -C "$W/x"
	cp -a "$W/x/c900-$c-bin-v$V/files/." "$STAGE/"
	# manifest.tab: <type> <path> <mode> <uid> <gid> [<link target>]
	cat "$W/x/c900-$c-bin-v$V/manifest.tab" >> "$W/rows"
	rm -rf "$W/x/c900-$c-bin-v$V"
done

# ---- the lists no package carries, resolved as a package would be ----
for l in licences; do
	"$PYTHON" "$ROOT/hostbuild/component.py" resolve lists/$l.list
done > "$W/testing"
while IFS='	' read -r t p m u g s; do
	case $t in
	f)	mkdir -p "$STAGE$(dirname "$p")"; cp "$s" "$STAGE$p"
		echo "f $p $m $u $g" >> "$W/rows" ;;
	d|e)	echo "$t $p $m $u $g" >> "$W/rows" ;;
	l)	echo "l $p 0 $u $g $s" >> "$W/rows" ;;
	esac
done < "$W/testing"

# ---- the kernel and the console drivers ----
cp "$KOUT" "$STAGE/coherent"
echo "f /coherent 755 0 1" >> "$W/rows"
mkdir -p "$STAGE/drv"
for d in notty lrtty hrtty; do
	cp "$KDRV/$d" "$STAGE/drv/$d"
	echo "f /drv/$d 755 0 1" >> "$W/rows"
done

# ---- the rows: directories, empty files, links, and every mode ----
# A hard link takes its target's mode, uid and gid: cohfs gives an inode the
# MANIFEST row of whichever of its names it walks first.
awk '
	$1 != "l" { mode[$2] = $3 " " $4 " " $5 }
	{ row[NR] = $0 }
	END {
		for (i = 1; i <= NR; i++) {
			split(row[i], f, " ")
			if (f[1] == "l") {
				if (!(f[6] in mode)) {
					print "build.sh: " f[2] " links to " f[6] \
					      ", which nothing installs" > "/dev/stderr"
					exit 1
				}
				print "l", f[2], mode[f[6]], f[6]
			} else
				print f[1], f[2], f[3], f[4], f[5]
		}
	}' "$W/rows" > "$W/rows2"
while read -r t p m u g to; do
	case $t in
	d)	mkdir -p "$STAGE$p" ;;
	e)	mkdir -p "$STAGE$(dirname "$p")"; : > "$STAGE$p" ;;
	l)	mkdir -p "$STAGE$(dirname "$p")"; ln -f "$STAGE$to" "$STAGE$p" ;;
	f)	[ -f "$STAGE$p" ] || { echo "build.sh: $p was not staged" >&2; exit 1; } ;;
	esac
	[ "$p" = / ] || echo "${p#/} $m $u $g"
done < "$W/rows2" > "$STAGE/MANIFEST"
cp "$HERE/devices" "$STAGE/DEVICES"
# What a list places under /tmp is on the /tmp filesystem, which rc mounts over
# the root's /tmp: /tmp/screens has to be where screen(1) will look for it.
if [ -n "$(ls -A "$STAGE/tmp" 2>/dev/null)" ]; then
	mv "$STAGE/tmp/"* "$W/tmp/"
	sed -n 's|^tmp/||p' "$STAGE/MANIFEST" > "$W/tmp/MANIFEST"
fi
sed -i '/^tmp\//d' "$STAGE/MANIFEST"

# ---- the templates: what the mounts are, and which release this is ----
# Text files only: a program may hold any bytes, @..@ among them.  base-bin
# carries the two there are, /etc/rc (@MOUNTS@) and /etc/motd (@VERSION@).
printf '/etc/mount /dev/hd4 / -u\n/etc/mount /dev/hd3 /tmp\n' > "$W/mounts"
for f in $(grep -rlI '@[A-Z][A-Z]*@' "$STAGE" 2>/dev/null || true); do
	awk -v v="$V" -v mf="$W/mounts" '
		/^@MOUNTS@$/ { while ((getline l < mf) > 0) print l; next }
		{ gsub(/@VERSION@/, v); print }' "$f" > "$f.new"
	cat "$f.new" > "$f"
	rm "$f.new"
	if grep -n '@[A-Z][A-Z]*@' "$f" >&2; then
		echo "build.sh: ${f#$STAGE} names a placeholder this image does not fill" >&2
		exit 1
	fi
done

# ---- the boot partition ----
cp "$KB/kboot" "$W/boot/coherent"
{
	echo "# kboot.cfg -- the test image's; written by test/image/build.sh."
	echo "geom $GEOM"
	echo "part 0 $BOOT_AT $BOOT_N"
	echo "part 3 $TMP_AT $SWAP_TOP"
	echo "part 4 $ROOT_AT $ROOT_N"
	echo "part 15 0 $TOTAL"
	echo "swap 3 $SWAP_BOT $SWAP_TOP"
	echo "os OpenCoherent-test $ROOT_AT coherent part"
	sed -n 's/^#[ 	]*define[ 	][ 	]*BF_\([A-Z0-9_]*\)[ 	][ 	]*\(0[xX][0-9a-fA-F]*\|[0-9][0-9]*\).*/\1 \2/p' \
		"$KB/include/bootinfo.h" |
	while read -r n b; do echo "bflag $n $((b))"; done
} > "$W/boot/kboot.cfg"

# ---- the image ----
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT.new"
dd if=/dev/zero of="$OUT.new" bs=$BS count=0 seek=$TOTAL 2>/dev/null
used=$("$COHFS" mkfs -p $BOOT_AT "$OUT.new" $BOOT_N $BOOT_I "$W/boot" |
	tee /dev/stderr | sed -n 's/.* \([0-9][0-9]*\)\/[0-9]* blocks used.*/\1/p')
if [ "$used" -gt $ROM_SAFE ]; then
	echo "build.sh: the boot partition uses $used blocks; the ROM reads $ROM_SAFE" >&2
	exit 1
fi
"$COHFS" mkfs -p $ROOT_AT "$OUT.new" $ROOT_N $ROOT_I "$STAGE" >&2
"$COHFS" mkfs -p $TMP_AT "$OUT.new" $TMP_N $TMP_I "$W/tmp" >&2
mv "$OUT.new" "$OUT"
{
	echo "image=test"
	echo "userland=$V"
	echo "kernel=$(sh "$ROOT/mk/deps.sh" -k kernel)"
	echo "kboot=$(sh "$ROOT/mk/deps.sh" -k kboot)"
	echo "tools=$(sh "$ROOT/mk/deps.sh" -k tools)"
	echo "components=$COMPONENTS"
} > "$OUT.stamp"
echo "build.sh: $OUT ($V)"
