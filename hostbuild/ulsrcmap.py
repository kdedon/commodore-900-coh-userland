#!/usr/bin/env python3
"""ulsrcmap.py -- fold compiler records into build/.ulsrcmap.

    python3 ulsrcmap.py [-o OUT] [-q] [<record>]

Read C900_BUILD_MAP or build/compiled-programs.log by default.  Union records
across builds and include linked objects, recipes and the helpers they read,
libc and linked-library sources.  Carry small source directories whole; list
compiled files from large ones.  Prefix paths needed for source packages but
not binary freshness with "+".  Drop missing local paths; retain libc, which
this tree's -src packages carry; the toolchain's startup code and headers are
recorded by name (dist/pack-component.sh's `toolchain_src='), not carried."""

import os
import re
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

# THE SAME, IN THE TOOLCHAIN REPOSITORY.  The C library every program compiles
# against is built from src/libc over there, not from anything here, and a
# statically linked GPL program's corresponding source includes it.  So it is
# named, and named UNCONDITIONALLY -- unlike everything else in this file,
# which is dropped when the path is not in this tree.  Paths in the map are
# resolved by the consumer across the whole search path, so this finds the
# toolchain checkout; with none, a -src package refuses by name, which is the
# right answer, because a package cut without the library it links is not the
# complete corresponding source it claims to be.
#
# THE STARTUP CODE AND THE HEADERS ARE NOT HERE.  src/csu (linked into every
# program the same way src/libc is) and src/include are the toolchain's own,
# and the toolchain ships their source in its own -src package -- this map
# does not carry them as paths a consumer must find in THIS tree's search
# path, because a release-shaped toolchain checkout need not have unpacked
# its own -src package for this one to resolve.  What a -src package cut from
# this map records instead is the NAME of the toolchain source package a
# reader combines it with, not a path: dist/pack-component.sh writes
# `toolchain_src=toolchain' once into every -src package's .provenance, the
# same field component-bin.pkg already uses for `ulid' and `kernel_linkid' --
# a recorded fact, not a new refusal.
ELSEWHERE = ['src/libc']


# WHAT A RECIPE ITSELF READS.  A build script is source the same way a .c file
# is, and it does not stand alone: it sources the toolchain resolver, which
# runs the dependency search, and some scripts run a sibling first to make a
# parser generator or a library.  A package holding the script and not what the
# script reads holds a recipe that cannot run, so the closure below travels
# with every recipe the record names -- a file a script SOURCES or RUNS,
# transitively.
#
# THE CLOSURE STOPS AT ANOTHER RECIPE.  A script the record itself names as a
# recipe is carried for the programs it built and for no others, so the walk
# neither adds it nor descends into it: build-all-userland.sh runs three dozen
# siblings, and following them would put the editors' recipe in gzip's package
# and say nothing about what gzip needed.  A sibling that is nobody's recipe --
# the parser generator a compile runs first -- is a helper and travels.
#
# Read out of the script text, at the paths a script in this tree can spell:
# $OS is the repository root and $HERE the script's own directory, and a
# variable assigned from either carries the same resolution (`_c9deps=
# "$_c9root/mk/deps.sh"').  A path that resolves through anything else is not
# this tree's to name -- $TC is the toolchain -- and is left alone.
#
# MARKED (`+', see the header comment), like the entry point: what a stamp
# depends on is the recipe named by the record, so an edit to a helper rebuilds
# nothing and no artifact can be behind it.
ASSIGN = re.compile(r'^[ \t]*(\w+)=("[^"]*"|\'[^\']*\'|\S*)', re.M)
RUNS = re.compile(r'(?:^|[\s;&|(])(?:\.|source|sh|bash|ksh|python3?|perl)'
                  r'[ \t]+(?:-\S+[ \t]+)*("[^"]*"|\'[^\']*\'|\S+)')
VAR = re.compile(r'\$\{(\w+)\}|\$(\w+)')


def subst(tok, v):
    """`tok' with its shell variables expanded, or None if one is not known."""
    tok = tok.strip('"\'')
    out = VAR.sub(lambda m: v.get(m.group(1) or m.group(2), '\0'), tok)
    return None if '\0' in out or '$' in out or '`' in out else out


def helpers(path, recipes, _seen={}):
    """Every file this one sources or runs, transitively, relative to the root.

    `recipes' is every path the record names as a recipe; the walk stops at
    one, which is carried by the rows whose programs it built.
    """
    if path in _seen:
        return _seen[path]
    _seen[path] = out = set()
    try:
        text = open(path, errors='replace').read()
    except OSError:
        return out
    # Assignments first, so a command naming a variable resolves like one
    # naming the path outright.  Two passes: a path is commonly built from a
    # variable assigned above it.
    v = {'OS': OS, 'HERE': os.path.dirname(path)}
    for _ in range(2):
        for m in ASSIGN.finditer(text):
            r = subst(m.group(2), v)
            if r:
                v[m.group(1)] = r
    for line in text.split('\n'):
        line = re.sub(r'(^|\s)#.*', '', line)
        for m in RUNS.finditer(line):
            r = subst(m.group(1), v)
            if r is None:
                continue
            f = os.path.normpath(os.path.join(os.path.dirname(path), r))
            d = rel(f)
            if (d is None or not os.path.isfile(f) or d in recipes
                    or d.startswith(
                        os.path.join('hostbuild', 'build') + os.sep)):
                continue
            out.add(d)
            out |= helpers(f, recipes)
    out.discard(rel(path))
    return out


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
        """The source files behind a link's inputs.

        (in this tree, outside every record, declared in another repository).
        """
        srcs, ext, dec = set(), set(), set()
        for i in inputs:
            a = os.path.normpath(os.path.join(cwd, i))
            if a in seen:
                continue
            seen.add(a)
            if i.endswith('.o'):
                if a in objsrc:
                    s, x, d = expand(cwd, sorted(objsrc[a]), seen)
                    srcs |= s; ext |= x; dec |= d
                else:
                    ext.add(a)
            elif i.endswith('.a') and os.path.basename(a) in libmap:
                # A DECLARED library (ulsrcmap.libs) -- trusted ahead of the
                # object-record search below, which cannot tell an archive
                # nobody recompiled this run from one that was never near any
                # source at all.  A declared path this tree does not hold is
                # one ANOTHER REPOSITORY publishes, and is named exactly as
                # ELSEWHERE names the C library: unconditionally, for the
                # programs that link it, and resolved by the consumer over the
                # whole search path.
                for p in libmap[os.path.basename(a)]:
                    f = os.path.join(OS, p)
                    if os.path.exists(f):
                        srcs.add(os.path.normpath(f))
                    else:
                        dec.add(p)
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
        return srcs, ext, dec

    # One entry per program name.  A record whose output is an object or an
    # archive is not a program; everything else is, and is keyed by the name the
    # sweep renamed it to.
    progs, external = {}, {}
    for (cwd, o), e in sorted(inv.items()):
        if o.endswith('.o') or o.endswith('.a'):
            continue
        srcs, ext, dec = expand(cwd, e['in'], set())
        p = progname(o)
        d = progs.setdefault(p, {'src': set(), 'recipe': set(), 'dec': set()})
        d['src'] |= srcs
        d['dec'] |= dec
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

    # Every path the record names as a recipe, anywhere: what the helper walk
    # stops at, so a recipe reaches only the rows whose programs it built.
    allrecipes = set(rel(r) for q in progs.values() for r in q['recipe'])
    allrecipes.discard(None)

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
        paths |= set(rel(r) for r in progs[p]['recipe'])
        for r in sorted(progs[p]['recipe']):
            paths.update('+' + h for h in helpers(r, allrecipes))
        paths |= progs[p]['dec']
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
