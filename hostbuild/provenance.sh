# provenance.sh -- shared build-provenance stamping, for `.' not for exec.
#
# Several builds share one filesystem namespace on one machine, so an
# artifact's identity is not implied by its path -- and an artifact built
# from uncommitted edits cannot be reproduced from any commit.  A stamp makes
# that self-announcing.
#
# A whole-tree `git status --porcelain' is useless as an alarm here: the tree
# normally carries uncommitted files belonging to other work, so a DIRTY flag
# that counts all of them is on permanently, and a warning that is always on
# is read as decoration.  So every stamp records TWO dirt counts:
#
#   dirty     the whole worktree             -- context, never an alarm
#   dirtysrc  ONLY the paths that were compiled into this artifact -- the alarm
#
# `dirtysrc' is what makes the line worth reading: it is nonzero exactly when
# the artifact cannot be reproduced from any commit.  Callers pass the scope
# (paths relative to the repo root) that actually feeds the thing they built.
#
# Usage:
#	. "$HERE/provenance.sh"
#	prov_write <stampfile> <kind> [scope-path ...] [-- k=v ...]
#	prov_header <label> [stampfile]     # prints; loud FIRST LINE if dirtysrc>0
#	prov_get <stampfile> <key>
#
# A stamp is `key=value' lines, one per line, values free of newlines.

# prov_repo [dir]: the git worktree containing <dir>, falling back to the
# caller's cwd.  The fallback matters: a stamp may legitimately be written to a
# scratch path outside the tree, and the tree it should NAME is still the one
# the build is running in.
prov_repo() {
	git -C "${1:-.}" rev-parse --show-toplevel 2>/dev/null ||
	git rev-parse --show-toplevel 2>/dev/null
}

# prov_write <stampfile> <kind> [scope paths...] [-- extra k=v...]
# Written to a temporary and renamed, so a concurrent reader never sees half a
# stamp -- the same rule the artifacts themselves follow.
prov_write() {
	_ps_file=$1; _ps_kind=$2; shift 2
	# Scope words are collected into a string (they are paths, no spaces);
	# the extras stay POSITIONAL, because a value legitimately contains
	# spaces (a file list) and flattening them would split one k=v into
	# several bare lines.
	_ps_scope=""
	while [ $# -gt 0 ]; do
		case "$1" in
		--) shift; break;;
		*=*)	# A scope word can never contain `='.  Seeing one means a
			# `--' separator was glued to its neighbour, which silently
			# produces a stamp with none of the keys the caller asked
			# for -- and a stamp missing its key reads downstream as a
			# missing artifact.  Refuse it instead.
			echo "prov_write: scope word \"$1\" looks like a k=v -- a '--' separator is missing or glued to its neighbour" >&2
			return 1;;
		*) _ps_scope="$_ps_scope $1";;
		esac
		shift
	done
	_ps_root=$(prov_repo "$(dirname "$_ps_file")")
	if [ -n "$_ps_root" ]; then
		_ps_head=$(git -C "$_ps_root" rev-parse HEAD 2>/dev/null)
		_ps_dirty=$(git -C "$_ps_root" status --porcelain 2>/dev/null | wc -l)
		if [ -n "$_ps_scope" ]; then
			# shellcheck disable=SC2086
			_ps_dsrc=$(git -C "$_ps_root" status --porcelain -- $_ps_scope 2>/dev/null | wc -l)
			# shellcheck disable=SC2086
			# Capped: the point of the list is to name the edits, and a
			# 60-entry line is scrolled past rather than read.  The
			# count above is the complete answer; this is the lead.
			# shellcheck disable=SC2086
			_ps_dlist=$(git -C "$_ps_root" status --porcelain -- $_ps_scope 2>/dev/null |
				    awk '{print $NF}' | sort | head -8 | tr '\n' ' ')
			if [ "$_ps_dsrc" -gt 8 ]; then
				_ps_dlist="$_ps_dlist+$((_ps_dsrc - 8)) more"
			fi
		else
			_ps_dsrc=$_ps_dirty; _ps_dlist=""
		fi
	else
		_ps_head=unknown; _ps_dirty=-1; _ps_dsrc=-1; _ps_dlist=""
	fi
	mkdir -p "$(dirname "$_ps_file")"
	{
		echo "kind=$_ps_kind"
		echo "commit=$_ps_head"
		echo "dirty=$_ps_dirty"
		echo "dirtysrc=$_ps_dsrc"
		echo "scope=$(echo $_ps_scope)"
		echo "dirtyfiles=$_ps_dlist"
		echo "tree=${_ps_root:-unknown}"
		# The SOURCE identity of what was just built, over the same
		# scope: `commit' alone cannot distinguish two lanes' different
		# uncommitted edits, and `dirtysrc' counts them without naming
		# which.  A consumer in another repository recomputes this over
		# the producer's tree and compares -- that is the whole of the
		# staleness check, and it is why the id is recorded here rather
		# than derived downstream from something that only looks stable.
		# shellcheck disable=SC2086
		echo "srcid=$(prov_scopeid "${_ps_root:-.}" $_ps_scope)"
		echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		echo "host=$(hostname 2>/dev/null)"
		echo "builder=$$"
		for _ps_kv in "$@"; do echo "$_ps_kv"; done
	} > "$_ps_file.tmp.$$" && mv -f "$_ps_file.tmp.$$" "$_ps_file"
	unset _ps_file _ps_kind _ps_scope _ps_root _ps_head \
	      _ps_dirty _ps_dsrc _ps_dlist _ps_kv
}

# prov_get <stampfile> <key> -- empty if absent.
prov_get() {
	[ -f "$1" ] || return 0
	sed -n "s/^$2=//p" "$1" | head -1
}

# prov_header <label> [stampfile] -- one or two lines describing what is about
# to be used.  If the stamp says the artifact was built with uncommitted edits
# in its OWN source, the warning is the FIRST line printed, before any result,
# so it cannot be discovered three lanes later.
#
# Returns 0 clean, 1 DIRTY, 2 NO STAMP.  The two are different findings and a
# caller must be able to treat them differently: dirty is a caveat on a result
# that is still a result, while an artifact with no stamp at all has no known
# origin and is the thing a consistency check should refuse.
prov_header() {
	_ph_label=$1; _ph_f=${2:-}; _ph_rc=0
	if [ -n "$_ph_f" ] && [ -f "$_ph_f" ]; then
		_ph_c=$(prov_get "$_ph_f" commit)
		_ph_d=$(prov_get "$_ph_f" dirty)
		_ph_ds=$(prov_get "$_ph_f" dirtysrc)
		_ph_t=$(prov_get "$_ph_f" tree)
		_ph_b=$(prov_get "$_ph_f" built)
		if [ "${_ph_ds:-0}" -gt 0 ] 2>/dev/null; then
			echo "!! $_ph_label WAS BUILT FROM A DIRTY TREE ($_ph_ds uncommitted file(s) in its own source)"
			echo "!! it matches NO commit -- do not report a failure from it as a source bug"
			echo "!!   $(prov_get "$_ph_f" dirtyfiles)"
			_ph_rc=1
		fi
		echo "== $_ph_label: commit $(echo "$_ph_c" | cut -c1-8) dirtysrc=$_ph_ds dirty=$_ph_d built $_ph_b"
		echo "==   tree $_ph_t"
	else
		echo "!! $_ph_label: NO PROVENANCE STAMP${_ph_f:+ ($_ph_f)} -- origin unknown"
		_ph_rc=2
	fi
	unset _ph_label _ph_f _ph_c _ph_d _ph_ds _ph_t _ph_b
	return $_ph_rc
}

# prov_now <label> [scope paths...] -- the CURRENT state of this tree, for
# harnesses that have no stamp to read (or want it beside one that they do).
prov_now() {
	_pn_label=$1; shift
	_pn_root=$(prov_repo)
	if [ -z "$_pn_root" ]; then echo "== $_pn_label: not a git tree"; return 0; fi
	_pn_h=$(git -C "$_pn_root" rev-parse HEAD 2>/dev/null | cut -c1-8)
	_pn_d=$(git -C "$_pn_root" status --porcelain 2>/dev/null | wc -l)
	if [ $# -gt 0 ]; then
		_pn_s=$(git -C "$_pn_root" status --porcelain -- "$@" 2>/dev/null | wc -l)
		echo "== $_pn_label: commit $_pn_h dirtysrc=$_pn_s dirty=$_pn_d tree $_pn_root"
	else
		echo "== $_pn_label: commit $_pn_h dirty=$_pn_d tree $_pn_root"
	fi
	unset _pn_label _pn_root _pn_h _pn_d _pn_s
}

# prov_id <file> -- the content identity of a build artifact.  sha1 of the bytes,
# short.  Used as the KERNEL LINK ID: `ld -k' bakes absolute kernel addresses
# into every loadable driver, so a driver is only valid against the exact
# kernel.out image it was linked from, and content is the only honest name for
# that -- an mtime says when, not which.
prov_id() {
	[ -f "$1" ] || { echo none; return 0; }
	sha1sum "$1" 2>/dev/null | cut -c1-12
}

# prov_srcid <tree> [scope paths...] -- the SOURCE identity of a build: WHICH
# SOURCE was compiled, not which bytes came out.  12 hex, or `unknown' when
# <tree> is not a git worktree (an unpacked release has no source to name, and
# its stamp's recorded id stands on its own).
#
# The output bytes cannot serve as this id.  Two builds of the compiler from
# one tree into two build directories differ, and differ ONLY there: the
# staging path is mapped into the debug info and the build-id note follows it.
# A content id would report every lane's private build of identical source as a
# different compiler -- an alarm on the normal case.  It is also the wrong
# question.  A compiler is the same compiler wherever it was built, and what a
# lane holding a bad object needs is which source to go and read.
#
# HEAD alone is not enough either: this tree is normally dirty in scope, and a
# bare commit id would call two lanes' different edits one compiler.  So the id
# covers uncommitted content too -- the diff against HEAD, plus the bytes of
# untracked files.  Scope is the paths that were compiled in, as everywhere
# else here: a change outside it did not change the compiler.
prov_srcid() {
	_pi_t=$1; shift
	git -C "$_pi_t" rev-parse HEAD >/dev/null 2>&1 || { echo unknown; return 0; }
	{
		git -C "$_pi_t" rev-parse HEAD
		git -C "$_pi_t" status --porcelain -- "$@"
		git -C "$_pi_t" diff HEAD -- "$@"
		git -C "$_pi_t" ls-files -o --exclude-standard -- "$@" |
		(cd "$_pi_t" && tr '\n' '\0' | xargs -0 -r sha1sum 2>/dev/null)
	} 2>/dev/null | sha1sum | cut -c1-12
	unset _pi_t
}

# prov_scopeid <tree> [scope paths...] -- the CONTENT identity of a scope: did
# the source that feeds this artifact change, committed or not.  12 hex, or
# `unknown' when <tree> is not a git worktree.
#
# NOT prov_srcid, and the difference is the whole point.  prov_srcid folds in
# `rev-parse HEAD', so ANY commit anywhere in the repository moves it -- a
# README, a makefile, another lane's program.  Across a repository boundary that
# is fatal to the alarm: four lanes commit to the userland all afternoon, and an
# id that moved on every one of them would report a freshly packed image as
# stale before it finished writing.  An alarm that is always on is read as
# decoration, which is the same objection this file's header makes to a
# whole-tree dirty flag.
#
# So the commit is not in it.  What is in it is the CONTENT of the scope: the
# blob ids git records for the tracked files in it, plus uncommitted changes to
# them, plus the bytes of untracked files in it.  Two trees at different commits
# whose scope content is identical have the same id, which is exactly the claim
# an artifact's stamp should be making.
#
# prov_srcid keeps its callers (prov_tc_record) unchanged: the compiler's id is
# recorded by the toolchain repository's own copy of this file, and both sides of
# that comparison have to compute it the same way.
prov_scopeid() {
	_pj_t=$1; shift
	git -C "$_pj_t" rev-parse --git-dir >/dev/null 2>&1 || { echo unknown; return 0; }
	{
		# `--' with no pathspec is every path, which is what an empty
		# scope should mean and what prov_write's dirtysrc already does.
		git -C "$_pj_t" ls-tree -r HEAD -- "$@"
		git -C "$_pj_t" diff HEAD -- "$@"
		git -C "$_pj_t" ls-files -o --exclude-standard -- "$@" |
		(cd "$_pj_t" && tr '\n' '\0' | xargs -0 -r sha1sum 2>/dev/null)
	} 2>/dev/null | sha1sum | cut -c1-12
	unset _pj_t
}

# prov_tc_record <toolchain-build-dir> <record-file> <id-file> [shape] -- record
# WHICH COMPILER this build tree is using, and leave its identity somewhere the
# build system can depend on.  Always returns 0: this reports, it does not
# decide.
#
# The compiler is a build INPUT.  When an input changes, what was built from it
# is out of date and make rebuilds it -- so <id-file> holds the compiler's
# source id and nothing else, and every target compiled with the toolchain
# names it as a prerequisite.  A changed compiler then rebuilds exactly what
# depends on it, silently and with no step for anyone to remember.
#
# <id-file> is rewritten ONLY when the value differs.  Rewriting it every run
# would make its mtime move every run, and every build would relink -- a
# permanent rebuild in place of a permanent question, which is no better.
# <record-file> carries the full human-readable record and is rewritten every
# time; nothing depends on it, so its mtime is free to move.
#
# <shape> is `checkout' (the default) or `release X.Y.Z'.  A release is pinned
# on purpose and its source tree is not on this machine, so the staleness
# comparison is not run for one: it would report every pinned release as stale
# the moment a checkout beside it moved on.
#
# STALE -- the published compiler is behind its own source -- is still worth
# saying, because the fix a lane is looking for may be in the tree and not in
# the compiler.  It is said and not enforced: the remedy is a build in ANOTHER
# repository, which this one cannot run for you and should not stop for.
prov_tc_record() {
	_tk_b=$1; _tk_rec=$2; _tk_idf=$3; _tk_shape=${4:-checkout}
	_tk_s="$_tk_b/z8001/.provenance"
	_tk_id=$(prov_get "$_tk_s" tcid)
	if [ -z "$_tk_id" ]; then
		# Not a refusal, but not silent either: with no id the build has
		# nothing to depend on, so a compiler swapped for another
		# equally anonymous one would not rebuild anything.  Both
		# published shapes carry an id; this is the case where one does
		# not, and it is worth seeing.
		echo "== toolchain has no source id ($_tk_s)" >&2
		echo "==   nothing can say which source this compiler came from," >&2
		echo "==   so a change of compiler cannot trigger a rebuild." >&2
		_tk_id=unknown
	fi
	_tk_tree=$(prov_get "$_tk_s" tree)
	_tk_scope=$(prov_get "$_tk_s" scope)
	if [ "$_tk_shape" = checkout ] && [ -n "$_tk_tree" ] && [ -d "$_tk_tree" ]; then
		# shellcheck disable=SC2086
		_tk_live=$(prov_srcid "$_tk_tree" $_tk_scope)
		if [ -n "$_tk_live" ] && [ "$_tk_live" != unknown ] && [ "$_tk_live" != "$_tk_id" ]; then
			echo "== the compiler at $_tk_b is behind its own source." >&2
			echo "==   built from $_tk_id (commit $(prov_get "$_tk_s" commit | cut -c1-8), $(prov_get "$_tk_s" built))" >&2
			echo "==   $_tk_tree is now $_tk_live" >&2
			echo "==   A fix in that tree is not in this compiler: (cd $_tk_tree && make cc as ld)." >&2
		fi
	fi

	# The id, alone, for the build system to depend on.  Compared before it is
	# moved into place, so an unchanged compiler leaves the mtime alone.
	mkdir -p "${_tk_idf%/*}"
	echo "$_tk_id" > "$_tk_idf.tmp.$$"
	if [ -f "$_tk_idf" ] && cmp -s "$_tk_idf.tmp.$$" "$_tk_idf"; then
		rm -f "$_tk_idf.tmp.$$"
	else
		mv -f "$_tk_idf.tmp.$$" "$_tk_idf"
	fi

	mkdir -p "${_tk_rec%/*}"
	{
		echo "kind=consumer-toolchain"
		echo "toolchain_id=$_tk_id"
		echo "toolchain=$_tk_b"
		echo "toolchain_commit=$(prov_get "$_tk_s" commit)"
		echo "toolchain_dirtysrc=$(prov_get "$_tk_s" dirtysrc)"
		echo "toolchain_built=$(prov_get "$_tk_s" built)"
		echo "recorded=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	} > "$_tk_rec.tmp.$$" && mv -f "$_tk_rec.tmp.$$" "$_tk_rec"
	unset _tk_b _tk_rec _tk_idf _tk_shape _tk_s _tk_id _tk_tree _tk_scope _tk_live
	return 0
}

# prov_part_check <label> <stamp> <record> <shape> <fix> -- the same two
# questions prov_tc_record asks about the compiler, asked about a PART BUILT IN
# ANOTHER REPOSITORY: the kernel, and the userland.  Returns 0 to proceed, 1 to
# stop; the caller stops, because a sourced file must not decide that for it.
#
# An image is assembled out of artifacts this repository cannot build and must
# not try to.  That leaves exactly two ways for it to be wrong and both are
# silent:
#
#   MISSING    the producer is checked out but has published nothing (or has
#              published half of it).  `make' cannot see this as a missing
#              prerequisite, because the prerequisite is in a tree it has no
#              rule for -- so without this check the packer either fails deep
#              inside content resolution or, worse, packs whatever an earlier
#              build left behind.  <fix> is the command to run, in the
#              repository that owns it.
#
#   STALE      the published artifact is behind its own source.  This is the
#              one that will keep happening: four lanes edit the userland while
#              an image is being packed, and a binary built an hour ago is what
#              a pack picks up whether or not the fix it is missing has landed.
#              Committed or not makes no difference to the image, so the
#              comparison is over prov_srcid, which moves when uncommitted
#              content moves.
#
# A release has no source tree on this machine, so only the identity half
# applies to it -- running the staleness half would report every pinned release
# as stale the moment a checkout beside it moved on.
#
# The identity is RECORDED either way, in <record> and (by the caller) in the
# image's own stamp, so that a packed image can answer "which kernel, which
# userland" without being taken apart.
#
# C900_PART_ACCEPT=1 proceeds and re-records, for the operator who knows --
# packing an image deliberately against a part whose tree has moved on.
prov_part_check() {
	_pc_l=$1; _pc_s=$2; _pc_rec=$3; _pc_shape=${4:-checkout}; _pc_fix=$5
	if [ ! -f "$_pc_s" ]; then
		echo "!! NO $_pc_l: $_pc_s does not exist." >&2
		echo "!!   That stamp is written when the $_pc_l is published, so" >&2
		echo "!!   nothing has been -- or only part of it has." >&2
		echo "!!   An image packed now would carry whatever an earlier build" >&2
		echo "!!   left in place, under this build's name." >&2
		echo "!! Publish it: $_pc_fix" >&2
		return 1
	fi
	_pc_id=$(prov_get "$_pc_s" srcid)
	_pc_tree=$(prov_get "$_pc_s" tree)
	_pc_scope=$(prov_get "$_pc_s" scope)
	_pc_why=
	if [ -z "$_pc_id" ] || [ "$_pc_id" = unknown ]; then
		# A stamp with no source id is not a refusal: an unpacked release
		# has no source to name and its recorded commit stands on its
		# own.  It is said out loud, because a build whose parts cannot
		# be traced to source must not look like one whose parts can.
		echo "== $_pc_l: no source id in $_pc_s -- identity is its recorded" >&2
		echo "==   commit $(prov_get "$_pc_s" commit | cut -c1-8) alone (built $(prov_get "$_pc_s" built))" >&2
		_pc_id=${_pc_id:-unknown}
	elif [ "$_pc_shape" = checkout ] && [ -n "$_pc_tree" ] && [ -d "$_pc_tree" ]; then
		# shellcheck disable=SC2086
		_pc_live=$(prov_scopeid "$_pc_tree" $_pc_scope)
		if [ "$_pc_live" != unknown ] && [ "$_pc_live" != "$_pc_id" ]; then
			_pc_why="STALE $_pc_l"
			echo "!! STALE $_pc_l: what is published is behind its own source." >&2
			echo "!!   published from source id $_pc_id (commit $(prov_get "$_pc_s" commit | cut -c1-8), $(prov_get "$_pc_s" built))" >&2
			echo "!!   $_pc_tree is now source id $_pc_live (commit $(git -C "$_pc_tree" rev-parse --short=8 HEAD 2>/dev/null))" >&2
			echo "!!   scope: $_pc_scope" >&2
			echo "!! An image packed now would be named after a tree it was not made from." >&2
			echo "!! Republish it: $_pc_fix" >&2
		fi
	fi
	if [ -n "$_pc_why" ]; then
		if [ -n "${C900_PART_ACCEPT:-}" ]; then
			echo "!! C900_PART_ACCEPT is set -- proceeding, and recording $_pc_id." >&2
		else
			echo "!! Refusing to pack an image.  C900_PART_ACCEPT=1 proceeds anyway." >&2
			return 1
		fi
	fi
	mkdir -p "${_pc_rec%/*}"
	{
		echo "kind=consumer-$_pc_l"
		echo "${_pc_l}_id=$_pc_id"
		echo "${_pc_l}_shape=$_pc_shape"
		echo "${_pc_l}_tree=$_pc_tree"
		echo "${_pc_l}_commit=$(prov_get "$_pc_s" commit)"
		echo "${_pc_l}_dirtysrc=$(prov_get "$_pc_s" dirtysrc)"
		echo "${_pc_l}_built=$(prov_get "$_pc_s" built)"
		echo "recorded=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	} > "$_pc_rec.tmp.$$" && mv -f "$_pc_rec.tmp.$$" "$_pc_rec"
	echo "== $_pc_l: source id $_pc_id ($_pc_shape, commit $(prov_get "$_pc_s" commit | cut -c1-8), dirtysrc=$(prov_get "$_pc_s" dirtysrc), built $(prov_get "$_pc_s" built))" >&2
	unset _pc_l _pc_s _pc_rec _pc_shape _pc_fix _pc_id _pc_tree _pc_scope _pc_why _pc_live
	return 0
}
