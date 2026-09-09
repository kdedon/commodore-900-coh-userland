#!/bin/sh
# tests/moreterm/run.sh -- more(1) must page a file under EVERY terminal type,
# including the ones whose termcap entry addresses the cursor with `cm' and has
# no `ho'.
#
#	sh run.sh			all six phases
#	MORE=/path/to/more sh run.sh	test a different build
#	MUTATE=nodecl|noso|pipestdin sh run.sh   see an assertion fail (below)
#
# WHY THE TERMINAL TYPE IS THE VARIABLE.  more's initterm() reads the home-cursor
# string `ho', and when the entry has not got one it builds a home sequence from
# the cursor-motion string instead:
#
#	Home = tgetstr("ho", &clearptr);
#	if (Home == 0 || *Home == '\0')
#	    if ((cursorm = tgetstr("cm", &clearptr)) != NULL) {
#		strcpy(cursorhome, tgoto(cursorm, 0, 0));
#
# That branch is taken for `vt100', `vt52' and `mgr' and not for `ansi', `hr',
# `lr' or `dumb', so a defect on it is invisible to any single-TERM test -- and
# `vt100' is what /.profile sets for every serial console, i.e. the default.
# Each phase below is therefore one group of entries chosen for the SHAPE of
# its capabilities, not for the terminal it names.
#
# WHAT more HAS TO DO TO PASS: print the whole of a two-line file and exit 0.
# That is deliberately the weakest demand that cannot be met by accident -- a
# more that dies in initterm() prints nothing at all.
#
# THE TERMINAL IS REAL.  initterm() reads the termcap only when stdout is a
# terminal (`gtty(fileno(stdout))'), so a run whose output is a pipe takes the
# `dumb' path for every TERM and reports four phases of success against a
# defect that is still there.  Each case is run on a pty for that reason.
#
# MUTATE=nodecl demonstrates that the gate can fail: it compiles more.c with the
# `char *tgoto();' declaration removed -- K&R then takes tgoto for a function
# returning int, which drops the segment of the far pointer it returns -- and
# runs the same cases against that.  Phases 1 and 4 go red, and so do phase 5's
# vt100, vt52 and mgr -- the same three entries, because they are the ones whose
# home string comes from tgoto.  Phases 2 and 3 and the rest of phase 5 stay
# green, which is the correlation itself.  The mutant does NOT fault under
# the process runner, which has no MMU and no signals: it spins instead, and the
# case fails because the file never appears.  On the machine the same binary
# answers `Segmentation violation -- core dumped'.
#
# MUTATE=pipestdin is phase 6's control: it compiles more.c with readch()
# reading fileno(stdin) again instead of descriptor 2.  Phase 6 goes red and
# phases 1-5 stay green, because every one of them hands more a FILE with the
# terminal still on stdin, where the wrong descriptor is the right one.
#
# MUTATE=noso does the same for phase 5, from the other side: it hands the guest
# an `lr' entry with `so'/`se' deleted while the expectation is still read from
# the entry as this file writes it, so more paints its prompt plain and the lr
# case goes red.  The other five terminals stay green -- an entry is damaged,
# not the program.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
HB="$OS/hostbuild"

MORE=${MORE:-$HB/build/bin/more}
MUTATE=${MUTATE:-none}

C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"
emu_need "run more(1), which is a Z8001 binary"

case $MUTATE in
none|nodecl|noso|pipestdin) ;;
*) echo "moreterm: unknown MUTATE=$MUTATE" >&2; exit 2;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/moret.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15

# ---------------------------------------------------------------------------
# The terminals.  Copied from the /etc/termcap this project ships (the dist
# repository's dist/files/etc/termcap), one entry per terminal the machine
# can actually be sitting at, so that what is under test is the capability
# shape rather than whichever file happens to be installed on the host.
# ---------------------------------------------------------------------------
cat > "$WORK/termcap" <<'EOF'
su|dumb|un|unknown:co#80:os:am:

dv|vt|vt52|dec vt52:\
	:bs:cd=\EJ:ce=\EK:cl=\EH\EJ:cm=\EY%+ %+ :co#80:li#24:nd=\EC:\
	:pt:sr=\EI:up=\EA:ku=\EA:kd=\EB:kr=\EC:kl=\ED:

d1|vt100|vt-100|pt100|pt-100|dec vt100:\
	:co#80:li#24:am:cl=50\E[;H\E[2J:bs:cm=5\E[%i%2;%2H:nd=2\E[C:up=2\E[A:\
	:ce=3\E[K:cd=50\E[J:so=2\E[7m:se=2\E[m:us=2\E[4m:ue=2\E[m:\
	:is=\E>\E[?3l\E[?4l\E[?5l\E[?7h\E[?8h:ks=\E[?1h\E=:ke=\E[?1l\E>:\
	:if=/usr/lib/tabset/vt100:ku=\EOA:kd=\EOB:kr=\EOC:kl=\EOD:\
	:kh=\E[H:k1=\EOP:k2=\EOQ:k3=\EOR:k4=\EOS:pt:sr=5\EM:

d0|vt100n|vt100 w/no init:is@:if@:tc=vt100:

dt|vt100w|vt-100w|pt100w|pt-100w|dec vt100 132 cols:\
	:co#128:li#24:is=\E>\E[?3h\E[?4l\E[?5l\E[?7h\E[?8h:tc=vt100:

ansi|ansi/pc-term compatible with color:\
	:am:bs:mi:ms:\
	:co#80:it#8:li#24:\
	:al=\E[L:bl=^G:cd=\E[J:ce=\E[K:cl=\E[H\E[J:cm=\E[%i%d;%dH:\
	:cr=^M:dc=\E[P:dl=\E[M:do=\E[B:ho=\E[H:kb=^H:le=^H:\
	:nd=\E[C:up=\E[A:so=\E[7m:se=\E[m:us=\E[4m:ue=\E[m:

hr|c900hr|Commodore 900 hi-res console:\
	:co#85:li#32:bs:\
	:cl=\E[E:ce=\E[K:cd=\E[J:cm=\E[%i%d;%dH:ho=\E[H:\
	:up=\E[A:do=\E[B:nd=\E[C:le=\E[D:\
	:al=\E[L:dl=\E[M:ic=\E[@:dc=\E[P:

lr|c900lr|Commodore 900 lo-res console:\
	:am:bs:mi:ms:co#80:li#24:\
	:al=\EL:cd=\EJ:ce=\EK:cl=\EE:cm=\EY%+ %+ :dc=\EN:dl=\EM:ei=\EO:\
	:ho=\EH:im=\E@:nd=\EC:se=\Eq:so=\Ep:sr=\EI:ue=\Ei:up=\EA:us=\Eh:\
	:kb=^H:kd=\EB:kl=\ED:kr=\EC:ku=\EA:

px|mgr|General Bellcore window manager teminal emulation:\
	:am:bs:li#24:co#80:\
	:al=\Ea:AL=\E%da:cd=\EC:ce=\Ec:cl=^L:cm=\E%r%d,%dM:cs=\E%d,%dt:\
	:dc=\EE:dl=\Ed:DL=\E%dd:do=\Ef:ic=\EA:md=\E2n:me=\E0n:mr=\E1n:\
	:nd=\Er:se=\E0n:so=\E1n:ta=^I:ue=\E0n:up=\Eu:us=\E4n:ve=\Ev:vs=\EV:
EOF

# Two copies.  The guest reads $WORK/termcap, which MUTATE may damage; every
# expectation is computed from $WORK/termcap.ref, which nothing damages.  A
# gate whose expectation moved with its input would pass any mutation at all.
cp "$WORK/termcap" "$WORK/termcap.ref"

if [ "$MUTATE" = noso ]; then
	sed 's/:se=\\Eq:so=\\Ep:/:/' "$WORK/termcap.ref" > "$WORK/tc.m"
	if cmp -s "$WORK/tc.m" "$WORK/termcap.ref"; then
		echo "moreterm: MUTATE=noso removed nothing -- lr's so/se are no" >&2
		echo "  longer spelt that way in the termcap above." >&2
		exit 2
	fi
	mv "$WORK/tc.m" "$WORK/termcap"
fi

printf 'ONE\nTWO\n' > "$WORK/f"

# Phase 5 needs a file longer than any entry's `li' so that more has to stop
# and prompt; 60 lines clears the tallest entry here (hr, 32).
awk 'BEGIN{for(i=1;i<=60;i++) print i}' < /dev/null > "$WORK/long"

# The pty. pty.fork() gives the guest a terminal on fd 0/1/2, which is the
# whole point; raw mode on the slave keeps the line discipline from echoing the
# escape sequences more writes back into more's own input.
# $2 is what to type once the guest has asked for it: an empty string for the
# phases that only page a short file, and ` q' for phase 5, where more must
# reach its `--More--' prompt before the escape sequences under test exist.
# The keys go in when the prompt appears (or, if it never does, once, late, so
# a program that is not going to prompt still terminates on its own).
cat > "$WORK/ptyrun.py" <<'EOF'
import os,pty,sys,select,time,signal,tty
lim=float(sys.argv[1]); keys=sys.argv[2].encode(); argv=sys.argv[3:]
pid,fd=pty.fork()
if pid==0:
    tty.setraw(0)
    os.execvp(argv[0],argv)
out=b''; t0=time.time(); sent=not keys; gone=False
while time.time()-t0 < lim:
    r,_,_=select.select([fd],[],[],0.3)
    if r:
        try: d=os.read(fd,4096)
        except OSError: break
        if not d: break
        out+=d
        continue
    if not sent and (b'--More--' in out or time.time()-t0 > lim/3):
        time.sleep(0.5)
        for k in keys:
            os.write(fd, bytes([k])); time.sleep(0.3)
        sent=True
    # The last write the guest makes is often its last act, so a reap that
    # broke the loop would drop it: the exit is noted and the loop runs on
    # until select stops offering, which is when the pty has drained.
    if gone: break
    if os.waitpid(pid,os.WNOHANG)[0]==pid: gone=True
try:
    os.kill(pid,signal.SIGKILL); os.waitpid(pid,0)
except OSError: pass
sys.stdout.buffer.write(out)
EOF

# ---------------------------------------------------------------------------
# The binary under test.  MUTATE=nodecl rebuilds more.c from the same sources
# the sweep uses, minus the one declaration, so the mutant differs from the
# shipped program in exactly the thing this file is about.
# ---------------------------------------------------------------------------
if [ "$MUTATE" = nodecl ] || [ "$MUTATE" = pipestdin ]; then
	. "$OS/hostbuild/toolchain.sh"
	LIBTERM="$TCB/curses/libterm.a"
	[ -f "$LIBTERM" ] || sh "$HB/build-curses.sh" >/dev/null 2>&1
	if [ "$MUTATE" = nodecl ]; then
		grep -v '^char		\*tgoto();$' "$OS/base/cmd/more/more.c" \
			> "$WORK/more.c"
		_what="the declaration"
	else
		sed 's/^	if (read (2, &ch, 1) <= 0)$/	if (read (fileno(stdin), \&ch, 1) <= 0)/' \
			"$OS/base/cmd/more/more.c" > "$WORK/more.c"
		_what="readch's descriptor"
	fi
	if cmp -s "$WORK/more.c" "$OS/base/cmd/more/more.c"; then
		echo "moreterm: MUTATE=$MUTATE changed nothing -- $_what" >&2
		echo "  it looks for is no longer spelt that way in more.c." >&2
		exit 2
	fi
	CCZ_VAR=800000020800 "$TC/ccz" -s -i \
		-I "$OS/include" -I "$OS/include/sys" -I "$OS/base/cmd" \
		-DCOHERENT -I "$OS/base/cmd/more" -I "$OS/base/lib/regexp" \
		-o "$WORK/more" "$WORK/more.c" "$OS/base/lib/regexp/regexp.c" \
		"$LIBTERM" > "$WORK/cc.log" 2>&1 || {
		echo "moreterm: the mutant did not compile; see $WORK/cc.log" >&2
		exit 2; }
	MORE=$WORK/more
fi
[ -f "$MORE" ] || {
	echo "moreterm: no more at $MORE -- sh $HB/build-extra-userland.sh" >&2
	exit 2; }

PASSED=0
BAD=0

# runmore <term> -- print `ok' when more paged the whole file and exited 0.
# The limit is generous because the process runner interprets every
# instruction; a more that is going to answer at all answers well inside it.
runmore() {
	N2ENV="TERM=$1:TERMCAP=$WORK/termcap" \
		python3 "$WORK/ptyrun.py" 30 "" "$C900_EMU" --exec "$MORE" "$WORK/f" \
		> "$WORK/o" 2>&1
	if grep -q ONE "$WORK/o" && grep -q TWO "$WORK/o" &&
	   grep -q 'exit 0' "$WORK/o"; then
		echo ok
	else
		echo no
	fi
}

runcases() {
	for t in $2; do
		if [ "$(runmore "$t")" = ok ]; then
			echo "  ok   $1 TERM=$t"
		else
			echo "  FAIL $1 TERM=$t: the file was not paged"
		fi
	done
}

tally() {
	cat "$1"
	_o=$(grep -c '^  ok   ' "$1" 2>/dev/null); _o=${_o:-0}
	_f=$(grep -c '^  FAIL ' "$1" 2>/dev/null); _f=${_f:-0}
	PASSED=$((PASSED + _o))
	BAD=$((BAD + _f))
}

# The instrument before the measurement.  `dumb' has no `cm', so more never
# reaches the branch under test and no build of it can fail this one.  If even
# this case cannot page a file then the pty, the emulator or the termcap is
# what is broken, and the phases below would report that as four defects in
# more.
if [ "$(runmore dumb)" != ok ] && [ "$MUTATE" = none ]; then
	echo "moreterm: cannot page a file under TERM=dumb, which reads no" >&2
	echo "  termcap at all.  Nothing below would measure more.  $WORK" >&2
	exit 2
fi

echo "moreterm: $MORE"
[ "$MUTATE" = none ] || echo "  (MUTATE=$MUTATE -- this run is EXPECTED to fail)"

echo "phase 1: entries with cm and NO ho -- the home string comes from tgoto"
runcases nohome "vt100 vt52 mgr" > "$WORK/p1"; tally "$WORK/p1"

echo "phase 2: entries with ho -- the home string is read straight out"
runcases home "ansi hr lr" > "$WORK/p2"; tally "$WORK/p2"

echo "phase 3: no usable entry at all -- more's own dumb path"
runcases dumb "dumb nosuchterm" > "$WORK/p3"; tally "$WORK/p3"

echo "phase 4: entries that reach vt100 through tc="
runcases viatc "vt100n vt100w" > "$WORK/p4"; tally "$WORK/p4"

# ---------------------------------------------------------------------------
# Phase 5.  Phases 1-4 ask only whether more survived the entry.  This one asks
# whether it USED it: the bytes more puts on the wire around its `--More--'
# prompt must be the entry's own `so', `se' and `ce' strings and no others.
#
# The prompt is the right place to look because it is the one piece of screen
# painting more does on a file of any length, and because all three capabilities
# meet there:  so  --More--(nn%)  se  CR  ce.  Under `lr' that is ESC p / ESC q
# / ESC K, under `vt100' ESC [ 7 m / ESC [ m / ESC [ K, under `mgr' ESC 1 n /
# ESC 0 n / ESC c, and under `hr', whose entry has no standout at all, the
# prompt must arrive with NO escape in front of it.  That is what makes this
# gate about the FILE and not about more: change an entry and this phase changes
# with it, because the expectation is read out of the entry.
#
# WHAT IT STILL DOES NOT SAY.  Nothing about how the screen looks.  A sequence
# can be correct and the display still wrong -- that judgement needs the
# simulator and a person, and no assertion here pretends to make it.
#
# NULs are deleted from the capture before matching.  They are tputs(3) padding,
# emitted BETWEEN the capability and the text it wraps because `so' carries a
# delay count (`so=2\E[7m'); the padding is a property of the terminal's speed,
# not of the entry's string, and matching around it is what is wanted.
# ---------------------------------------------------------------------------
cat > "$WORK/promptcap.py" <<'EOF'
import re, sys

def parse(path):
    ents, cur = {}, []
    for line in open(path, errors='replace'):
        s = line.rstrip('\n')
        if not s.strip() or s.lstrip().startswith('#'):
            continue
        cur.append(s)
        if not s.endswith('\\'):
            joined = ''.join(l[:-1] if l.endswith('\\') else l for l in cur)
            parts = joined.split(':')
            fields = [f.strip() for f in parts[1:] if f.strip()]
            for n in parts[0].split('|'):
                ents.setdefault(n, fields)
            cur = []
    return ents

def resolve(ents, name, seen=None):
    """libterm's rule: the entry's own fields first, the tc= parent appended
    after them, first hit wins.  tc= counts only as the last field."""
    seen = seen or set()
    fields = ents[name]
    d = {}
    for f in fields:
        m = re.match(r'([A-Za-z0-9]{2})=(.*)$', f)
        if m:
            d.setdefault(m.group(1), m.group(2))
    if fields and fields[-1].startswith('tc='):
        p = fields[-1][3:]
        if p in ents and p not in seen:
            seen.add(p)
            for k, v in resolve(ents, p, seen).items():
                d.setdefault(k, v)
    return d

def expand(s):
    """A capability string as tputs(3) will put it on the wire: the leading
    delay count and any trailing `*' are tputs' business, not the terminal's."""
    if s is None:
        return None
    s = re.sub(r'^[0-9]+(\.[0-9]+)?\*?', '', s)
    out, i = b'', 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out += {'E': b'\033', 'n': b'\n', 'r': b'\r', 't': b'\t',
                    'b': b'\b', 'f': b'\f', '\\': b'\\'}.get(n, n.encode())
            i += 2
        elif c == '^' and i + 1 < len(s):
            out += bytes([ord(s[i + 1].upper()) ^ 0x40])
            i += 2
        else:
            out += c.encode()
            i += 1
    return out

ents = parse(sys.argv[1])
term = sys.argv[2]
cap = open(sys.argv[3], 'rb').read().replace(b'\0', b'')
c = resolve(ents, term)
so, se, ce = expand(c.get('so')), expand(c.get('se')), expand(c.get('ce'))

i = cap.find(b'--More--')
if i < 0:
    print("more never prompted, so it emitted nothing to judge")
    sys.exit(1)
pre, post = cap[:i], cap[i + len(b'--More--'):]
bad = []
if so:
    if not pre.endswith(so):
        bad.append("so: expected %r before the prompt, got %r"
                   % (so, pre[-len(so) - 4:]))
elif b'\033' in pre[-8:]:
    bad.append("so: the entry has none, but %r came before the prompt"
               % pre[-8:])
if se and se not in post[:40]:
    bad.append("se: expected %r after the prompt, got %r" % (se, post[:40]))
if ce and ce not in post[:80]:
    bad.append("ce: expected %r to erase the prompt, got %r" % (ce, post[:80]))
if bad:
    print("; ".join(bad))
    sys.exit(1)
print("so=%r se=%r ce=%r" % (so, se, ce))
EOF

promptcase() {
	N2ENV="TERM=$1:TERMCAP=$WORK/termcap" \
		python3 "$WORK/ptyrun.py" 45 " q" \
		"$C900_EMU" --exec "$MORE" "$WORK/long" > "$WORK/po" 2>&1
	# printf, not echo: the report quotes escape sequences, and sh's echo
	# would expand the backslashes in them into the characters they name.
	if _w=$(python3 "$WORK/promptcap.py" "$WORK/termcap.ref" "$1" "$WORK/po"); then
		printf '  ok   prompt TERM=%s: %s\n' "$1" "$_w"
	else
		printf '  FAIL prompt TERM=%s: %s\n' "$1" "$_w"
	fi
}

echo "phase 5: the prompt is painted with the entry's own so/se/ce"
for t in vt100 vt52 lr hr mgr ansi; do promptcase "$t"; done > "$WORK/p5"
tally "$WORK/p5"

# ---------------------------------------------------------------------------
# Phase 6.  `ls | more': stdin is the PIPE being paged, so the answer to
# --More-- cannot be read from there -- the next byte would be the text.  more
# takes its commands from descriptor 2, the terminal it already saved with
# gtty() and put in CBREAK with stty().
#
# Phases 1-5 all hand more a FILE and leave stdin on the terminal, so none of
# them can see this: the wrong descriptor is the right one there.  The case
# needs a pipe on fd 0 and a terminal on fd 1 and 2 at once, which is why it
# has a runner of its own rather than another TERM in the loop above.
#
# WHAT IT ASSERTS: 60 lines go in, more stops inside the first screenful, and
# the rest arrives only after a SPACE is typed at the terminal.  The last line
# is the evidence -- a more that never waited would have printed it unprompted,
# and one that read its command from the pipe would have eaten text as keys.
# ---------------------------------------------------------------------------
cat > "$WORK/piperun.py" <<'EOF'
import os,pty,sys,select,time,signal,tty
lim=float(sys.argv[1]); argv=sys.argv[2:]
data=b''.join(b'L%d\n'%i for i in range(1,61))
pid,fd=pty.fork()
if pid==0:
    tty.setraw(0)
    r,w=os.pipe()
    if os.fork()==0:
        os.close(r); os.write(w,data); os.close(w); os._exit(0)
    os.close(w); os.dup2(r,0); os.close(r)
    os.execvp(argv[0],argv)
# Nothing is typed until --More-- has been seen, so `L60' arriving before the
# first key is proof more never stopped -- not a race with a slow guest.
out=b''; t0=time.time(); sent=0; gone=False; before=b''
while time.time()-t0 < lim:
    r,_,_=select.select([fd],[],[],0.3)
    if r:
        try: d=os.read(fd,4096)
        except OSError: break
        if not d: break
        out+=d; continue
    if b'--More--' in out and sent < 4:
        if not sent: before=out
        time.sleep(0.4); os.write(fd,b' '); sent+=1
    if gone: break
    if os.waitpid(pid,os.WNOHANG)[0]==pid: gone=True
try:
    os.kill(pid,signal.SIGKILL); os.waitpid(pid,0)
except OSError: pass
sys.stdout.buffer.write(b'BEFORE\n'+before+b'\nAFTER\n'+out)
EOF

pipecase() {
	N2ENV="TERM=$1:TERMCAP=$WORK/termcap" \
		python3 "$WORK/piperun.py" 60 "$C900_EMU" --exec "$MORE" \
		> "$WORK/pi" 2>&1
	_b=$(sed -n '/^BEFORE$/,/^AFTER$/p' "$WORK/pi")
	if ! grep -q -- '--More--' "$WORK/pi"; then
		echo "  FAIL pipe TERM=$1: more never prompted at all"
	elif echo "$_b" | grep -q '^L60$'; then
		echo "  FAIL pipe TERM=$1: the whole file arrived before any key"
	elif grep -q '^L60$' "$WORK/pi"; then
		echo "  ok   pipe TERM=$1: stopped, then finished on SPACE"
	else
		echo "  FAIL pipe TERM=$1: never reached the end of the pipe"
	fi
}

echo "phase 6: with a pipe on stdin the prompt is answered from the terminal"
for t in vt100 mgr; do pipecase "$t"; done > "$WORK/p6"
tally "$WORK/p6"

echo
if [ "$BAD" -eq 0 ]; then
	echo "moreterm: $PASSED ok"
	exit 0
fi
echo "moreterm: $BAD FAILED, $PASSED ok"
exit 1
