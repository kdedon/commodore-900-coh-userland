#!/bin/sh
# sim-run.sh <cmdfile> [dist] -- cold-boot a dist, log in as root on the serial
# console, run each line of <cmdfile> in the shell, and print everything the
# console said.  For checking real behaviour on the target instead of inferring
# it from source.
#
# Pins `-display headless' for the same reason sim-boot-login.sh does: the
# console is chosen by a boot-time probe, so an inherited video board would
# move the shell to the screen and this harness would see nothing.
#
# Commands are typed one character at a time with the same CR convention as the
# login (see sim-boot-login.sh): getty learns the line ending from the login
# name's terminator, so everything after it uses CR too.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CMDS=${1:?usage: sim-run.sh <cmdfile> [dist]}
DIST=${2:-coherent3-full-test}
S=${S:-http://localhost:7810}
# The simulator has no default path: see mk/simulator.sh for what it is, why
# there is nothing to search for, and what to use instead when you have none.
C900_ROOT=$(cd "$HERE/.." && pwd)
. "$C900_ROOT/mk/simulator.sh"
. "$C900_ROOT/mk/dist.sh"
sim_need "cold-boot a dist and drive its console"
# The image is the distribution repository's product, not this tree's: see
# mk/dist.sh.  dist_img refuses by name, and names the repository to pack it in.
IMG=$(dist_img "$DIST") || exit 2
J='Content-Type: application/json'
# Per-run, not a fixed name: concurrent runs are normal, and a shared name
# hands one run another's console transcript.  The path is printed at the end.
CH=${CH:-${TMPDIR:-/tmp}/sim-run-acc.$$.txt}; : > $CH
CTRLD=${TMPDIR:-/tmp}/sim-run-ctrld.$$.json

send() { curl -s -X POST $S/serial/send -H "$J" -d "$1" >/dev/null; }
drain() { curl -s $S/serial/recv-all | python3 -c "import json,sys; sys.stdout.write(json.load(sys.stdin).get('ch_a',''))" >> $CH; }
waitfor() { t=0; while [ $t -lt $2 ]; do sleep 3; t=$((t+3)); drain; grep -q "$1" $CH && return 0; done; return 1; }
typeline() { python3 "$HERE/simtype.py" "$S" "$1"; }
# One shell prompt per completed command; used to know a command has finished
# rather than assuming it fits in a fixed window.
prompts() { tr -cd '#' < $CH | wc -c; }

# State what is about to boot, before it boots: the stamp makes a result
# attributable to a commit.  A dirty-tree build says so in line 1.
. "$HERE/provenance.sh"
prov_header "image $DIST" "$IMG.stamp" || true
# Boot a working copy; the build artifact stays as `make dist' left it.  See
# workimg.sh, and set PERSIST=1 there to keep the guest's writes instead.
dist_need "boot the target on a scratch copy"
IMG=$("$C900_WORKIMG" "$IMG" "${S##*:}") || exit 1
. "$HERE/simctl.sh"
sim_start "$SIM" "$S" headless || exit 1
curl -s -X POST $S/disk/hd/0/insert -H "$J" -d "{\"path\":\"$IMG\"}" >/dev/null
curl -s -X POST $S/exec/run >/dev/null

waitfor '# ' 300 || { echo "FAIL: no single-user prompt"; tail -c 400 $CH; exit 1; }
sleep 2
printf '{"channel":1,"text":"\\u0004"}' > $CTRLD
curl -s -X POST $S/serial/send -H "$J" -d @$CTRLD >/dev/null; rm -f $CTRLD
waitfor 'login:' 300 || { echo "FAIL: no login prompt"; tail -c 400 $CH; exit 1; }
sleep 2
typeline root
waitfor 'toolchain' 120 || { echo "FAIL: no motd"; tail -c 400 $CH; exit 1; }
sleep 4

echo "=== logged in; running commands ==="
while IFS= read -r line; do
	case "$line" in ''|\#*) continue;; esac
	echo "--- $line"
	typeline "$line"
	# Wait for the shell to come back, don't guess how long the command
	# takes.  A fixed drain window lies: exec reads every byte of a binary
	# through the emulated HDC, so a large program can still be loading
	# when the window closes, and "no output yet" reads exactly like "the
	# program printed nothing and died".  Drain until the prompt count
	# goes up, then move on.
	before=$(prompts)
	t=0
	while [ $t -lt ${CMDWAIT:-300} ]; do
		sleep 3; t=$((t+3)); drain
		[ "$(prompts)" -gt "$before" ] && break
	done
	[ $t -ge ${CMDWAIT:-300} ] && echo "(WARNING: no prompt after ${CMDWAIT:-300}s -- output may be truncated)"
done < "$CMDS"
sleep 4; drain

# Commit the guest's work so the result can be read from the image instead
# of trusted from the console.  `sync' flushes the guest's buffer cache to
# the virtual disk; POST /disk/sync then commits the sim's COW sectors to the
# backing file.  After this, hostbuild/fsread.py can read any file the test
# wrote, with no console timing in the path.
typeline sync
t=0
while [ $t -lt 60 ]; do sleep 3; t=$((t+3)); drain; done
curl -s -X POST $S/disk/sync -H "$J" -d '{}' >/dev/null
echo "=== flushed: read results with 'python3 fsread.py $IMG cat <path>' ==="

echo "=== console transcript ($CH) ==="
cat $CH
