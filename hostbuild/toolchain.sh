# toolchain.sh -- resolve the Z8001 cross toolchain.  SOURCE this file
# (`. "$OS/hostbuild/toolchain.sh"'); do not exec it.
#
# The compiler, assembler and linker are not in this repository.  They live in
# `commodore-900-toolchain', which has three consumers -- COHERENT, CP/M-8000
# and kboot -- and belongs to none of them; a compiler fix and a linker fix are
# indistinguishable to all three, so they version together, apart from any OS.
# This tree consumes one of two shapes of it.
#
#   $C900_TOOLCHAIN   the toolchain.  Unset, mk/deps.sh searches: the pinned
#                     release unpacked in deps/, then a checkout beside this
#                     repository (at most three parents out), then one inside a
#                     `repos/' directory beside it.  The search lives THERE and
#                     not here so that `make deps', this file and toolchain.mk
#                     cannot answer the question differently -- they used to
#                     hold three copies of it, and deps/ was in only one.
#
# Two shapes resolve, and they are not interchangeable:
#
#   checkout   a source clone.  Everything works, including the harnesses that
#              build curses, libm and libmisc from this tree's sources.
#   release    an unpacked release archive.  Its host/ view carries the same
#              paths over its own bin/, so nothing downstream spells a part
#              twice; but a binary archive has no build-*.sh and none of the
#              libraries they make, so it serves a KERNEL and plain userland,
#              and anything else fails at the command that wanted one.
#
# Which shape was used is PRINTED, once per build, with the variable and the
# path.  The line is cheap and the forensics help avoid misidentified toolchains
# between builds.
#
# Sets, for the caller:
#   $TC               the toolchain's host directory: ccz, the build-*.sh
#                     harnesses (checkout only), and build/ (cc0/cc1/cc2-z8001,
#                     as-z8001, ld-z8001, libc-z8001.a, curses, libm ...).
#   $TCB              the build directory to READ artifacts out of.
#   $C900_TC_SHAPE    `checkout' or `release X.Y.Z'.
#
#   $C900_TC_BUILD    where $TCB comes from, when set; $TC/build otherwise.
#                     It is the toolchain's own name for that directory too, so
#                     one setting moves both halves: where the toolchain's
#                     build-*.sh and arz EMIT, and where everything here READS.
#                     A lane whose compiler is genuinely its own sets this and
#                     nothing else.  It is exported for that reason -- a
#                     toolchain script run from here must land in the directory
#                     this tree reads.  Unset, the path is the shared one, and
#                     which is chosen is REPORTED for the same reason the shape
#                     is: a build's provenance must not be a matter of
#                     inspection.
#
# Exports $COHERENT_OS, which is the reciprocal input: the toolchain builds
# itself from its own sources alone, but a few of its harnesses build OS
# artifacts WITH it (libc-z8001.a, libm, ccz's default include path) and take an
# OS tree as an input.  See the toolchain's host/coherent-os.sh.
#
# $OS must already name the repository root -- a sourced file cannot portably find its
# own path, and that is how this one finds the repository root.  Checked
# against hostbuild, which is this file's own directory: every other
# top-level name here belongs to a component and a component can be absent,
# so a check against one of those would refuse builds that are fine.
if [ -z "${OS:-}" ] || [ ! -d "$OS/hostbuild" ]; then
	echo "toolchain.sh: \$OS must name the repository root before sourcing" >&2
	exit 2
fi
_c9root="$OS"
_c9deps="$_c9root/mk/deps.sh"
_c9tc=$(C900_TOOLCHAIN="${C900_TOOLCHAIN:-}" sh "$_c9deps" toolchain)
# The refusal is mk/deps.sh's own: it names the variable, says whether the
# variable's own value was the only thing tried, and lists every path it
# searched.  Without it the first symptom is `cc0-z8001: not found' from inside
# a sweep, several hundred lines into a build.
if [ -z "$_c9tc" ]; then
	C900_TOOLCHAIN="${C900_TOOLCHAIN:-}" sh "$_c9deps" -n toolchain "${C900_TOOLCHAIN:-}"
	exit 2
fi
C900_TOOLCHAIN=$(cd "$_c9tc" && pwd)
C900_TC_SHAPE=$(C900_TOOLCHAIN="$C900_TOOLCHAIN" sh "$_c9deps" -k toolchain)
TC="$C900_TOOLCHAIN/host"
TCB="${C900_TC_BUILD:-$TC/build}"
COHERENT_OS=$(cd "$OS" && pwd)

# $TCSYSINC: the toolchain's system headers -- the set ccz appends to every
# compile, resolved the way ccz resolves it: src/include in a checkout,
# usr/include in an unpacked release.  A script that needs to READ one of
# those headers rather than compile against it takes the path from here, so
# there is one answer to where they are.
if [ -d "$C900_TOOLCHAIN/native" ]; then
	TCSYSINC="$C900_TOOLCHAIN/usr/include"
else
	TCSYSINC="$C900_TOOLCHAIN/src/include"
fi

# $KINC: the KERNEL's header set, resolved through mk/deps.sh's `kernel' edge.
#
# The userland does not own the machine layer and must not keep a copy of it.
# <sys/machz8001.h> is the one that forces the issue: the toolchain's own
# <sys/machine.h> includes it under Z8001 -- which cc0-z8001 predefines, so
# that arm is always taken -- and it exists in neither the toolchain nor this
# tree.  Without $KINC on the include path, every command reaching
# <sys/machine.h> fails to compile.
#
# EMPTY when the kernel does not resolve, and that is deliberate: only the
# handful of commands that reach the machine layer need it, so a build of
# everything else must not be blocked by a missing kernel checkout.  The
# scripts that DO need it refuse by name, quoting deps.sh's own message,
# rather than compiling against whatever else happens to be reachable.
#
# $KDIR is the same edge answered as a CHECKOUT rather than a header set: the
# kernel repository's own build tree, holding the linked kernel a probe reads a
# namelist out of and the stamp checker that says which link an image carries.
# A headers release has neither, so $KDIR is empty for one, and a harness that
# needs the build tree refuses by name on that.
_c9k=$(C900_KERNEL="${C900_KERNEL:-}" sh "$_c9deps" kernel 2>/dev/null || :)
KDIR=""
if [ -n "$_c9k" ] && [ -d "$_c9k/os/include" ]; then
	KINC="$_c9k/os/include"		# a checkout
	KDIR="$_c9k"
elif [ -n "$_c9k" ] && [ -d "$_c9k/include" ]; then
	KINC="$_c9k/include"		# an unpacked headers release
else
	KINC=""
fi
# $TCID: the compiler's source id, alone in a file, for the makefiles to
# depend on -- the shell equivalent of toolchain.mk's, naming the same path.
TCID="$COHERENT_OS/hostbuild/build/.tcid"
# Once per build, not once per script: three dozen harnesses source this file in
# one `make', and thirty-six identical lines report nothing the first one did.
#
# The same guard records which compiler this build tree is using, which is why
# it lives here rather than in a target someone remembers to run: every harness
# that compiles anything comes through this file, so the record is current for
# every build and costs one git diff over the compiler's source.
#
# WHICH compiler was resolved is not the same question as WHICH SOURCE it came
# from, and only the second one distinguishes a fix that is in the compiler
# from a fix that is merely committed.  The toolchain's build directory is
# shared by every lane by default, so a compiler published before a fix landed
# is what an unsuspecting build picks up; the id recorded in $TCID is what lets
# a target that depends on it rebuild instead of shipping the stale object.
# See prov_tc_record in provenance.sh.
if [ -z "${C900_TC_REPORTED:-}" ]; then
	echo "toolchain: $C900_TC_SHAPE at C900_TOOLCHAIN=$C900_TOOLCHAIN" >&2
	# On the same line's heels, and only when it is not the default: a
	# non-default build directory changes WHICH compiler ran as completely
	# as a different toolchain does, so it is reported on the same terms.
	[ -n "${C900_TC_BUILD:-}" ] &&
		echo "toolchain: build dir C900_TC_BUILD=$TCB (not the shared $TC/build)" >&2
	. "$OS/hostbuild/provenance.sh"
	prov_tc_record "$TCB" "$COHERENT_OS/hostbuild/build/.tcstamp" "$TCID" "$C900_TC_SHAPE"
	C900_TC_REPORTED=1
fi
# c900_buildlog, for the harnesses that drive cc0/cc1/cc2 themselves rather
# than through ccz (which sources the same file).  It belongs to the toolchain
# because both repositories' scripts compile with it and the record has to read
# the same either way; a release older than the file, or the release host/ view
# that does not carry it, gets the no-op and behaves as before.
if [ -r "$TC/buildlog.sh" ]; then
	. "$TC/buildlog.sh"
else
	c900_buildlog() { :; }
	c900_buildmap() { :; }
fi
# WHICH SCRIPT COMPILED IT.  Every sweep here sources this file, and a sourced
# file sees the sourcing script's $0, so the recipe names itself without any of
# the thirty-odd having to remember to.  It is a source of a program in its own
# right -- the split of a directory into programs, the -D switches and the link
# order live in the script and nowhere else -- so a source package that carried
# the .c files without it would not carry a recipe that compiles them.
#
# The whole CHAIN, appended rather than set, because a sweep here is two scripts
# deep and both halves are the recipe: build-all-userland.sh decides that
# cmd/knapsack is built at all and with which arguments, build-cmd.sh decides
# which of its files are which program.  Either one alone reads as complete and
# is not.  Depth is never more than three, so this cannot grow without bound.
C900_BUILD_RECIPE="${C900_BUILD_RECIPE:+$C900_BUILD_RECIPE }$0"
export C900_BUILD_RECIPE
export TCID
export C900_TOOLCHAIN COHERENT_OS C900_TC_SHAPE C900_TC_REPORTED C900_TC_BUILD
export TCSYSINC
unset _c9root _c9deps _c9tc
# end of toolchain.sh
