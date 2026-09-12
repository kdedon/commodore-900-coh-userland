# provenance.sh -- build identity and freshness records; source this file.
# Stamps identify source, artifacts and dependency builds.

# prov_repo [dir] -- git worktree containing dir, or the current worktree.
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

# prov_header <label> [stampfile] -- print build identity.
# Return 0 for clean source, 1 for dirty source, 2 for a missing stamp.
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

# prov_id <file> -- first 12 SHA-1 digits, or none if absent.
# Kernel link identity must follow bytes because drivers bind absolute addresses.
prov_id() {
	[ -f "$1" ] || { echo none; return 0; }
	sha1sum "$1" 2>/dev/null | cut -c1-12
}

# prov_srcid <tree> [scope paths...] -- hash HEAD and uncommitted changes
# in scope, including untracked file contents.  Return unknown outside git.
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

# prov_scopeid <tree> [scope paths...] -- hash the scoped HEAD entries,
# working diff and untracked contents.  Exclude the commit id so unrelated
# commits do not invalidate a published artifact.
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

# prov_tc_record <build-dir> <record-file> <id-file> [shape]
# Record compiler identity; update id-file only when it changes.
# Report stale checkout builds; release builds use their recorded identity.
# Always return 0.
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

# prov_part_check <label> <stamp> <record> <shape> <fix>
# Refuse missing or stale dependency builds and record accepted identities.
# Release builds skip source freshness checks.  A nonempty C900_PART_ACCEPT
# allows a stale build.  Return 0 to proceed, 1 to stop.
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
