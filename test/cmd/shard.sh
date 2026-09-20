#!/bin/sh
# shard.sh -- partition run.sh's own check list across CI's matrix.
#
#	sh test/cmd/shard.sh <index> <count>
#
# Prints the checks shard <index> (0-based) of <count> runs, from run.sh
# --list, so every check lands in exactly one shard.  Each name, longest
# first, goes to the shard with the least durations.tab time so far.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)

IDX=${1:?usage: shard.sh <index> <count>}
CNT=${2:?usage: shard.sh <index> <count>}
case $IDX in ''|*[!0-9]*) echo "shard.sh: index must be a number" >&2; exit 2 ;; esac
case $CNT in ''|*[!0-9]*) echo "shard.sh: count must be a number" >&2; exit 2 ;; esac
[ "$CNT" -ge 1 ] || { echo "shard.sh: count must be at least 1" >&2; exit 2; }
[ "$IDX" -lt "$CNT" ] || { echo "shard.sh: index must be < count" >&2; exit 2; }

sh "$HERE/run.sh" --list > "$HERE/.shard-list.$$" || exit 1
trap 'rm -f "$HERE/.shard-list.$$"' EXIT

# Ties go to the lowest index, so the split is deterministic.
awk -v idx="$IDX" -v cnt="$CNT" '
	FNR == NR {
		if ($0 !~ /^#/ && NF >= 2) dur[$1] = $2
		next
	}
	{
		name = $1
		d = (name in dur) ? dur[name] : 30
		best = 0
		for (i = 1; i < cnt; i++) if (total[i] < total[best]) best = i
		total[best] += d
		if (best == idx) print name
	}
' "$HERE/durations.tab" "$HERE/.shard-list.$$"
