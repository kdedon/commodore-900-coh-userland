#!/bin/sh
# tests/stackhw/run.sh -- stack high-water marks, ON A BOOTED SYSTEM.
#
#	sh run.sh [daemons|cmds|deep|copy|cpverify|top|all]	default all
#	KEEP=1 sh run.sh			leave the work directory
#	DIST=<dist> sh run.sh			another image (default coherent3-full-test)
#
# stackhw(1) reports, for every live process, how far down its stack segment
# has been written.  The kernel zero-fills the whole segment at exec
# (sys/z8001/src/exec.c exstack -> salloc, no SFNCLR) and nothing re-clears it,
# so the lowest non-zero byte is the deepest frame that process has ever had.
#
# WHY IT HAS TO BOOT.  `c900 --exec' runs a guest program against the host's
# memory and filesystem: there is no kernel, no /dev/kmem and no process list,
# so a probe that reads the kernel's own view of a segment cannot run there at
# all.
#
# THE PHASES, because a mark is only worth what was exercised to make it:
#
#   daemons  the standing set the booted system is running -- inet and the
#	     switchboard from /etc/rc.net, syslogd from /etc/rc, and the cron and
#	     smtpd nothing else starts -- sampled idle, then again after the
#	     switchboard workload of tests/cmd/inetd.cmd (internal service, exec'd
#	     service, peer-to-program, refusal, fingerd end to end with
#	     finger(1), remshd, then chanmax's connection ceiling).  There is no talkd: the
#	     ntalk service is inetd's, and the child that holds its state is a
#	     fork of inetd, so its stack is sampled as one of the switchboard's.
#	     The switchboard is the system's own, stopped and started again on
#	     the configuration the workload needs: one machine has one holder of
#	     a port, so a second inetd beside rc.net's would measure neither.
#   cmds     a spread of commands, each started in the background and sampled
#	     while it runs, because a command that has exited has no segment
#	     left to read.
#   copy     cp's copy buffer against copy time at three sizes, each timed on
#	     the same file by the guest's own time(1).  The three are built from
#	     scratch copies of cmd/cp.c with only its buffer constant
#	     substituted, so the tree carries no measurement variants and the
#	     difference between them is the buffer.
#   cpverify cp, mv and cpdir as the tree has them, against the shipped
#	     binaries: same file, timed, and the results compared byte for byte.
#   top      top's process tables over two samples, against the shipped binary:
#	     the percentage column is a difference between samples and the load
#	     average comes from the kernel, so both are asserted.
#   deep     the commands whose depth is data dependent, one at a time under
#	     `stackhw -c', which samples that command's process alone and so
#	     keeps up with a command that runs for a second.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HB=$(cd "$HERE/../../hostbuild" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
. "$OS/hostbuild/toolchain.sh"		# sets $TC
CCZ="$TC/ccz"
DIST=${DIST:-coherent3-full-test}
ROOTPART=${ROOTPART:-136}
WHAT=${1:-all}
C900_ROOT=$ROOT
. "$ROOT/mk/emulator.sh"
emu_need "boot the target and measure its processes"
# inject.py puts the probe on a copy of the image: it addresses a partition by
# its start block, which is how the media descriptors name one, and creates a
# path the image does not have.
INJECT=$HERE/../rawalign/inject.py
# loutdis is a Go host tool: it imports the private simulator, so it lives in
# c900oses/gotools and not in the toolchain (mk/gotools.sh), which refuses by
# name -- the tree, and go itself -- rather than leaving an empty path for the
# loop below to report as a missing nothing.
. "$ROOT/mk/gotools.sh"
gotools_need loutdis "read the probe's stack frames back out of the binary"
LOUTDIS=$GOTOOLS_BIN
# The image is the distribution repository's product (mk/dist.sh), and so is
# the directory emu-run.sh resolves a dist name in, which is where the staged
# copies below are put for it to boot.
. "$ROOT/mk/dist.sh"
IMG=$(dist_img "$DIST") || exit 2
# The linked kernel this probe reads its symbols out of belongs to the kernel
# repository ($KDIR); this tree links no kernel.  fsread.py, which reads a file
# back out of a packed filesystem, is the distribution repository's.
KHB=$KDIR/os/hostbuild
FSREAD=$C900_DIST/os/hostbuild/fsread.py

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

[ -n "$KDIR" ] || {
	echo "stackhw: needs the kernel repository's build tree, for the linked" >&2
	echo "  kernel its symbols come from; sh mk/deps.sh -n kernel" >&2
	exit 2; }
# Each is named as well as spelled: a variable that resolved to nothing still
# fails the test, and "missing  -- cannot run" names neither what is wanted nor
# what wanted it.
for f in "IMG=$IMG" "CCZ=$CCZ" "KERNEL=$KHB/kobj/kernel.out" \
	 "FSREAD=$FSREAD" "INJECT=$INJECT" "LOUTDIS=$LOUTDIS"; do
	case $f in *=) echo "stackhw: ${f%=} resolved to nothing -- cannot run" >&2; exit 2;; esac
	[ -e "${f#*=}" ] || { echo "stackhw: missing ${f%%=*} (${f#*=}) -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# --------------------------------------------------------------- phase 0
# The image must carry the kernel whose symbol table the probe is aimed at:
# every offset below is read out of kobj/kernel.out on the host and seeked to
# in a guest booted from the image, so a mismatch reads plausible numbers off
# the wrong addresses -- which it has done here before.  So the kernel is
# taken back OUT of the packed filesystem and compared with the one the
# offsets come from, rather than two stamps being compared with each other.
# dist.py patches DATA WORDS into the packed copy -- rootdev_ and pipedev_ (a
# dev_t each), release_ (the image's release, 32 bytes of string and padding)
# and the wd(4) parameter and boot tables -- which is why the allowance below
# is the size of those fields together and not zero.  A relink moves thousands
# of bytes, so the two cases are nowhere near each other.
echo "phase 0: the image carries the kernel the probe is aimed at"
python3 "$FSREAD" "$IMG" cat /coherent --part "$ROOTPART" > "$WORK/oncoherent" \
	2> "$WORK/fsread.err"
if [ ! -s "$WORK/oncoherent" ]; then
	fail "could not read /coherent (part $ROOTPART) out of $IMG:"
	sed 's/^/       | /' "$WORK/fsread.err"
	echo "stackhw: cannot continue"; exit 1
fi
KOUT=$KHB/kobj/kernel.out
if [ "$(wc -c < "$WORK/oncoherent")" != "$(wc -c < "$KOUT")" ]; then
	fail "/coherent is $(wc -c < "$WORK/oncoherent") bytes and $KOUT is" \
	     "$(wc -c < "$KOUT") -- a different kernel"
	echo "stackhw: cannot continue"; exit 1
fi
NDIFF=$(cmp -l "$KOUT" "$WORK/oncoherent" 2>/dev/null | wc -l)
if [ "$NDIFF" -le 64 ]; then
	ok "/coherent in the image is $KOUT ($NDIFF byte(s) patched by dist.py)"
else
	fail "/coherent differs from $KOUT in $NDIFF bytes -- the image was" \
	     "packed from another link of the kernel"
	echo "stackhw: cannot continue"; exit 1
fi

# procq_'s offset in the kernel's data segment.  nlist(3) cannot supply it (it
# assigns the 32-bit ldsym.ls_addr to a 16-bit n_value and keeps the segment
# word), so it is read from the kernel's symbol table on the host.
PROCQ=$("$LOUTDIS" -syms "$KHB/kobj/kernel.out" | awk '$3=="procq_"{print $1}')
case "$PROCQ" in
????????) Q=$((0x${PROCQ#????}));;
*)	echo "stackhw: no procq_ in kobj/kernel.out symbol table" >&2; exit 1;;
esac
ok "procq_ at 0x$PROCQ, offset $Q in the kernel's data segment"

# --------------------------------------------------------------- phase 1
echo "phase 1: build the probe"
# The probe walks the kernel's own PROC and SEG structures, so it compiles
# against the kernel's headers as well as this tree's: <sys/proc.h> reaches
# <sys/timeout.h>, which exists only in the kernel repository ($KINC).
if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	-I "$KINC" -I "$KINC/sys" \
	-o "$WORK/stackhw" "$HERE/stackhw.c" > "$WORK/cc.log" 2>&1; then
	ok "stackhw: $(wc -c < "$WORK/stackhw") B"
else
	fail "stackhw did not compile: $(tail -3 "$WORK/cc.log")"
	echo "stackhw: cannot continue"; exit 1
fi

# The three cp variants, from scratch copies of the shipped source.  Only the
# buffer differs, so a difference in copy time is the buffer's.
#
# cp.c works in BUFSIZ units throughout -- the array, the read, the hole scan
# and the write are all that one constant -- so the variant is made by
# redefining it after the include that sets it, and not by resizing the array
# alone, which would leave the loop reading a block at a time out of a bigger
# buffer and measure nothing.
if [ "$WHAT" = copy ] || [ "$WHAT" = all ]; then
	for sz in 25600 512 4096; do
		awk -v sz=$sz '
			/^#[ \t]*include/ { print; inc = 1; next }
			inc && !done { print "#undef\tBUFSIZ";
				       print "#define\tBUFSIZ\t" sz; done = 1 }
			{ print }' "$OS/base/cmd/cp.c" > "$WORK/cp$sz.c"
		grep -q "^#define	BUFSIZ	$sz\$" "$WORK/cp$sz.c" || {
			fail "cp.c: no include to define its buffer after -- cannot vary it"
			echo "stackhw: cannot continue"; exit 1; }
		if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
			-o "$WORK/cp$sz" "$WORK/cp$sz.c" \
			> "$WORK/cc.cp$sz.log" 2>&1; then
			ok "cp with buf[$sz]: $(wc -c < "$WORK/cp$sz") B"
		else
			fail "cp buf[$sz] did not compile: $(tail -3 "$WORK/cc.cp$sz.log")"
			echo "stackhw: cannot continue"; exit 1
		fi
	done
fi

# stage <tag> <file=guestpath> ... -- a copy of the image carrying the probe and
# whatever else the phase needs.
stage() {
	tag=$1; shift
	cp --reflink=auto -f "$IMG" "$WORK/$tag.bin" 2>/dev/null || \
		cp -f "$IMG" "$WORK/$tag.bin"
	set -- "$WORK/stackhw=/bin/stackhw" "$@"
	inj=""
	for pair; do inj="$inj ${pair%%=*} ${pair#*=}"; done
	python3 "$INJECT" --mode 755 "$WORK/$tag.bin" "$ROOTPART" $inj \
		> "$WORK/inj.$tag.log" 2>&1 ||
		{ fail "could not stage $tag"; sed 's/^/       | /' "$WORK/inj.$tag.log"; return 1; }
}

# run <tag> -- boot the staged image once and feed it $WORK/<tag>.cmds.
# emu-run.sh owns the emulator: it kills and waits for it on every exit path,
# including its own timeout, and reclaims one a killed harness left behind.
run() {
	tag=$1
	cp -f "$WORK/$tag.bin" "$C900_IMGDIR/stackhw-$tag.bin"
	# Named before the assignments below, which would otherwise expand $WORK
	# to the per-run image copy they are setting.
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	WORK=$rwork OUT=$rout ERR=$rerr EMUWAIT=${EMUWAIT:-900} \
		timeout -k 10 $(( ${EMUWAIT:-900} + 300 )) \
		sh "$HB/emu-run.sh" "$WORK/$tag.cmds" "stackhw-$tag" \
		> "$WORK/$tag.harness" 2>&1
	rc=$?
	rm -f "$C900_IMGDIR/stackhw-$tag.bin"
	[ $rc -le 1 ] || fail "$tag: harness exited $rc"
	grep -q __EMU_DONE__ "$WORK/$tag.out" 2>/dev/null ||
		fail "$tag: the guest never reached the end of the script"
	[ -s "$WORK/$tag.out" ]
}

# table <tag> ... -- every mark that was measured, deepest first, one line per
# (program, tag) with the largest sample kept.
table() {
	# The transcript comes off a serial console, so the CR ends the last
	# field; the fields wanted are named, not positional, except for the tag
	# and the program: SHW <tag> <pid> <program> key=value ...
	tr -d '\r' < "$1" | awk '/^SHW /{
		if ($5 ~ /^size=/) {
			prog=$4; tag=$2;
			hw=0; fr=0; mz=0; sz=0;
			for (i=5; i<=NF; i++) {
				split($i, kv, "=");
				if (kv[1]=="hw") hw=kv[2]+0;
				if (kv[1]=="frames") fr=kv[2]+0;
				if (kv[1]=="maxz") mz=kv[2]+0;
				if (kv[1]=="size") sz=kv[2]+0;
			}
			k=prog;
			if (k in best == 0 || hw > best[k]) { best[k]=hw; bfr[k]=fr; bmz[k]=mz; btag[k]=tag; bsz[k]=sz }
		}
	}
	END{
		printf "  %-12s %8s %8s %8s %6s  %s\n", "program", "size", "hw", "frames", "maxz", "worst sample";
		n=0; for (k in best) { n++; ord[n]=k }
		for (i=1; i<=n; i++) for (j=i+1; j<=n; j++)
			if (best[ord[j]] > best[ord[i]]) { t=ord[i]; ord[i]=ord[j]; ord[j]=t }
		for (i=1; i<=n; i++) { k=ord[i];
			printf "  %-12s %8d %8d %8d %6d  %s\n", k, bsz[k], best[k], bfr[k], bmz[k], btag[k] }
	}'
}

# ------------------------------------------------------------- daemons
if [ "$WHAT" = daemons ] || [ "$WHAT" = all ]; then
echo "phase 2: the standing daemons, idle and then under the switchboard workload"
cat > "$WORK/daemons.cmds" <<EOF
# THE STACK IS ALREADY UP.  This guest boots multi user: /etc/rc has started
# syslogd and /etc/rc.net has mknod'd /dev/inet, started /etc/inet, addressed
# the interface and started the switchboard on /etc/inetd.conf.  Starting any
# of those a second time measures a machine nobody runs, so none of them is
# started here and the idle sample below is of the set the system itself
# brought up.
#
# The one exception is the switchboard, and it is replaced rather than added
# to.  Two inetds cannot hold one port -- the second passive open is refused
# and the port then answers whichever got there first (/etc/rc.net says so in
# as many words) -- and the workload below needs services that
# /etc/inetd.conf does not offer: an exec'd one, a peer-to-program one, and a
# port configured nowhere.  So rc.net's inetd is asked to stop, which gives
# every listening socket back to the stack and collects the ntalk child that
# holds port 518, and this phase's inetd is then the only switchboard on the
# machine.  What is measured is still one switchboard serving every kind of
# path it has, which is what the phase is for.
#
# The pid comes out of ps(1), whose columns are TTY, PID and then the command:
# awk writes the kill line and sh runs it, because this shell is typed to
# through a pacing emulator and a backquote round trip is not worth the risk.
sleep 10
/bin/ps -ax > /tps.out
/bin/awk '\$3 == "/etc/inetd" { print "/bin/kill", \$2 }' /tps.out > /tkill.sh
/bin/cat /tkill.sh
/bin/sh /tkill.sh
sleep 5
# cron and smtpd are started here because nothing else starts them: rc.net
# leaves smtp to the switchboard's own line, and this phase's configuration
# has none, so there is exactly one holder of port 25.
/etc/cron &
/etc/smtpd -m 1 &
/bin/logger stackhw: syslogd has a line to write
echo T_IDLE
/bin/stackhw -q $Q idle
# The switchboard's own workload, from tests/cmd/inetd.cmd: five services, one
# of each kind of path.
echo 'echo \$INETD_REMADDR \$INETD_LOCPORT > /tinetd.env' > /bin/hello
echo echo hello >> /bin/hello
/bin/chmod 755 /bin/hello
echo 'echo \$INETD_REMADDR' > /bin/renv
/bin/chmod 755 /bin/renv
# remshd refuses uid 0, so the session that is measured below is a non-root one,
# and a .rhosts for that account is what lets it in: none is shipped.
/bin/mkdir /usr/guest
echo c900.localnet root > /usr/guest/.rhosts
/bin/chmod 644 /usr/guest/.rhosts
echo echo stream tcp nowait root internal > /tinetd.conf
echo 7777 stream tcp nowait root /bin/echo echo hello >> /tinetd.conf
echo 7778 stream tcp nowait root /bin/sh sh >> /tinetd.conf
echo finger stream tcp nowait root /etc/fingerd fingerd >> /tinetd.conf
echo shell stream tcp nowait root /etc/remshd remshd >> /tinetd.conf
echo ntalk dgram udp wait root internal >> /tinetd.conf
/etc/inetd -d /tinetd.conf &
sleep 10
echo T_INTERNAL
/bin/echoclient 10.0.0.2 7
sleep 2
echo T_EXEC
/bin/echoclient 10.0.0.2 7777
sleep 2
echo T_DOWN
/bin/echoclient 10.0.0.2 7778
sleep 5
/bin/cat /tinetd.env
echo T_NEG
/bin/echoclient 10.0.0.2 7779
sleep 2
# A sample taken WHILE a service is being served, to catch the forked child of
# inetd and the daemon it runs: both are gone seconds later.
echo T_DURING
/bin/finger root@10.0.0.2 &
/bin/stackhw -q $Q during
sleep 5
echo T_FINGERD
/bin/finger root@10.0.0.2
sleep 5
echo T_REMSHD
/bin/remsh -l guest 10.0.0.2 /bin/renv
sleep 5
echo T_TELNETD
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=23 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1023 /etc/telnetd < /dev/null > /ttelnetd.out
echo QUIT > /tsmtp.in
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=25 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1024 /etc/smtpd < /tsmtp.in
echo T_WORK
/bin/stackhw -q $Q work
echo T_CEILING
/bin/chanmax 10 3
/bin/stackhw -q $Q ceiling
/bin/ps -al
EOF
	if stage daemons && run daemons; then
		grep -c '^SHW ' "$WORK/daemons.out" > "$WORK/n" ||:
		ok "$(cat "$WORK/n") marks in the transcript"
		table "$WORK/daemons.out"
	else
		fail "the daemon run produced no transcript"
	fi
fi

# ---------------------------------------------------------------- cmds
if [ "$WHAT" = cmds ] || [ "$WHAT" = all ]; then
echo "phase 3: a spread of commands, sampled while each one runs"
cat > "$WORK/cmds.cmds" <<EOF
# Each command is started in the background and sampled twice while it runs: a
# command that has exited has no stack segment left to read, and one sample can
# fall either side of a short run.
#
# Every output goes to /dev/null or is removed as soon as it has been sampled,
# and the input is two copies of /etc/helpfile.  The root filesystem has a few
# hundred kilobytes free, and a run that fills it reports "Out of space" from
# whichever command got there first while the samples still look ordinary.
/bin/df /
/bin/cat /etc/helpfile /etc/helpfile > /text
echo '{n=n+length($0)} END{print n}' > /awkprog
/bin/wc -c /text
# A deep directory, for the commands whose recursion is over directories rather
# than over an input file.
/bin/mkdir /d1
/bin/mkdir /d1/d2
/bin/mkdir /d1/d2/d3
/bin/mkdir /d1/d2/d3/d4
/bin/mkdir /d1/d2/d3/d4/d5
/bin/mkdir /d1/d2/d3/d4/d5/d6
/bin/mkdir /d1/d2/d3/d4/d5/d6/d7
/bin/mkdir /d1/d2/d3/d4/d5/d6/d7/d8
/bin/cp /etc/passwd /d1/d2/d3/d4/d5/d6/d7/d8/f
echo T_NROFF
/bin/nroff /text > /dev/null 2> /dev/null &
/bin/stackhw -q $Q nroff
/bin/stackhw -q $Q nroff
echo T_AWK
/bin/awk '{n=n+length(\$0)} END{print n}' /text > /dev/null &
/bin/stackhw -q $Q awk
/bin/stackhw -q $Q awk
echo T_SORT
/bin/sort /text > /sort.out &
/bin/stackhw -q $Q sort
/bin/stackhw -q $Q sort
echo T_SED
/bin/sed -e 's/e/E/g' /text > /sed.out &
/bin/stackhw -q $Q sed
/bin/stackhw -q $Q sed
echo T_GREP
/bin/grep -c e /text > /dev/null &
/bin/stackhw -q $Q grep
echo T_DIFF
/bin/diff /text /sed.out > /dev/null &
/bin/stackhw -q $Q diff
/bin/stackhw -q $Q diff
/bin/rm -f /sort.out /sed.out
echo T_CP
/bin/cp /bin/t4 /t4copy &
/bin/stackhw -q $Q cp
/bin/stackhw -q $Q cp
/bin/rm -f /t4copy
echo T_LS
/bin/ls -lR / > /dev/null &
/bin/stackhw -q $Q ls
/bin/stackhw -q $Q ls
echo T_FIND
/bin/find / -print > /dev/null &
/bin/stackhw -q $Q find
/bin/stackhw -q $Q find
echo T_CPDIR
/bin/cpdir /d1 /d1copy &
/bin/stackhw -q $Q cpdir
echo T_TAR
/bin/tar cf /dev/null /etc &
/bin/stackhw -q $Q tar
/bin/stackhw -q $Q tar
echo T_SHREC
echo 'echo \$1; /bin/test \$1 -gt 1 && /rec \`/bin/expr \$1 - 1\`' > /rec
/bin/chmod 755 /rec
/rec 12 > /dev/null &
/bin/stackhw -q $Q shrec
/bin/stackhw -q $Q shrec
echo T_TOP
/bin/top -b -d 3 -s 1 500 > /dev/null &
/bin/stackhw -q $Q top
/bin/stackhw -q $Q top
echo T_PS
/bin/ps -al > /dev/null &
/bin/stackhw -q $Q ps
echo T_MAN
/bin/man man > /dev/null 2> /dev/null &
/bin/stackhw -q $Q man
echo T_DONE
/bin/stackhw -q $Q settle
/bin/df /
EOF
	if stage cmds && run cmds; then
		ok "$(grep -c '^SHW ' "$WORK/cmds.out") marks in the transcript"
		table "$WORK/cmds.out"
	else
		fail "the command run produced no transcript"
	fi
fi

# ---------------------------------------------------------------- copy
if [ "$WHAT" = copy ] || [ "$WHAT" = all ]; then
echo "phase 4: cp's buffer against copy time"
cat > "$WORK/copy.cmds" <<EOF
# The same 250 KB file three times with each buffer size, timed by the guest.
# /bin/t4 is the largest file on the root partition; the copies are removed
# between runs so every one of them allocates fresh blocks.
/bin/wc -c /bin/t4
echo T_25600
/bin/time /bin/cp25600 /bin/t4 /c1
/bin/rm -f /c1
/bin/time /bin/cp25600 /bin/t4 /c1
/bin/rm -f /c1
/bin/time /bin/cp25600 /bin/t4 /c1
/bin/rm -f /c1
echo T_4096
/bin/time /bin/cp4096 /bin/t4 /c2
/bin/rm -f /c2
/bin/time /bin/cp4096 /bin/t4 /c2
/bin/rm -f /c2
/bin/time /bin/cp4096 /bin/t4 /c2
/bin/rm -f /c2
echo T_512
/bin/time /bin/cp512 /bin/t4 /c3
/bin/rm -f /c3
/bin/time /bin/cp512 /bin/t4 /c3
/bin/rm -f /c3
/bin/time /bin/cp512 /bin/t4 /c3
echo T_CMP
/bin/cmp /bin/t4 /c3
echo T_SIZE
/bin/size /bin/cp25600 /bin/cp4096 /bin/cp512
EOF
	if stage copy "$WORK/cp25600=/bin/cp25600" "$WORK/cp4096=/bin/cp4096" \
		"$WORK/cp512=/bin/cp512" && run copy; then
		ok "copy transcript:"
		sed -n '/^T_25600/,/^T_SIZE/p' "$WORK/copy.out" | sed 's/^/       | /'
		sed -n '/^T_SIZE/,$p' "$WORK/copy.out" | sed 's/^/       | /'
	else
		fail "the copy run produced no transcript"
	fi
fi

# ---------------------------------------------------------------- deep
# The commands whose stack depth is data dependent, each measured on its own by
# stackhw -c: the probe forks, the child execs the command, and the parent
# samples nothing but that child until it is gone.  Sampling one process instead
# of all of them is what makes it fast enough to catch a command that runs for a
# second, which the `cmds' phase cannot.
if [ "$WHAT" = deep ] || [ "$WHAT" = all ]; then
echo "phase 7: the data-dependent recursions, one command at a time"
cat > "$WORK/deep.cmds" <<EOF
# /usr carries the deepest directory tree on the image, so the commands whose
# recursion is over directories are pointed at the mounted root rather than at a
# chain built here.  The device /usr lives on differs by media descriptor, so
# both are tried and whichever is not this image says so and is ignored.
/etc/mount /dev/hd6 /usr
/etc/mount /dev/hd1 /usr
/bin/df /
/bin/cat /etc/helpfile /etc/helpfile > /text
echo '{n=n+length(\$0)} END{print n}' > /awkprog
echo T_DEEP
/bin/stackhw -q $Q -c '/bin/find / -print' find
/bin/stackhw -q $Q -c '/bin/ls -lR /usr' lsR
/bin/stackhw -q $Q -c '/bin/cpdir /etc /etccopy' cpdir
/bin/stackhw -q $Q -c '/bin/cp /bin/t4 /t4copy' cp
/bin/rm -f /t4copy
/bin/stackhw -q $Q -c '/bin/tar cf /dev/null /etc' tar
/bin/stackhw -q $Q -c '/bin/nroff /text' nroff
/bin/stackhw -q $Q -c '/bin/awk -f /awkprog /text' awk
/bin/stackhw -q $Q -c '/bin/yacc /text' yacc
/bin/stackhw -q $Q -c '/bin/make -f /text' make
/bin/stackhw -q $Q -c '/bin/sort /text' sort
/bin/stackhw -q $Q -c '/bin/man man' man
/bin/stackhw -q $Q -c '/bin/top -b -d 2 -s 1 500' top
/bin/stackhw -q $Q -c '/bin/ps -al' ps
/bin/stackhw -q $Q -c '/bin/diff /text /etc/helpfile' diff
/bin/stackhw -q $Q -c '/bin/sed -e s/e/E/g /text' sed
/bin/stackhw -q $Q -c '/bin/sh -c /bin/date' shell
/bin/stackhw -q $Q -c '/bin/grep -c e /text' grep
/bin/stackhw -q $Q -c '/bin/wc -l /text' wc
/bin/stackhw -q $Q -c '/bin/echo hello' echo
/bin/rm -rf /etccopy
echo T_DONE
/bin/df /
EOF
	if stage deep && run deep; then
		ok "$(grep -c '^SHW ' "$WORK/deep.out") commands measured"
		tr -d '\r' < "$WORK/deep.out" | grep '^SHW ' | sed 's/^/       | /'
	else
		fail "the deep run produced no transcript"
	fi
fi

# ------------------------------------------------------------- cpverify
# The three commands whose copy buffer is BUFSIZ, against the shipped binaries
# on the same files: the copy must be byte-identical and no slower.  mv's buffer
# is only reached when the move crosses a filesystem, so /usr is mounted for it
# -- a same-filesystem mv is a rename and copies nothing.
if [ "$WHAT" = cpverify ] || [ "$WHAT" = all ]; then
echo "phase 6: cp, mv and cpdir against the shipped binaries"
for c in cp mv cpdir; do
	if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
		-o "$WORK/n$c" "$OS/base/cmd/$c.c" > "$WORK/cc.$c.log" 2>&1; then
		ok "$c: $(wc -c < "$WORK/n$c") B, $("$LOUTDIS" -segs "$WORK/n$c" | sed -n 's/.*SIPDATA  private *[0-9]* *\([0-9]*\).*/\1 bytes of private data/p' | head -1)"
	else
		fail "$c did not compile: $(grep -v 'Strict\|Warning' "$WORK/cc.$c.log" | tail -3)"
	fi
done
cat > "$WORK/cpverify.cmds" <<EOF
# The device /usr lives on differs by media descriptor, so both are tried and
# whichever is not this image says so and is ignored.
/etc/mount /dev/hd6 /usr
/etc/mount /dev/hd1 /usr
/bin/wc -c /bin/t4
echo T_OLDCP
/bin/time /bin/cp /bin/t4 /old1
/bin/rm -f /old1
/bin/time /bin/cp /bin/t4 /old1
echo T_NEWCP
/bin/time /bin/ncp /bin/t4 /new1
/bin/rm -f /new1
/bin/time /bin/ncp /bin/t4 /new1
echo T_CPSAME
/bin/cmp /bin/t4 /new1
/bin/cmp /old1 /new1
echo T_NEWMV
/bin/ncp /bin/t4 /mv1
/bin/time /bin/nmv /mv1 /usr/mv1
/bin/cmp /bin/t4 /usr/mv1
/bin/ls /mv1
echo T_NEWCPDIR
/bin/ncpdir /etc /etccopy
/bin/cmp /etc/helpfile /etccopy/helpfile
/bin/ls /etc | /bin/wc -l
/bin/ls /etccopy | /bin/wc -l
echo T_SIZES
/bin/size /bin/cp /bin/ncp /bin/mv /bin/nmv /bin/cpdir /bin/ncpdir
EOF
	if stage cpverify "$WORK/ncp=/bin/ncp" "$WORK/nmv=/bin/nmv" \
		"$WORK/ncpdir=/bin/ncpdir" && run cpverify; then
		awk '/^T_OLDCP/,0' "$WORK/cpverify.out" | tr -d '\r' | sed 's/^/       | /'
	else
		fail "the cpverify run produced no transcript"
	fi
fi

# ------------------------------------------------------------------ top
# top's process tables are grown from the chain rather than fixed, and the
# fields it keeps between samples are the two deltas() needs.  What that can
# break is the CPU percentage, which is a difference between samples, and the
# load average, which is read from the kernel.  So the check is two batch
# displays, one interval apart, with both asserted.
if [ "$WHAT" = top ] || [ "$WHAT" = all ]; then
echo "phase 5: top over two samples, against the shipped binary"
# top drives the terminal through termcap, so it links libterm out of the
# toolchain's build directory -- built on demand, the way build-editors.sh and
# build-less.sh do it, since nothing here is guaranteed to have built it first.
CURSES=$TCB/curses
[ -f "$CURSES/libterm.a" ] || sh "$HB/build-curses.sh" >/dev/null 2>&1
if CCZ_VAR=800000020800 "$CCZ" -s -i -L \
	-I "$OS/base/cmd/top" -I "$HB/build/top" -I "$OS/include" -I "$OS/include/sys" \
	-I "$KINC" -I "$KINC/sys" \
	-o "$WORK/topnew" "$OS/base/cmd/top/top.c" "$OS/base/cmd/top/display.c" \
	"$OS/base/cmd/top/screen.c" "$OS/base/cmd/top/commands.c" "$OS/base/cmd/top/utils.c" \
	"$OS/base/cmd/top/username.c" "$OS/base/cmd/top/version.c" \
	"$OS/base/cmd/top/m_coherent.c" "$CURSES/libterm.a" \
	> "$WORK/cc.top.log" 2>&1; then
	ok "top: $(wc -c < "$WORK/topnew") B"
	"$LOUTDIS" -segs "$WORK/topnew" | sed -n '/l_ssize\[PRVD/p;/l_ssize\[BSSD/p;/SIPDATA/p;/K column/p' | sed 's/^/       | /'
else
	fail "top did not build: $(grep -v 'Strict\|Warning' "$WORK/cc.top.log" | tail -5)"
fi
cat > "$WORK/top.cmds" <<EOF
# Two displays a second apart, so the percentage column is a real difference
# between two samples, and 500 processes asked for so nothing is cut by the
# display count.
echo T_NEW
/bin/topnew -b -d 2 -s 1 500
echo T_OLD
/bin/top -b -d 2 -s 1 500
EOF
	if stage top "$WORK/topnew=/bin/topnew" && run top; then
		# Segmented on the echoed tags, and the last segment ends at the
		# harness's own marker: an open-ended range would give the first
		# binary credit for the second one's rows.
		# The console transcript carries the guest's CR, so the tags are
		# matched after it is taken off and not before: an anchored
		# pattern does not match `T_NEW\r'.
		tr -d '\r' < "$WORK/top.out" > "$WORK/top.txt"
		awk '/^T_NEW$/{f=1;next} /^T_OLD$/{f=0} f' "$WORK/top.txt" > "$WORK/top.NEW"
		awk '/^T_OLD$/{f=1;next} /^__EMU_DONE__$/{f=0} f' "$WORK/top.txt" > "$WORK/top.OLD"
		for w in NEW OLD; do
			if grep -q "load average" "$WORK/top.$w"; then
				ok "$w: $(grep 'load average' "$WORK/top.$w" | head -1)"
			else
				fail "$w: no load average line"
			fi
			n=$(grep -c '^ *[0-9][0-9]* ' "$WORK/top.$w")
			[ "$n" -gt 0 ] && ok "$w: $n process rows over two displays" ||
				fail "$w: no process rows"
			grep -q "leaves the arena\|no namelist\|does not match" "$WORK/top.$w" &&
				fail "$w: $(grep 'leaves the arena\|no namelist\|does not match' "$WORK/top.$w" | head -1)"
		done
		# The %CPU column of a process row, not any number on the page:
		# the Cpu states line is a figure the kernel's own counters
		# give and would answer this question without deltas() ever
		# having matched a process.  In a process row the only bare
		# N.N field is %CPU -- SIZE and RES carry a K, TIME a colon.
		if awk '/^ *[0-9][0-9]* /{for(i=1;i<=NF;i++)
			if($i ~ /^[0-9]+\.[0-9]$/ && $i+0 > 0) n=1
		} END{exit n ? 0 : 1}' "$WORK/top.NEW"; then
			ok "NEW: a non-zero percentage, so the between-sample delta survived"
		else
			fail "NEW: every percentage is zero -- deltas() found no previous sample"
		fi
	else
		fail "the top run produced no transcript"
	fi
fi

echo
if [ "$BAD" = 0 ]; then
	echo "stackhw: every mark above is against the allowance the kernel's own"
	echo "         SEG reported on the same line (size=)."
	exit 0
fi
echo "stackhw: FAIL ($BAD)"
exit 1
