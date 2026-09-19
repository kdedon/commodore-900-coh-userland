#!/bin/sh
# emu-run.sh <cmdfile> [image] -- run commands on the target under the minimal
# instruction-level emulator (a commodore-900-emulator checkout; see
# mk/emulator.sh for how it is found and what to set) and print the
# console transcript.  The image defaults to the test image.  The emulator
# writes through, so cohfs reads results straight back.
#
# The emulator takes the whole keystroke script on the command line and writes
# the console to stdout.  Use this for anything that does not need video, a
# second serial line, or a debugger.
#
# THE GUEST COMES UP MULTI-USER, at a login prompt: init repairs and remounts
# the root filesystem itself and reaches single user only when that fails, when
# the loader asks for it, or when /etc/ttys enables no line.  So the script
# below opens with a root login.
#
# It cannot be a paced line like the rest.  The emulator paces scripted input
# on the guest PRINTING a prompt character (`#' or `>'), and a login prompt has
# neither -- so nothing would ever be typed and every run would time out with
# an empty transcript.  The two emulator facilities for exactly this are used
# instead: --input-mark holds the \i (type-ahead) bytes until the guest has
# printed the text, and each byte of the login is marked \i, so `root' is typed
# when, and only when, `login:' appears.  From the shell prompt it produces,
# every later byte is paced normally.
#
# Ctrl-D cannot be sent: --input escapes are only \r \n \t \\ (and \g, \i).
# Nothing here needs it, since the login reaches multi-user without one.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CMDS=${1:?usage: emu-run.sh <cmdfile> [image]}
# The emulator is a consumed checkout, resolved by mk/emulator.sh -- the same
# search list mk/compiler.mk uses, in the same order.  $EMU names one
# explicitly (the buildenv harness, test/buildenv, passes a wrapper
# script through it).
C900_ROOT=$(cd "$HERE/.." && pwd)
. "$C900_ROOT/mk/emulator.sh"
EMU=$C900_EMU
# Defaults to the test image (test/image/build.sh).
IMG=${2:-$C900_ROOT/hostbuild/build/test.bin}
if [ ! -f "$IMG" ]; then
	echo "emu-run.sh: no image $IMG" >&2
	echo "  Pack the test image from this build:  sh $C900_ROOT/test/image/build.sh" >&2
	exit 2
fi
# Every per-run path is per-invocation by default: concurrent runs are the
# normal case, and a fixed name lets one run read back another's transcript.
# $$ is the run's identity; set OUT/ERR/WORK explicitly to name them yourself.
OUT=${OUT:-${TMPDIR:-/tmp}/emu-run.$$.out}

emu_need "boot the target and type commands at its console"

# State what is about to run before it runs: the stamp says which source the
# image under the transcript was built from.  Written by build-image.sh; a
# missing one is itself the answer.
. "$HERE/provenance.sh"
prov_header "image $(basename "$IMG")" "$IMG.stamp" || true

# Run on a copy.  The emulator writes through to the image and the guest
# mounts its root read-write, so a run mutates the disk it booted from and a
# reused image makes runs depend on their predecessors.  A fresh copy per run
# keeps every run starting from the same image.  WORK names the copy;
# PERSIST=1 writes to the image itself.
case "${PERSIST:-}" in
'' | 0 | no | NO | false | FALSE) ;;
*)	echo "emu-run.sh: PERSIST set -- writing through to $IMG" >&2
	WORK=$IMG ;;
esac
if [ -n "${WORK:-}" ]; then
	[ "$WORK" = "$IMG" ] ||
		cp --reflink=auto -f "$IMG" "$WORK" 2>/dev/null || cp -f "$IMG" "$WORK"
	IMG="$WORK"
else
	# Tagged with the pid: the tag is what keeps two runs off one copy,
	# and two guests mounting one file read-write is silent mutual
	# corruption.  Per-run copies accumulate, so sweep the ones whose run
	# is over first -- only this harness's own tag pattern, and only when
	# the pid in the name is dead.
	WD=$(dirname "$IMG")/work
	mkdir -p "$WD"
	for stale in "$WD"/*.emu.[0-9]*.bin; do
		[ -e "$stale" ] || continue
		spid=${stale##*.emu.}; spid=${spid%.bin}
		kill -0 "$spid" 2>/dev/null || rm -f "$stale"
	done
	W=$WD/$(basename "$IMG" .bin).emu.$$.bin
	cp --reflink=auto -f "$IMG" "$W" 2>/dev/null || cp -f "$IMG" "$W" ||
		{ echo "emu-run.sh: cannot copy $IMG to $W" >&2; exit 1; }
	IMG=$W
fi

# Build the keystroke script: every non-comment line, CR-terminated, then a
# sync and a final marker so we know when to stop.  The marker must be the LAST
# thing the guest prints, which is why it is echoed rather than inferred from the
# prompt.
#
# The `sync' is load-bearing even though the emulator writes through to the
# image: write-through is at the sector level, and COHERENT holds dirty blocks
# in its own buffer cache, so anything a command wrote may never reach a
# sector before the emulator stops.
MARK=__EMU_DONE__
# The login, typed at the prompt --input-mark waits for.  Every byte is \i, so
# the whole of it is delivered as soon as that prompt has been printed and the
# receiver is free; the paced script below follows from the shell prompt.
LOGINMARK=${LOGINMARK:-login:}
LOGIN=${LOGIN:-root}
script=''
_rest=$LOGIN
while [ -n "$_rest" ]; do
	script="$script\\i${_rest%"${_rest#?}"}"
	_rest=${_rest#?}
done
script="$script\\i\\r"
raw_seen=''
stopdir=''
while IFS= read -r line; do
	case "$line" in
	# `#% stop <list>': the script's own stop channels (see STOPON).
	'#% stop '*)	stopdir=${line#'#% stop '}; continue;;
	''|\#*)	continue;;
	# RAW <chars> -- append characters with NO carriage return, for a
	# program that reads single keystrokes rather than lines.
	#
	# The emulator feeds one byte at a time and, after every CR, waits for
	# a fresh `#' prompt before feeding the next (bus.c inq_wait_seq).  A
	# curses program prints no prompt, so the CR ending its first input is
	# normally the last byte it can ever receive.  Two things get round
	# that without touching the emulator: give the program its answers on
	# the command line so it never needs a CR, and note that hunt's maze is
	# drawn with DOOR = '#', which advances prompt_seq and releases the gate
	# as a side effect of the first frame.
	# The first RAW line also emits \g, which tells the emulator to stop
	# waiting for a `#' prompt from here on.  It has to be positional: the
	# setup commands above still need the gate, and with it off from the
	# start they race each other into a shell that is not reading yet.
	RAW\ *)	[ -n "$raw_seen" ] || { script="$script\\g"; raw_seen=1; }
		script="$script${line#RAW }";;
	# GATE -- release the prompt gate here and nowhere earlier, for a
	# program that reads LINES but is not the shell and so prints no `#'.
	# The in-kernel debugger's prompt is `*', for instance, and without this
	# its first command is the last byte the guest can ever be fed.  Same
	# positional rule as RAW: the commands before it still need the gate.
	GATE)	[ -n "$raw_seen" ] || { script="$script\\g"; raw_seen=1; };;
	*)		script="$script$line\\r";;
	esac
done < "$CMDS"
script="${script}sync\\rsync\\recho $MARK\\r"

# The emulator silently drops scripted input past INQMAX (8192) bytes.  Count
# what it will queue (an escape is one byte, \g and \i none) and refuse a
# script that won't fit.
qbytes=$(printf '%s' "$script" | sed 's/\\[gi]//g; s/\\./x/g' | wc -c)
if [ "$qbytes" -gt "${INQMAX:-8192}" ]; then
	echo "emu-run.sh: $CMDS types $qbytes bytes; the emulator queues only ${INQMAX:-8192}" >&2
	echo "  and would silently drop the rest.  Shorten the typed lines (a \`#' comment is not typed)." >&2
	exit 2
fi

# The emulator runs until Ctrl-] which we cannot send either, so run it in the
# background and stop it once the marker appears.  Kill by PID, not by name:
# never disturb anything else the session has running.
# `exec' replaces the subshell with the emulator, so $! is the process that
# needs killing -- without it $! is the subshell's pid and the emulator spins
# on unkilled.  The cd moves to the parent, since the emulator resolves
# ../rom and ../disk relative to its working directory.
: > "$OUT"
#
# stderr goes to $ERR rather than /dev/null: the emulator writes its MMU
# violation trace there when CSIM_MMU_DEBUG is set, the only way to see a
# fault the kernel then turns into a signal.
ERR=${ERR:-${TMPDIR:-/tmp}/emu-run.$$.err}
: > "$ERR"

# Reclaim an emulator a previous run leaked, before starting this one: a
# SIGKILLed harness leaves the emulator spinning with no marker to stop it
# and no trap that can fire.  Each run records `<harness-pid> <emulator-pid>'
# in its own file, and both conditions must hold before anything is killed:
# the harness that started it must be gone, and the recorded pid must still
# be running the emulator, because pids are reused.
#
# Do not test for an orphan by `ppid == 1': a process whose parent exits
# reparents to `systemd --user', not to init, so that test never fires.
PIDD=${EMUPIDDIR:-$HOME/.cache/c900/emu.d}
mkdir -p "$PIDD"
for f in "$PIDD"/*.pid; do
	[ -e "$f" ] || continue
	read -r ownr old rest < "$f" || continue
	case $(ps -o comm= -p "${old:-0}" 2>/dev/null) in
	"$(basename "$EMU")")
		if kill -0 "${ownr:-0}" 2>/dev/null; then
			echo "(note: emulator $old belongs to a live run $ownr"\
			     "-- another lane; leaving it alone)"
		else
			echo "(reclaiming emulator $old: its harness $ownr is gone)"
			kill -9 "$old" 2>/dev/null
			rm -f "$f"
		fi;;
	*)	rm -f "$f";;		# gone, or the pid is somebody else's now
	esac
done
PIDF=$PIDD/$$.pid

# FLOPPY=<image> attaches a medium in the drive, reachable in the guest as
# /dev/fd1.  A floppy dist (the install disk) has a filesystem no hard-disk
# boot can otherwise reach, so without this the guest's own tools -- icheck,
# dcheck, ncheck, mount -- can never be pointed at one.  The path is taken as
# given: unlike $IMG it is NOT copied, so hand it a copy when the run may write.
#
# A BOOTABLE FLOPPY TAKES THE MACHINE.  The stock ROM's autoboot spec tries
# `(fd,1)coherent' before `(hd)coherent', so a medium carrying a loader at
# /coherent -- the install disk does -- is what boots, and the system under the
# transcript is the floppy's.  The kernel line says which: `coherent.sys' on
# /dev/fd1 is the floppy, `coherent' the hard disk.
FLOPPYARG=''
[ -n "${FLOPPY:-}" ] && FLOPPYARG="--floppy=$FLOPPY"
# Every medium the machine is given, on the record before it runs.  A harness
# whose claim is about the IMAGE -- test/selfhost -- has to be able to show that
# nothing else was attached, and the absence of a variable it did not set is not
# a showing; a line naming each medium is.
echo "=== emulator $EMU"
echo "=== medium disk $IMG"
[ -n "${FLOPPY:-}" ] && echo "=== medium floppy $FLOPPY"

# --stop-mark ends the run at the moment the guest announces it is finished,
# which is the same $MARK the script echoes last.  park (halted, woken only
# by the tick) and idle (quiet at a prompt, input spent) catch a hang, so no
# wall clock is needed.  --require-stop makes the exit status say whether a
# channel fired.
#
# Past a GATE or RAW the guest waits out console quiet while halted, which
# park can't tell from a hang, so such a script stops on mark alone.
#
# A script's `#% stop <list>' overrides that (park counts host loops, so a
# long guest sleep wants mark), STOPON overrides both, and EMUARGS appends
# raw emulator options such as --break, --dump, --max.
stopon=mark
[ -n "$raw_seen" ] || stopon=mark,park,idle
stopon=${STOPON:-${stopdir:-$stopon}}
( cd "$(dirname "$EMU")" && exec "$EMU" --disk "$IMG" $FLOPPYARG ${EMUARGS:-} \
	--stop-on="$stopon" --stop-mark "$MARK" --require-stop \
	--input-mark "$LOGINMARK" --input "$script" >>"$OUT" 2>>"$ERR" ) &
pid=$!
echo "$$ $pid" > "$PIDF"
# Stops the emulator on a signal to us.
trap 'kill -9 $pid 2>/dev/null; rm -f "$PIDF"' EXIT
trap 'kill -9 $pid 2>/dev/null; rm -f "$PIDF"; exit 130' INT
trap 'kill -9 $pid 2>/dev/null; rm -f "$PIDF"; exit 143' TERM

t0=$(date +%s)
wait $pid
estatus=$?
t=$(( $(date +%s) - t0 ))
rm -f "$PIDF"

# Which channel ended the run, from the emulator's closing line
# "[c900: stopped after N instructions -- REASON]".  run.sh's judge reads it.
reason=$(sed -n 's/^\[c900: stopped after [0-9]* instructions -- \(.*\)\]$/\1/p' "$ERR" | tail -1)
case "$reason" in
*"printed the stop mark"*)		chan=mark ;;
*"guest is parked"*)			chan=park ;;
*"scripted input consumed"*)		chan=idle ;;
*"rang the stop doorbell"*)		chan=port ;;
*"breakpoint"*)				chan=break ;;
*"halted with no interrupt pending"*)	chan=halt ;;
*"instruction budget"*)		chan=budget ;;
*"Ctrl-]"*)				chan=manual ;;
'')	if [ "$estatus" = 3 ]; then chan=none; else chan=unknown; fi ;;
*)	chan=other ;;
esac
echo "=== stop channel: $chan${reason:+ ($reason)}"

# A run that never reached the mark did not finish, and saying so in a warning
# while exiting 0 makes a truncated run read as a pass -- which is how a guest
# stopped at its own sleep was reported as a clean switchboard sweep.  The
# transcript is still printed: a short one is the evidence for the failure.
unfinished=0
grep -q "$MARK" "$OUT" || { unfinished=1; echo "(the guest never reached $MARK -- stopped by channel: $chan)"; }
echo "=== console transcript ($t s) ==="
cat "$OUT"
echo "=== read files the commands wrote with:"
echo "    $(sh "$C900_ROOT/mk/deps.sh" tools)/bin/cohfs cat $IMG /<file>"
# The per-run names carry the pid, so they have to be printed rather than
# remembered.
echo "=== this run: transcript $OUT, stderr $ERR, image $IMG"
exit $unfinished
