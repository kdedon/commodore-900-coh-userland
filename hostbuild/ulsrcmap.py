#!/usr/bin/env python3
"""ulsrcmap.py -- fold the build's own record of what it compiled into the
SOURCE MAP this repository publishes: build/.ulsrcmap, one line per program.

    python3 ulsrcmap.py [-o OUT] [-q] [<record>]

`record' defaults to $C900_BUILD_MAP, else build/compiled-programs.log.

WHY THE PRODUCER PUBLISHES THIS.  A binary package creates licence obligations
(GPLv1 rcs, GPLv2 gzip and screen, GPLv3+ ttycity, GPL archive tools) and a
source package is how they are discharged: for every executable
shipped, the complete corresponding source, the recipe that compiles it and the
licence texts governing it.  The assembling repository cannot work out which
sources built which program and must not guess -- one directory is not one
program here (cmd/diff builds diff and /usr/lib/diffh; cmd/knapsack builds
enroll, xencode and xdecode and no `knapsack' at all), half the programs are
linked from objects a separate invocation compiled, and MGR is built by its own
Makefile in a tree of two thousand files.  Only the compile driver knows, so the
compile driver records it (host/buildlog.sh, c900_buildmap) and this folds the
record.

WHAT A LINE NAMES, and why more than the .c files:

  a SMALL contributing directory,        base/cmd/sh, games/bsd/rogue,
  whole                                  archive/rcs: a directory that holds
                                         one program's source holds the rest of
                                         what that source needs and states --
                                         the headers, the yacc grammar (cmd/find
                                         has no .c file at all, only find.y), the
                                         vendor's own makefile, the readme, the
                                         licence text.  Named as a directory, so
                                         the consumer walks it and nothing has to
                                         be enumerated here or kept in step.
  a LARGE one, by the compiled files     base/cmd holds five hundred unrelated
  only                                   single-file commands and mgr two
                                         thousand files; naming either would put
                                         the whole tree in every package that
                                         drew one file from it.  SMALL is fewer
                                         than 64 files counted recursively -- a
                                         per-program source directory is small,
                                         and a tree of unrelated programs is not.
  the recipe chain                        hostbuild/build-*.sh, recorded by
                                         the build ($C900_BUILD_RECIPE): the
                                         script that compiled and linked this
                                         program.
  the build's ENTRY POINT, marked `+'    hostbuild/Makefile, which selects the
                                         targets and computes their dependency
                                         lists.  A source package owes it -- it
                                         is part of the recipe -- but no program
                                         is compiled from it, and no stamp here
                                         names it as a prerequisite, so editing
                                         it rebuilds nothing and changes no
                                         binary.  See THE `+' MARKER below.
  the headers, libc and csu               the headers every program compiles
                                         against and the C library and startup
                                         code every one of them is statically
                                         linked with.  A statically linked GPL
                                         program's corresponding source includes
                                         them; they are built from the TOOLCHAIN
                                         repository's src/{include,libc,csu}, so
                                         those are what is named.

THE `+' MARKER: a path may be written `+<path>', which says it belongs in this
program's complete corresponding source and the program is NOT BUILT FROM it.
The distinction exists because the map answers two questions, and only one of
them wants the same set.  A -src package wants everything the licence obliges,
so it takes marked and unmarked paths alike and the marker costs it nothing.
The other question is "is this staged binary older than a source it was built
from" (commodore-900-dist, dist.py source_ages), and an unmarked-only answer
is the one worth reading: a program is behind a source when rebuilding from that
source would produce a different program.  Naming every program every time the
entry point is touched is a report that no longer distinguishes the fix nobody
rebuilt from an afternoon's editing, which is the same as no report.

OVER-INCLUSION IS SAFE HERE AND UNDER-INCLUSION IS NOT, which decides every
close call: a package carrying a source file the program did not need is still a
true statement about what was shipped, and one missing a file is not.  So the
record is read as a UNION across builds (it is append-only), an archive on a
link line pulls in every object logged beside it, and a path that no longer
exists is dropped rather than made to fail.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.dirname(HERE)                      # the repository root; map paths are
                                                # relative to it (dist FORMAT)
B = os.path.join(HERE, 'build')

# THE BUILD'S ENTRY POINT, named by every row and MARKED: a source package owes
# the recipe, and the Makefile is the half of it that decides which targets exist
# and what each stamp depends on.  Nothing is compiled from it, and no rule in it
# names it as a prerequisite of any stamp, so an edit to it rebuilds nothing --
# which is why it is marked rather than judged (`+', see the header comment).
CARRIED = ['hostbuild/Makefile']

# THE SAME, IN THE TOOLCHAIN REPOSITORY.  The C library, the startup code and
# the headers every program compiles against are built from src/{libc,csu} and
# src/include over there, not from anything here, and a statically linked GPL
# program's corresponding source includes them.  So they are named, and they are
# named UNCONDITIONALLY -- unlike everything else in this file, which is dropped
# when the path is not in this tree.  Paths in the map are resolved by the
# consumer across the whole search path, so these find the toolchain checkout;
# with none, a -src package refuses by name, which is the right answer, because
# a package cut without the library it links is not the complete corresponding
# source it claims to be.
ELSEWHERE = ['src/include', 'src/libc', 'src/csu']

# Where the published programs are (publish-userland.sh's own set), less the one
# subtree in it that is not published: build/mgr/host holds bitmaptoc and
# fonttoc, x86 tools MGR's Makefile runs during its build, which no list stages
# and which could not execute on the machine if it did.
PUBLISHED = ['bin', 'root', 'mgr']
NOTPUBLISHED = [os.path.join('mgr', 'host')]


def rel(p):
    """`p' as a path relative to the repository root, or None if it is not under it."""
    r = os.path.relpath(os.path.normpath(p), OS)
    return None if r.startswith('..') or r == '.' else r


def progname(out):
    """The name a shipped program has, from the path the link wrote.

    Every sweep here links to a SIDE NAME and renames into place, so that a
    failing link leaves the previous binary alone (build-cmd.sh's link_one).
    That convention is this repository's, which is why it is undone here and not
    in the compile driver: `build/bin/.ls.new' is /bin/ls.
    """
    b = os.path.basename(out)
    if b.startswith('.') and b.endswith('.new'):
        b = b[1:-len('.new')]
    return b


def read(path):
    """The record, as {(cwd, out): {'in': [...], 'recipe': [...]}} in order."""
    inv = {}
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if len(f) != 4 or f[0] not in ('o', 'i'):
            continue
        kind, cwd, out, val = f
        e = inv.setdefault((cwd, out), {'in': [], 'recipe': []})
        if kind == 'o':
            for r in val.split():
                if r not in e['recipe']:
                    e['recipe'].append(r)
        elif val not in e['in']:
            e['in'].append(val)
    return inv


def main(argv):
    out = os.path.join(B, '.ulsrcmap')
    quiet = False
    rec = os.environ.get('C900_BUILD_MAP') or os.path.join(
        B, 'compiled-programs.log')
    args = argv[1:]
    while args:
        if args[0] == '-o':
            out = args[1]; args = args[2:]
        elif args[0] == '-q':
            quiet = True; args = args[1:]
        else:
            rec = args[0]; args = args[1:]
    if not os.path.exists(rec):
        sys.stderr.write(
            "ulsrcmap.py: no build record at %s.\n"
            "  Nothing has been compiled with $C900_BUILD_MAP set, so there is\n"
            "  no map to publish and every -src package will refuse by name.\n"
            "  Build with `make -C hostbuild', which exports it.\n" % rec)
        return 1
    inv = read(rec)
    extra = {}
    xp = os.path.join(HERE, 'ulsrcmap.extra')
    if os.path.exists(xp):
        for line in open(xp):
            f = line.split('#')[0].split()
            if len(f) >= 2:
                extra.setdefault(f[0], []).extend(f[1:])

    # Declared source for a link-time LIBRARY, by the archive's basename --
    # see ulsrcmap.libs for why an archive's own compile record cannot always
    # be trusted to name it.  Keyed on basename, not the full path a link
    # names it by, since a program links `libsocket.a' or `../net/libsocket.a'
    # depending on where it stands.
    libmap = {}
    lp = os.environ.get('C900_ULSRCMAP_LIBS') or os.path.join(HERE, 'ulsrcmap.libs')
    if os.path.exists(lp):
        for line in open(lp):
            f = line.split('#')[0].split()
            if len(f) >= 2:
                libmap[f[0]] = f[1:]

    # Objects, by the ABSOLUTE path a later link will name them under.  This is
    # the join between the two halves of a split compile: build-screen.sh,
    # build-netcmds.sh, build-sendmail.sh, build-curses.sh and MGR's own
    # Makefile all compile with -c and link the objects in a separate
    # invocation, and the link line carries no source at all.
    objsrc = {}
    for (cwd, o), e in inv.items():
        if not o.endswith('.o'):
            continue
        a = os.path.normpath(os.path.join(cwd, o))
        objsrc.setdefault(a, set()).update(
            os.path.normpath(os.path.join(cwd, i)) for i in e['in'])

    def expand(cwd, inputs, seen):
        """The source files behind a link's inputs."""
        srcs, ext = set(), set()
        for i in inputs:
            a = os.path.normpath(os.path.join(cwd, i))
            if a in seen:
                continue
            seen.add(a)
            if i.endswith('.o'):
                if a in objsrc:
                    s, x = expand(cwd, sorted(objsrc[a]), seen)
                    srcs |= s; ext |= x
                else:
                    ext.add(a)
            elif i.endswith('.a') and os.path.basename(a) in libmap:
                # A DECLARED library (ulsrcmap.libs) -- trusted ahead of the
                # object-record search below, which cannot tell an archive
                # nobody recompiled this run from one that was never near any
                # source at all.
                for p in libmap[os.path.basename(a)]:
                    f = os.path.join(OS, p)
                    if os.path.exists(f):
                        srcs.add(os.path.normpath(f))
            elif i.endswith('.a'):
                # An ARCHIVE names no members on the link line and `ar' is not
                # this record's business, so its members are taken to be the
                # objects logged nearest to it, by a search that widens in three
                # steps and stops at the first that finds any:
                #
                #   beside it            net/libsocket.a, net/*.o
                #   below it             $TCB/curses/libterm.a, curses/obj/*.o
                #   below its parent     build/mgr/lib/libmgrcl.a,
                #                        build/mgr/obj/*.o
                #
                # Widening in order rather than taking the whole subtree at once
                # is what keeps netstat's package out of net/test: net holds
                # libsocket.a and its objects at the top and forty network
                # probes' objects below, and a subtree-wide rule put every one of
                # them in.  The widest step that is reached does over-reach --
                # libterm.a and libcurses.a share one obj/ by three files, and
                # every MGR client gets the server's client-library objects
                # whether it linked them or not -- and over-reaching is the way
                # round this map may be wrong.
                d = os.path.dirname(a)
                found = []
                for where in (
                        lambda o: os.path.dirname(o) == d,       # beside it
                        lambda o: o.startswith(d + os.sep),      # below it
                        lambda o: o.startswith(              # below its parent
                            os.path.dirname(d) + os.sep)):
                    found = [s for o, s in objsrc.items() if where(o)]
                    if found:
                        break
                for s in found:
                    srcs |= s
                if not found:
                    ext.add(a)
            else:
                srcs.add(a)
        return srcs, ext

    # One entry per program name.  A record whose output is an object or an
    # archive is not a program; everything else is, and is keyed by the name the
    # sweep renamed it to.
    progs, external = {}, {}
    for (cwd, o), e in sorted(inv.items()):
        if o.endswith('.o') or o.endswith('.a'):
            continue
        srcs, ext = expand(cwd, e['in'], set())
        p = progname(o)
        d = progs.setdefault(p, {'src': set(), 'recipe': set()})
        d['src'] |= srcs
        for r in e['recipe']:
            # The record is APPEND-ONLY and holds rows written against earlier
            # layouts of this tree, so a recipe path is checked for existence
            # exactly as a source path is.  Without that check a row naming a
            # script at a path that no longer exists survives into the map, and
            # every -src package drawing on it refuses with the old path.
            a = os.path.normpath(os.path.join(cwd, r))
            if rel(a) and os.path.exists(a):
                d['recipe'].add(a)
        if ext:
            external.setdefault(p, set()).update(ext)

    # A DIRECTORY IS PACKED WHOLE by the consumer, so naming one is only correct
    # when everything in it is source.  Three of these trees are built IN PLACE
    # (games/net/hunt's Makefile leaves its objects and its linked binary beside
    # the .c files), and a source package carrying objects and a Z8001 binary
    # would be a source package by name only.  So a directory is named whole when
    # git tracks every file in it, and otherwise expanded to the files git does
    # track -- which is the same set minus the build's leavings.
    tracked = set()
    try:
        import subprocess
        r = subprocess.run(['git', '-C', OS, 'ls-files', '-z'],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        tracked = set(x for x in r.stdout.decode().split('\0') if x)
    except OSError:
        pass

    # Is a contributing directory this program's own, or a tree of unrelated
    # programs?  Answered by size, recursively -- mgr holds only eight entries
    # and two thousand files below them, and a consumer told to take a directory
    # walks all of it.
    SMALL = 64
    small, expand_to = {}, {}

    def is_small(d):
        if d not in small:
            n = 0
            for _, _, files in os.walk(os.path.join(OS, d)):
                n += len(files)
                if n >= SMALL:
                    break
            small[d] = n < SMALL
        return small[d]

    def whole(d):
        """[d] when every file under it is tracked, else its tracked files."""
        if d not in expand_to:
            dirty, keep = False, []
            for base, _, files in os.walk(os.path.join(OS, d)):
                for n in files:
                    r = os.path.relpath(os.path.join(base, n), OS)
                    if r in tracked:
                        keep.append(r)
                    else:
                        dirty = True
            expand_to[d] = sorted(keep) if (dirty and tracked) else [d]
        return expand_to[d]

    lines, unmapped = [], []
    for p in sorted(progs):
        paths, dirs = set(), set()
        for s in sorted(progs[p]['src']):
            r = rel(s)
            if r is None or not os.path.exists(s):
                continue
            # Build output is not source.  MGR generates its icon table into
            # build/mgr/gen and compiles that; naming it would put an untracked
            # generated file in a source package and leave what generated it out.
            # The generator and its inputs are declared in ulsrcmap.extra.
            if r.startswith(os.path.join('hostbuild', 'build') + os.sep):
                continue
            paths.add(r)
            dirs.add(os.path.dirname(r))
        if not paths:
            unmapped.append(p)
            continue
        for d in sorted(dirs):
            if d and is_small(d):
                # The directory supersedes its own files: a consumer walks it,
                # so naming both would say the same thing twice.
                paths = set(x for x in paths
                            if os.path.dirname(x) != d) | set(whole(d))
        for r in sorted(progs[p]['recipe']):
            paths.add(rel(r))
        for a in CARRIED:
            if os.path.exists(os.path.join(OS, a)):
                paths.update('+' + x for x in
                             (whole(a) if os.path.isdir(os.path.join(OS, a))
                              else [a]))
        paths.update(ELSEWHERE)
        lines.append((p, sorted(paths)))

    # The published set, for the coverage line and for the two rules below: an
    # EXECUTABLE staged under one of the published trees.  The data files staged
    # beside them (adventure.msg, cards.pck, less.hlp) are not programs and owe
    # no source of their own, and the objects and archives left in the working
    # directories are not published at all -- counting either would report a
    # shortfall that is not one.
    published = {}
    for d in PUBLISHED:
        for base, _, files in os.walk(os.path.join(B, d)):
            if any(os.path.relpath(base, B).startswith(x)
                   for x in NOTPUBLISHED):
                continue
            for n in files:
                if n.startswith('.') or os.path.splitext(n)[1] in (
                        '.o', '.a', '.c', '.h'):
                    continue
                p = os.path.join(base, n)
                if os.access(p, os.X_OK):
                    published[n] = p
    have = dict(lines)

    # A PROGRAM SHIPPED UNDER A SECOND NAME.  gzip is installed as gunzip and
    # zcat as well, compress as uncompress and zcat, by copying the binary --
    # not by a link, so the copy is a published program in its own right and
    # owes its own source.  It is the same source, and that is a fact about the
    # bytes rather than a guess: the copy is matched to the original by content.
    # A rename that changed the program would not match and would be reported.
    import hashlib

    def digest(p):
        h = hashlib.sha1()
        with open(p, 'rb') as fh:
            for b in iter(lambda: fh.read(65536), b''):
                h.update(b)
        return h.hexdigest()

    bycontent = {}
    for n, p in published.items():
        if n in have:
            try:
                bycontent.setdefault(digest(p), n)
            except OSError:
                pass
    for n, p in sorted(published.items()):
        if n in have:
            continue
        try:
            d = digest(p)
        except OSError:
            continue
        if d in bycontent:
            lines.append((n, have[bycontent[d]]))
            have[n] = have[bycontent[d]]

    # THE SUPPLEMENT: what the compile record cannot see.  merge(1) is a shell
    # script the build seds paths into and wargames(6) a shell game copied whole
    # -- nothing compiles either, so neither leaves a record; and two of the
    # network daemons' objects are assembled by a Makefile calling as-z8001
    # directly, which is not the compile driver and records nothing.  All of them
    # are shipped programs owing their source like any other, so the ones this
    # tree holds are declared in ulsrcmap.extra -- by hand, because nothing
    # computes them, and reviewably, because a hand declaration nobody can see is
    # the guess this map exists to avoid.
    #
    # ADDITIVE, never a replacement: a row can only add paths to a program the
    # record already covers, so a row that goes stale costs an unnecessary file
    # in a package and can never quietly stand in for a compile that moved.
    for n in sorted(extra):
        paths = []
        for x in extra[n]:
            f = os.path.join(OS, x)
            if not os.path.exists(f):
                continue
            paths.extend(whole(x) if os.path.isdir(f) else [x])
        if not paths:
            continue
        if n in have:
            have[n] = sorted(set(have[n]) | set(paths))
            lines = [(p, have[n] if p == n else v) for p, v in lines]
        else:
            have[n] = sorted(set(paths))
            lines.append((n, have[n]))
    lines.sort()

    short = sorted(set(published) - set(have))

    tmp = out + '.new'
    with open(tmp, 'w') as f:
        f.write("# .ulsrcmap -- which sources each published program is made "
                "of.  GENERATED by\n"
                "# hostbuild/ulsrcmap.py from the build's own record; see that "
                "file for what a\n"
                "# line names and why it names more than the .c files.  Paths "
                "are relative to\n"
                "# the repository root, one program per line, `#' comments.  Read by "
                "commodore-900-dist\n"
                "# (dist.py, SRCMAP) to cut a component's -src package.\n"
                "# A path written `+<path>' belongs in the program's source "
                "package and is not\n"
                "# something the program is built from: nothing is compiled "
                "from it and editing it\n"
                "# rebuilds nothing, so it is not a source an artifact can be "
                "behind.\n")
        f.write("# %d program(s) mapped, %d published program file(s) not "
                "mapped\n" % (len(lines), len(short)))
        for p, paths in lines:
            f.write("%s\t%s\n" % (p, " ".join(paths)))
    os.replace(tmp, out)

    if not quiet:
        print("== source map: %d program(s) in %s"
              % (len(lines), os.path.relpath(out, OS)))
        if short:
            print("== %d published file(s) with no mapped source: %s"
                  % (len(short), " ".join(short)))
        if unmapped:
            print("== %d record(s) whose every source is outside this tree: %s"
                  % (len(unmapped), " ".join(sorted(unmapped))))
        # An input the record could not be followed back through -- an object
        # this build did not compile (make found it up to date), or an archive
        # with no objects logged anywhere near it.  It is the one way this map
        # UNDER-states a program, so it is reported rather than counted as
        # resolved: the fix is a full build, not a wider guess.  A program the
        # supplement speaks for is not reported: an assembled object is
        # permanently invisible to the record and the row is the answer, not a
        # workaround for a stale build.
        external = dict((p, v) for p, v in external.items() if p not in extra)
        if external:
            print("== %d program(s) with an input no compile record covers "
                  "(their source is understated): %s"
                  % (len(external), " ".join(
                      "%s(%s)" % (p, os.path.basename(sorted(v)[0]))
                      for p, v in sorted(external.items()))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
