#!/bin/sh
# sim-boot-login.sh -- cold-boot the c900sim with the current build/c900coh.bin
# and drive it to a logged-in root shell on the serial console.  Encodes the
# whole recipe: kill+relaunch the sim (HTTP reset can't cold-boot), insert the
# image, free-run, pick "1) Coherent 3.5" at the loader menu, Ctrl-D through
# single-user, and log in as root.
#
# The login name is terminated with CR (not LF) ON PURPOSE: getty auto-detects
# the terminal's line ending from that character, and a CR turns CRMOD on for
# the session -- afterwards both a hand-typed Enter (CR) and script-sent LF
# terminate lines.  An LF-terminated login leaves CRMOD off and a human's
# Enter key then never ends a line (looks like a hung shell).
#
# Console mapping (headless): kernel output = SCC ch A, keyboard input = ch B.
# Requires: c900sim built at $SIM, image built (build-image.sh), curl+python3.
#
# DISPLAY MUST BE PASSED EXPLICITLY.  `c900sim -display' takes headless|lr|hr,
# and with the flag OMITTED it reads display.board from a PERSISTED ui_state.json
# -- so whatever a previous run selected silently carries over.  That matters now
# that the console is chosen at boot by probing for a video board (md.s vidsel):
# with a board present, init loads /drv/lrtty or /drv/hrtty (this driver was
# called /drv/kv historically) and the shell talks to the VIDEO
# console, this serial harness sees nothing after the kernel banner, and the boot
# looks hung when it is in fact fine.  This script is the SERIAL harness, so it
# pins headless; use DISPLAY=hr (or lr) to drive the video console instead.
set -u
S=${S:-http://localhost:7810}
DISPLAY_BOARD=${DISPLAY:-headless}
HERE=$(cd "$(dirname "$0")" && pwd)
# The simulator has no default path: see mk/simulator.sh for what it is, why
# there is nothing to search for, and what to use instead when you have none.
C900_ROOT=$(cd "$HERE/.." && pwd)
. "$C900_ROOT/mk/simulator.sh"
. "$C900_ROOT/mk/dist.sh"
sim_need "cold-boot an image and log in on its console"
IMG=${IMG:-"$HERE/build/c900coh.bin"}
# Boot a working copy; the build artifact stays as `make dist' left it.  See
# workimg.sh, and set PERSIST=1 there to keep the guest's writes instead.
dist_need "boot the target on a scratch copy"
IMG=$("$C900_WORKIMG" "$IMG" "${S##*:}") || exit 1
J='Content-Type: application/json'
# Per-run, not a fixed name -- see sim-run.sh: a second run truncating a
# shared transcript file is how one lane ends up reporting another's console.
CH=${CH:-${TMPDIR:-/tmp}/sim-boot-acc.$$.txt}; : > $CH
CTRLD=${TMPDIR:-/tmp}/sim-boot-ctrld.$$.json
CRF=${TMPDIR:-/tmp}/sim-boot-cr.$$.json
send() { curl -s -X POST $S/serial/send -H "$J" -d "$1" >/dev/null; }
drain() { curl -s $S/serial/recv-all | python3 -c "import json,sys; sys.stdout.write(json.load(sys.stdin).get('ch_a',''))" >> $CH; }
waitfor() { # $1=grep-pattern $2=timeout-sec
	t=0; while [ $t -lt $2 ]; do sleep 3; t=$((t+3)); drain; grep -q "$1" $CH && return 0; done; return 1
}
. "$HERE/simctl.sh"
sim_start "$SIM" "$S" "$DISPLAY_BOARD" || exit 1
curl -s -X POST $S/disk/hd/0/insert -H "$J" -d "{\"path\":\"$IMG\"}" >/dev/null
curl -s -X POST $S/exec/run >/dev/null
# kboot auto-boots the sole OS (no menu); wait straight for single-user.
waitfor '# ' 300 || { echo FAIL-singleuser; tail -c 300 $CH; exit 1; }
echo SINGLE-USER-OK; sleep 2
printf '{"channel":1,"text":"\\u0004"}' > $CTRLD
curl -s -X POST $S/serial/send -H "$J" -d @$CTRLD >/dev/null; rm -f $CTRLD
waitfor 'login:' 300 || { echo FAIL-login-prompt; tail -c 300 $CH; exit 1; }
echo LOGIN-PROMPT-OK; sleep 2
for c in r o o t; do send "{\"channel\":1,\"text\":\"$c\"}"; sleep 1; done
printf '{"channel":1,"text":"\\r"}' > $CRF
curl -s -X POST $S/serial/send -H "$J" -d @$CRF >/dev/null; rm -f $CRF
waitfor 'toolchain' 120 || { echo FAIL-motd; tail -c 300 $CH; exit 1; }
sleep 4; drain
echo LOGGED-IN-OK
tail -c 400 $CH
