#!/bin/sh
# staleaddr-gate.sh -- the mutation case for udp-two-test.py's reconfigure step.
#
# WHAT IT IS FOR.  udp-two-test.py renumbers both guests on a running stack and
# then requires a UDP exchange to complete.  That assertion is only worth having
# if it can FAIL, and the thing it is watching for -- inet keeping a copy of the
# interface address -- cannot be produced from the harness side: it is a property
# of the stack's source.  So this gate puts the defect back, one line, and
# requires the test to refuse the resulting system.  A run that has never been
# seen to fail proves nothing; a check that cannot fail must not survive.
#
#	sh staleaddr-gate.sh		mutate, expect FAIL, restore, expect PASS
#	sh staleaddr-gate.sh mutant	the mutated half alone
#	sh staleaddr-gate.sh clean	the unmutated half alone
#
# Variables: EMU (see twohost.py).  Takes about an hour; do not run it
# alongside another build of the net package.
#
# THE MUTATION is in ip_get_ifaddr(), and it is one place rather than one per
# consumer on purpose.  Every consumer of the interface address now goes through
# that function; making IT answer with the address it saw first re-creates
# exactly the original behavior -- "read once, never again" -- for all of them
# at the same time.  Mutating a single consumer instead would leave the gate
# passing for the wrong reason if a later change moved the defect to another one.
#
# The source is edited in the tree and restored on the way out, including on a
# signal: there is no way to build a mutant stack without building the stack, and
# the net package carries net/inet/inet from the tree.  The restore is checked, and the
# gate refuses to report anything if the tree it leaves behind is not the tree it
# started with.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
IMG=${TMPDIR:-/tmp}/staleaddr-gate.$$.bin
SRC=$OS/net/inet/generic/ip_lib.c
BACKUP=${TMPDIR:-/tmp}/staleaddr-gate.$$.ip_lib.c
MODE=${1:-both}
fail=0

restore() {
	if [ -f "$BACKUP" ]; then
		cp "$BACKUP" "$SRC" && rm -f "$BACKUP"
		echo "== restored $SRC"
	fi
}
trap 'restore; exit 130' INT TERM
trap 'restore; rm -f "$IMG" "$IMG.stamp"' EXIT

# The mutant: ip_get_ifaddr answers with the address it saw the first time.
mutate() {
	cp "$SRC" "$BACKUP"
	python3 - "$SRC" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = """	assert(port_nr >= 0 && port_nr < ip_conf_nr);

	return ip_port_table[port_nr].ip_ipaddr;"""
new = """	static ipaddr_t mutant_first;

	assert(port_nr >= 0 && port_nr < ip_conf_nr);

	if (!mutant_first)
		mutant_first= ip_port_table[port_nr].ip_ipaddr;
	return mutant_first;"""
if s.count(old) != 1:
    sys.exit("staleaddr-gate: ip_get_ifaddr does not look as expected -- "
             "the mutation must be re-aimed before this gate means anything")
open(p, "w").write(s.replace(old, new))
EOF
}

build() {			# build <what it is>
	echo "== building the $1 stack and a test image of it"
	rm -f "$OS/net/inet/generic/ip_lib.o" "$OS/net/inet/inet"
	sh "$OS/hostbuild/build-net.sh" || return 1
	sh "$OS/dist/pack-component.sh" net bin || return 1
	sh "$OS/test/image/build.sh" "$IMG" || return 1
}

# check <want: pass|fail> <what it is>
check() {
	( cd "$HERE" && python3 udp-two-test.py --image="$IMG" )
	st=$?
	got=pass; [ $st -eq 0 ] || got=fail
	if [ "$got" != "$1" ]; then
		echo "FAIL gate: the $2 stack $got""ed udp-two-test.py, wanted $1"
		fail=1
	else
		echo "ok: the $2 stack $got""ed udp-two-test.py, as it must"
	fi
}

case $MODE in
mutant|both)
	mutate || exit 1
	build mutant || { echo "FAIL gate: the mutant did not build"; exit 1; }
	check fail "mutant (address read once)"
	restore
	;;
esac

case $MODE in
clean|both)
	build "as-committed" || { echo "FAIL gate: the tree did not build"; exit 1; }
	check pass "as-committed (address read live)"
	;;
esac

[ $fail -eq 0 ] && echo "== staleaddr gate: PASS" || echo "== staleaddr gate: FAIL"
exit $fail
