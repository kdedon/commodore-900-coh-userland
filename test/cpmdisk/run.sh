#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
#
# test/cpmdisk/run.sh -- cpm(1) against drive images the check formats itself.
#
# base/cmd/cpm.c is compiled unchanged by the host cc and run over drives
# check.py formats; check.py then decodes the directory itself.
#
#	sh test/cpmdisk/run.sh
#
# Writes only to hostbuild/build/work/cpmdisk.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
HOSTCC=${HOSTCC:-gcc}
PYTHON=${PYTHON:-python3}
WORK=$OS/hostbuild/build/work/cpmdisk

rm -rf "$WORK"
mkdir -p "$WORK"
"$HOSTCC" -std=gnu89 -w -o "$WORK/cpm" "$OS/base/cmd/cpm.c" || exit 2

"$PYTHON" "$HERE/check.py" "$WORK" "$WORK/cpm"
