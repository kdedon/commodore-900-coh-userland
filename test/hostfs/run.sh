#!/bin/sh
# tests/hostfs/run.sh -- the host-directory pass-through, checked end to end
# on the target with the guest's OWN tools.
#
# A host directory is rendered as a COHERENT filesystem on a floppy medium
# (hostfsd -render), the emulator attaches it with --floppy, and the guest
# mounts it with the stock /etc/mount on the shipped /dev/fd1 node; `ls',
# `cat' and `cp' are the ordinary commands out of the image.  Afterwards
# hostfsd -extract returns what the guest wrote.
#
# The subject of every assertion is a NONCE minted per run and written only
# into the export directory; phase 1 proves it absent from the dist image, so
# a guest that prints it read the host directory, not its own disk.  Phase 5
# is the negative control: the identical command file with no --floppy
# attached must fail every positive assertion.
#
# Cost: two emulator boots, about 100 s. Host-side only; nothing is installed
# on the target and no image is rebuilt.
#
#	sh run.sh		positive + negative control
#	KEEP=1 sh run.sh	leave the work directory for inspection
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HB=$(cd "$HERE/../../hostbuild" && pwd)
DIST=${DIST:-coherent3-full-test}
# The emulator is resolved by mk/emulator.sh, the same search list that
# mk/compiler.mk and the hostbuild harnesses use; $EMUBIN names one
# explicitly.  The firmware sits beside bin/ in the same checkout.
C900_ROOT=$(cd "$HERE/../.." && pwd)
. "$C900_ROOT/mk/emulator.sh"
C900_EMU_ROM=${EMUROM:-$C900_EMU_ROM}
emu_need_rom "boot the target and run this gate"
EMUBIN=$C900_EMU
EMUROM=$C900_EMU_ROM
# The image is the distribution repository's, not this tree's own
# hostbuild/build (mk/dist.sh) -- packing moved to a separate repository.
. "$C900_ROOT/mk/dist.sh"
IMG=$(dist_img "$DIST") || exit 2
# hostfsd is a Go host tool: it imports the private simulator, so it lives in
# c900oses/gotools and not in this repository's own build (mk/gotools.sh).
. "$C900_ROOT/mk/gotools.sh"
gotools_need hostfsd "render/extract the host pass-through medium"
HOSTFSD=$GOTOOLS_BIN

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$IMG" "$HOSTFSD"; do
	[ -e "$f" ] || { echo "hostfs: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK/export"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# The nonce: pid and clock, so a stale transcript or floppy from an earlier
# run cannot satisfy this run's assertions.
NONCE=HFHOST-$$-$(date +%s)
GNONCE=HFGUEST-$$-$(date +%s)
echo "$NONCE" > "$WORK/export/hostmark.txt"
echo "host file number two" > "$WORK/export/other.txt"

echo "hostfs pass-through, nonce $NONCE"

# ---------------------------------------------------------------- phase 1
# Provenance precondition: the nonce is not in the image the guest boots.
echo "phase 1: nonce is absent from the booted image"
if grep -ac "$NONCE" "$IMG" 2>/dev/null | grep -qv '^0$'; then
	fail "the nonce is already in $DIST.bin -- phase 3 would prove nothing"
else
	ok "$(basename "$IMG") does not contain $NONCE"
fi

# ---------------------------------------------------------------- phase 2
echo "phase 2: render the export directory onto a floppy medium"
if "$HOSTFSD" -render "$WORK/floppy.img" -dir "$WORK/export" -floppy \
	> "$WORK/render.log" 2>&1
then
	ok "rendered ($(wc -c < "$WORK/floppy.img") B)"
else
	fail "hostfsd -render: $(tail -1 "$WORK/render.log")"
	echo "hostfs: cannot continue"; exit 1
fi

# The guest command file, used by BOTH runs unchanged.  Everything in it is a
# stock command out of the image: mount, ls, cat, cp, umount.
cat > "$WORK/cmds" <<EOF
/etc/mount /dev/fd1 /mnt
ls -l /mnt
cat /mnt/hostmark.txt
cp /mnt/hostmark.txt /guestcopy.txt
cat /guestcopy.txt
cp /mnt/other.txt /mnt/other2.txt
echo $GNONCE > /mnt/back.txt
/etc/umount /dev/fd1
EOF

# run <tag> <floppy-or-none> -- one emulator run; transcript lands in
# $WORK/<tag>.out.  Each run gets its own copy of the image (emu-run.sh does
# that itself via WORK) so neither run can see the other's writes.
run() {
	tag=$1; fl=$2
	if [ "$fl" = none ]; then
		cat > "$WORK/emu.$tag" <<EOF
#!/bin/sh
exec "$EMUBIN" --firmware="$EMUROM" "\$@"
EOF
	else
		cat > "$WORK/emu.$tag" <<EOF
#!/bin/sh
exec "$EMUBIN" --firmware="$EMUROM" --floppy="$fl" "\$@"
EOF
	fi
	chmod +x "$WORK/emu.$tag"
	# emu-run.sh takes its work-image path in $WORK too; expand every path
	# here, since assignments in one command prefix are visible to the
	# later ones in the same prefix.
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	remu=$WORK/emu.$tag
	# C900_EMU, not EMU: mk/emulator.sh takes $EMU only when C900_EMU is
	# unset, and a caller that resolved the emulator once and exported it
	# (the release sweep does) would otherwise silently get the bare
	# binary here -- and with it no --floppy, which is the whole medium
	# this harness is about.
	C900_EMU=$remu WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$HB/emu-run.sh" "$WORK/cmds" "$DIST" > "$WORK/$tag.harness" 2>&1
	[ -s "$WORK/$tag.out" ]
}

# assert_positive <tag> -- the four claims that constitute "tier 1 works".  Used
# on the real run (all must hold) and on the control (none may hold).  Returns
# the number that held, so the control can require zero.
assert_positive() {
	t=$WORK/$1.out
	held=0
	# The transcript ECHOES every command the guest was fed, so a filename
	# is in it whether or not the mount worked; the `ls -l' claims match
	# the LONG-FORMAT LINE (mode, owner, size), which only an ls that
	# found the file can print.
	grep -q "$NONCE" "$t" && held=$((held + 1))		# 1 cat over the mount
	grep -q '^-rw.*[0-9]  *hostmark\.txt' "$t" && held=$((held + 1))
	grep -q '^-rw.*[0-9]  *other\.txt' "$t" && held=$((held + 1))
	# 4: the nonce twice -- once from the mount, once from the guest's own
	# copy of it on the guest's own disk, which is `cp' having worked.
	grep -c "$NONCE" "$t" 2>/dev/null | grep -qv '^[01]$' \
		&& held=$((held + 1))
	echo $held
}

# ---------------------------------------------------------------- phase 3
echo "phase 3: guest reads the host directory with ls/cat/cp"
if run pos "$WORK/floppy.img"; then
	held=$(assert_positive pos)
	if [ "$held" = 4 ]; then
		ok "all 4 read claims hold (nonce printed, both names listed, cp copy read back)"
	else
		fail "only $held of 4 read claims hold"
		sed -n '/mount/,$p' "$WORK/pos.out" | head -20 | sed 's/^/       | /'
	fi
else
	fail "the positive run produced no transcript"
fi

# ---------------------------------------------------------------- phase 4
echo "phase 4: guest writes come back to the host directory"
if "$HOSTFSD" -extract "$WORK/floppy.img" -dir "$WORK/export" \
	> "$WORK/extract.log" 2>&1
then
	ok "$(cat "$WORK/extract.log")"
else
	fail "hostfsd -extract: $(tail -1 "$WORK/extract.log")"
fi
if [ -f "$WORK/export/back.txt" ] && grep -q "$GNONCE" "$WORK/export/back.txt"
then
	ok "back.txt holds the guest's nonce $GNONCE"
else
	fail "back.txt missing or does not hold $GNONCE"
fi
if [ -f "$WORK/export/other2.txt" ] &&
   cmp -s "$WORK/export/other.txt" "$WORK/export/other2.txt"
then
	ok "other2.txt -- the guest's own cp, over the mount -- came back intact"
else
	fail "other2.txt missing or differs from the file the guest copied"
fi
# Extraction never deletes and never invents: the two files the host put there
# are still there, and nothing else appeared.
n=$(ls "$WORK/export" | wc -l)
if [ -f "$WORK/export/hostmark.txt" ] && [ "$n" = 4 ]; then
	ok "export holds exactly the 2 host files + the 2 guest ones"
else
	fail "export has $n entries, wanted 4: $(ls "$WORK/export" | tr '\n' ' ')"
fi

# ---------------------------------------------------------------- phase 5
# The control: same command file, same image, same assertions -- no medium.
# A claim that still holds here was never measuring the pass-through.
echo "phase 5: negative control -- identical run with no medium attached"
if run neg none; then
	held=$(assert_positive neg)
	if [ "$held" = 0 ]; then
		ok "all 4 read claims fail without the medium (they can fail)"
	else
		fail "$held claim(s) still hold with NO floppy attached --"
		fail "  those assertions do not measure the pass-through"
		sed -n '/mount/,$p' "$WORK/neg.out" | head -20 | sed 's/^/       | /'
	fi
	if grep -qi 'mount' "$WORK/neg.out"; then
		ok "control transcript: $(grep -i 'mount:' "$WORK/neg.out" | head -1)"
	fi
else
	fail "the control run produced no transcript"
fi

echo
if [ "$BAD" = 0 ]; then
	echo "hostfs: PASS -- host directory read and written by the guest's own"
	echo "        tools, and the same checks demonstrably fail without it"
	exit 0
fi
echo "hostfs: FAIL ($BAD)"
[ "${KEEP:-0}" = 1 ] && echo "        work kept in $WORK"
exit 1
