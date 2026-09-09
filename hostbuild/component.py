#!/usr/bin/env python3
"""component.py -- this repository's file groups, resolved into packages.

    python3 hostbuild/component.py components
    python3 hostbuild/component.py component <name> <bin|src|man|dev>
    python3 hostbuild/component.py packages

A COMPONENT is a dist/lists/*.list carrying a `package' line: a named group of
the files this repository ships.  The list is the group's `bin' half, and the
other kinds are derived from it -- the source that discharges each binary's
licence, the manual pages for its programs, the compile-time interface.

WHY THE PRODUCER RESOLVES THEM.  This repository is the one that knows which
binaries it built, which sources they were compiled from (hostbuild/ulsrcmap.py)
and which manual pages it tracks for them.  A consuming repository that
re-derived any of that would be keeping a second, drifting copy of a fact only
the build has.  So the groups are resolved and packed here, and what crosses the
boundary is the finished archive.

The resolved set is emitted as text, one entry per line, because the format's
other intended consumer is an on-box installer walking it with sh and awk:
nothing may become the only thing that understands it.

WHERE A PATH IS LOOKED FOR.  Every path in a list is relative to this
repository's root (dist/FORMAT in commodore-900-dist).  A few lists name a
file another repository publishes -- the kernel's console drivers, the
toolchain's Z8001 cc -- so the root is a SEARCH PATH: this repository first,
then the checkouts named in $C900_OSPATH.  A component whose entries do not all
resolve refuses by name rather than packing a group with holes in it.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))               # hostbuild/
OS = os.path.dirname(HERE)                                      # repository root
DIST = os.path.join(OS, 'dist')
LISTS = os.path.join(DIST, 'lists')

def _toolchain():
    """The Z8001 toolchain checkout, or None.

    It is a search root and not just a compiler: the C library, the startup code
    and the headers every program here is compiled against and statically linked
    with are built from src/{libc,csu,include} over there, and the source map
    names them, because a statically linked GPL program's corresponding source
    includes them.  Resolved through this repository's own dependency resolver,
    which is what every other consumer of the toolchain asks.
    """
    p = os.environ.get('C900_TOOLCHAIN')
    if not p:
        r = subprocess.run(['sh', os.path.join(OS, 'mk', 'deps.sh'), 'toolchain'],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        p = r.stdout.decode().strip()
    return p if p and os.path.isdir(p) else None


def _kernel():
    """The kernel repository's os/ view, or None.

    Two things of this tree's reach over there: the console drivers a few lists
    install (build/drv on the kernel side) and the lists that DEFINE the
    console components, which lists here `include' -- install.list draws all
    three through console-both.list.  Resolved through this repository's own
    dependency resolver; the os/ view is where its descriptor paths land.
    """
    p = os.environ.get('C900_KERNEL')
    if not p:
        r = subprocess.run(['sh', os.path.join(OS, 'mk', 'deps.sh'), 'kernel'],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        p = r.stdout.decode().strip()
    if p and os.path.isdir(os.path.join(p, 'os', 'hostbuild')):
        return os.path.join(p, 'os')
    return p if p and os.path.isdir(p) else None


# THIS REPOSITORY IS ALWAYS FIRST, so a file it owns can never be shadowed.
# After it come the checkouts $C900_OSPATH names, then the kernel -- for the
# console drivers a few lists install and the lists that define them -- and the
# toolchain, for the library sources every -src package must carry.  Each root
# once, in that order.
OSPATH = []
for _d in ([OS] + [d for d in os.environ.get('C900_OSPATH', '').split(':') if d]
           + [d for d in [_kernel(), _toolchain()] if d]):
    if _d not in OSPATH:
        OSPATH.append(_d)

# The manual this repository assembles from the pages every component tracks
# (hostbuild/extract-man.sh, `make -C hostbuild man').  man.index is its table of
# contents -- `<section>/<page>\t<name>\t<description>', the same file /bin/man
# scans on the machine -- and a component's -man package is the pages for what
# that component BUILDS: the rows whose name is one of its programs, and the
# articles for the libraries its `libraries' line names.
MANTREE = 'hostbuild/build/man'

# The published map from a shipped program to the sources it was compiled from,
# written by hostbuild/ulsrcmap.py from the build's own record.  Without it no
# -src package can be cut, and every one of them refuses by name: a source
# package assembled from a guess would be a licence promise nobody checked.
SRCMAP = 'hostbuild/build/.ulsrcmap'

PKGDIRECTIVES = ('package', 'role', 'conflict', 'kinds', 'system', 'userland',
                 'articles',
                 'libraries', 'dev')
PKGROLES = ('base', 'optional', 'alternative', 'variant')
PKGKINDS = ('bin', 'src', 'man', 'dev')

# The roots a list entry with no src= is looked for in: dist/userlands/*.uland.
ULKEYS = ('drv', 'bin', 'root')
DEFAULT_ULAND = 'coherent'


def die(msg):
    sys.exit("component: " + msg)


def oscands(rel):
    """Every place a list path could be, in search order."""
    return [os.path.join(d, rel) for d in OSPATH]


def osp(rel):
    """The first root of the search path that HAS `rel'; else this repository's
    own spelling of it, so a caller's message names something."""
    for c in oscands(rel):
        if os.path.exists(c):
            return c
    return os.path.join(OS, rel)


def read_kv(path, listkeys=()):
    out = {}
    if not os.path.exists(path):
        die("no such file: %s" % path)
    for lno, line in enumerate(open(path), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        f = line.split(None, 1)
        if len(f) != 2:
            die("%s:%d: expected `key value': %s" % (path, lno, line))
        k, v = f[0], f[1].strip()
        if k in listkeys:
            out.setdefault(k, []).append(v)
        elif k in out:
            die("%s:%d: duplicate key %s" % (path, lno, k))
        else:
            out[k] = v
    return out


def version():
    """The release string, from this repository's own version script."""
    r = subprocess.run(['sh', os.path.join(HERE, 'version.sh')],
                       stdout=subprocess.PIPE)
    v = r.stdout.decode().strip()
    if r.returncode != 0 or not v:
        die("hostbuild/version.sh named no version (exit %d)" % r.returncode)
    return v


def listpath(rel):
    """dist/<rel>: this repository's own first, else the first root of the
    search path that has it.  The COMPONENTS are this repository's own -- a
    component is a group of ITS files, and listnames() reads only dist/lists --
    but a list may `include' one another producer defines: install.list draws
    the three console drivers through console-both.list, and those three lists
    are the kernel repository's, beside the drivers it links."""
    own = os.path.join(DIST, rel)
    if os.path.exists(own):
        return own
    for c in oscands(os.path.join('dist', rel)):
        if os.path.exists(c):
            return c
    return own


def listnames():
    return sorted('lists/' + n for n in os.listdir(LISTS) if n.endswith('.list'))


def list_meta(rel):
    """The package directives at the head of dist/<rel>, syntax-checked."""
    meta = {'list': rel, 'package': None, 'role': None, 'conflict': [],
            'kinds': None, 'system': None, 'userland': None,
            'libraries': None, 'dev': [], 'articles': {}}
    for lno, line in enumerate(open(listpath(rel)), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        f = line.split()
        if f[0] not in PKGDIRECTIVES:
            continue
        where = "%s:%d" % (rel, lno)
        # `kinds' is the one directive with a list on the right: a component
        # publishes several packages, and naming them one per line would let two
        # of those lines disagree about the same component.
        if f[0] == 'kinds':
            if len(f) < 2:
                die("%s: `kinds' names no kind (%s)" % (where, "|".join(PKGKINDS)))
            if meta['kinds'] is not None:
                die("%s: a second `kinds' line" % where)
            meta['kinds'] = f[1:]
            continue
        # THE LINK-TIME LIBRARIES THIS COMPONENT BUILDS, by the name the manual
        # files their articles under.  A library's pages document something the
        # component ships, so they belong in its -man package exactly as its
        # programs' pages do; without this line the whole of libsocket's manual
        # -- twenty-six articles -- would be in no package at all.  One line, for
        # the same reason `kinds' is one line.
        if f[0] == 'libraries':
            if len(f) < 2:
                die("%s: `libraries' names no library" % where)
            if meta['libraries'] is not None:
                die("%s: a second `libraries' line" % where)
            for l in f[1:]:
                if not re.fullmatch(r'lib[a-z0-9_]+', l):
                    die("%s: `%s' is not a library name; the manual files an "
                        "article under the archive's name without its suffix "
                        "(libsocket, libmp)" % (where, l))
            meta['libraries'] = f[1:]
            continue
        # THE COMPILE-TIME INTERFACE, one line per file or directory:
        #
        #	dev <path it installs at> <path in a checkout>
        #
        # It is a directive and not an entry because none of it goes on an
        # image: -dev is what somebody unpacks on a machine that will COMPILE
        # against this component, and the -bin list is what runs.
        # `articles' names the Lexicon articles documenting one of this
        # component's programs under a name other than the program's own:
        # `articles <program> <article>...', one line per program.  A loadable
        # driver is the case -- /drv/hrtty is documented by console and kb, the
        # device it provides and the keyboard it reads, and no page is named
        # hrtty -- so its -man package carries those pages and the program
        # counts as documented by them (Component.man).
        if f[0] == 'articles':
            if len(f) < 3:
                die("%s: `articles' takes <program> <article>..." % where)
            if f[1] in meta['articles']:
                die("%s: a second `articles' line for %s" % (where, f[1]))
            meta['articles'][f[1]] = f[2:]
            continue
        if f[0] == 'dev':
            if len(f) != 3:
                die("%s: `dev' takes <install path> <source path>" % where)
            if not f[1].startswith('/'):
                die("%s: dev install path must be absolute: %s" % (where, f[1]))
            meta['dev'].append((f[1], f[2]))
            continue
        if len(f) != 2:
            die("%s: `%s' takes exactly one value" % (where, f[0]))
        if f[0] == 'conflict':
            meta['conflict'].append(f[1])
        elif meta[f[0]] is not None:
            die("%s: a second `%s' line" % (where, f[0]))
        else:
            meta[f[0]] = f[1]
    return meta


def meta_kinds(m):
    """The kinds this component publishes.  `bin' is every component's and is
    not written down; a `kinds' line names the OTHERS."""
    k = ['bin']
    for x in (m['kinds'] or []):
        if x not in k:
            k.append(x)
    return k


def all_components():
    return sorted(m['package'] for m in [list_meta(n) for n in listnames()]
                  if m['package'])


def packages(out, only=None):
    """Every list that IS a package, validated.  The list is the unit: a list
    without a `package' line is not a package, which is how dist/lists/
    testing.list -- probes, development images only -- stays out of a release
    channel by construction rather than by a rule somebody has to remember."""
    metas, byname = [], {}
    for rel in listnames():
        m = list_meta(rel)
        if not m['package']:
            continue
        if m['role'] is None:
            die("%s: `package %s' with no `role' (%s)"
                % (rel, m['package'], "|".join(PKGROLES)))
        if m['role'] not in PKGROLES:
            die("%s: role `%s' is not one of %s"
                % (rel, m['role'], "|".join(PKGROLES)))
        for k in (m['kinds'] or []):
            if k not in PKGKINDS:
                die("%s: `kinds' names `%s' (%s)"
                    % (rel, k, "|".join(PKGKINDS)))
            if k == 'bin':
                die("%s: `kinds' names `bin', which every component publishes "
                    "and none declares" % rel)
        # A DECLARATION THAT DOES NOTHING IS THE ONE NOBODY NOTICES IS WRONG.
        # `libraries' feeds the -man package and `dev' IS the -dev package, so
        # each must be paired with the kind it feeds, both ways: a `dev' line
        # under a component that publishes no `dev' packs nothing, and a `dev'
        # kind with no line would refuse at pack time in a sweep that reports
        # refusals and goes on.
        if m['libraries'] and 'man' not in (m['kinds'] or []):
            die("%s: `libraries' names %s and the component publishes no `man' "
                "package, so those articles would go nowhere"
                % (rel, " ".join(m['libraries'])))
        if m['articles'] and 'man' not in (m['kinds'] or []):
            die("%s: `articles' names pages for %s and the component publishes "
                "no `man' package, so those articles would go nowhere"
                % (rel, ", ".join(sorted(m['articles']))))
        if m['dev'] and 'dev' not in (m['kinds'] or []):
            die("%s: `dev' names %d file(s) and the component's `kinds' line "
                "does not say `dev'" % (rel, len(m['dev'])))
        if 'dev' in (m['kinds'] or []) and not m['dev']:
            die("%s: `kinds' says `dev' and no `dev' line names a header or a "
                "library.  A -dev package is that interface; there is nothing "
                "else for it to hold." % rel)
        if m['package'] in PKGKINDS:
            die("%s: a component may not be named after a kind (`%s'), because "
                "`%s-src' would then name two things" % (rel, m['package'],
                                                         m['package']))
        if m['package'] in byname:
            die("%s: package `%s' is already declared by %s"
                % (rel, m['package'], byname[m['package']]['list']))
        byname[m['package']] = m
        metas.append(m)
    # A `conflict' names a LIST, and both directions must be stated: a pair
    # where only one side says so installs together whenever the other is
    # chosen first.
    for m in metas:
        for c in m['conflict']:
            if not os.path.exists(listpath(c)):
                die("%s: conflict names %s, which is not a list"
                    % (m['list'], c))
            o = list_meta(c)
            if not o['package']:
                die("%s: conflict names %s, which is not a package"
                    % (m['list'], c))
            if m['list'] not in o['conflict']:
                die("%s conflicts with %s and %s does not say so.  A conflict "
                    "is a fact about a pair and both sides state it."
                    % (m['list'], c, c))
    for m in metas if only is None else [byname[only]] if only in byname else []:
        out.write("%-16s %-12s %-14s %s\n"
                  % (m['package'], m['role'], ",".join(meta_kinds(m)),
                     m['list']))
    if only is not None and only not in byname:
        die("no package `%s'" % only)
    out.write("-- %d package(s)\n" % len(metas))


class Entry:
    __slots__ = ('type', 'dest', 'mode', 'uid', 'gid', 'keys', 'origin')

    def __init__(self, type, dest, mode, uid, gid, keys, origin):
        self.type, self.dest, self.origin = type, dest, origin
        self.mode, self.uid, self.gid, self.keys = mode, uid, gid, keys


def read_list(rel, seen, entries):
    """Splice dist/<rel> into `entries', following `include' directives."""
    path = listpath(rel)
    if rel in seen:
        die("include cycle on %s" % rel)
    seen.add(rel)
    if not os.path.exists(path):
        die("no such list: %s" % rel)
    for lno, line in enumerate(open(path), 1):
        line = line.split('#')[0].strip()
        if not line:
            continue
        f = line.split()
        origin = "%s:%d" % (rel, lno)
        if f[0] == 'include':
            read_list(f[1], seen, entries)
            continue
        if f[0] == 'require':
            # A required list is a fact about a DISTRIBUTION -- which other
            # group must be installed beside this one -- and is checked when a
            # distribution is resolved, not when a group is packed.
            if len(f) != 2:
                die("%s: require takes one list" % origin)
            if not os.path.exists(listpath(f[1])):
                die("%s: no such list: %s" % (origin, f[1]))
            continue
        if f[0] in PKGDIRECTIVES:
            continue
        if f[0] not in ('d', 'f', 'e', 'l', 't'):
            die("%s: unknown entry type `%s'" % (origin, f[0]))
        if len(f) < 2:
            die("%s: entry has no path" % origin)
        typ, dest = f[0], f[1]
        if not dest.startswith('/'):
            die("%s: path must be absolute: %s" % (origin, dest))
        rest = f[2:]
        mode = uid = gid = None
        plain = [x for x in rest if '=' not in x]
        if plain:
            if len(plain) != 3 or plain != rest[:3]:
                die("%s: expected `mode uid gid' (all three, before any "
                    "key=value): %s" % (origin, ' '.join(rest)))
            mode, uid, gid = int(plain[0], 8), int(plain[1]), int(plain[2])
        keys = {}
        for kv in rest[len(plain):]:
            k, _, v = kv.partition('=')
            if k not in ('src', 'part', 'from', 'to'):
                die("%s: unknown key `%s'" % (origin, k))
            keys[k] = v
        # A hard link is a SECOND NAME for a file another entry places -- one
        # inode, link count 2 -- so mode, owner and data belong to the `to='
        # entry and stating them here would state them twice.
        if typ == 'l':
            if 'to' not in keys:
                die("%s: `l' needs to=<path already placed>" % origin)
            if mode is not None:
                die("%s: `l' takes no mode/uid/gid -- a hard link shares the "
                    "inode of %s, and its permissions with it"
                    % (origin, keys['to']))
            if 'src' in keys:
                die("%s: `l' takes no src= -- its content is %s's"
                    % (origin, keys['to']))
        elif 'to' in keys:
            die("%s: to= is only for `l' entries" % origin)
        entries.append(Entry(typ, dest.rstrip('/') or '/', mode, uid, gid,
                             keys, origin))


def man_index():
    """{name: [section/page, ...]} from the assembled manual, or {}."""
    p = osp(os.path.join(MANTREE, 'man.index'))
    if not os.path.exists(p):
        return {}
    out = {}
    for line in open(p, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 2 and f[0] and f[1]:
            out.setdefault(f[1], []).append(f[0])
    return out


# ---- WHICH LIBRARY A MANUAL PAGE DOCUMENTS -----------------------------------
# Read from THE PAGES THEMSELVES, because the manual already says it and a table
# kept here would be a second copy of that with nothing keeping it level.  A
# Lexicon article's first line is its title and its class, and a library
# function's class names the library it is in:
#
#	accept() -- Sockets Function (libsocket)
#	madd() -- Multiple-Precision Mathematics (libmp)
#	libsocket -- Overview
#
# Three statements are read, in that order of authority, and each is the
# manual's own:
#
#   THE CLASS'S PARENTHESIS names the library outright.  `(libterm/libcurses)'
#   names two, and the page belongs to both.
#   A LIBRARY'S OWN ARTICLE (class `Library' or `Overview', titled libX) is that
#   library's, and the roster it prints -- `accept()....Accept a connection' --
#   is its statement of what it holds.  A rostered name whose page names a
#   DIFFERENT library keeps that library: libmp's roster lists pow(), and
#   COHERENT.2/pow is libmp's page while pow.m is libm's, so the two never
#   change hands.
#   A CLASS WHOSE OTHER MEMBERS ALL NAME ONE LIBRARY carries a member that
#   names none: tgetflag() is `termcap Function' where its four siblings are
#   `termcap Function (libterm)'.  Overview articles are neither evidence nor
#   subject here -- fifteen unrelated articles share the class `Overview' --
#   so the sweep is over pages whose own parenthesis spoke.
#
# Everything else is unattributed and is packed by nobody, which is the same
# rule an article for a program no component installs already lives under.
LIBNAME = re.compile(r'lib[a-z0-9_]+$')


def _deov(s):
    """A rendered Lexicon line with its overstrike removed.  The pages are
    nroff output -- `a\ba' for bold, `_\ba' for italic -- so a title read as
    bytes is `aacccceepptt()' and matches nothing."""
    return re.sub('.\x08', '', s)


def _man_headers():
    """{page: its title line, de-overstruck}."""
    out = {}
    for sect in ('COHERENT.1', 'COHERENT.2'):
        d = osp(os.path.join(MANTREE, sect))
        if not os.path.isdir(d):
            continue
        for n in sorted(os.listdir(d)):
            f = os.path.join(d, n)
            if not os.path.isfile(f):
                continue
            with open(f, errors='replace') as fp:
                out[sect + '/' + n] = _deov(fp.readline().rstrip('\n'))
    return out


def _man_class(h):
    """(class, [library named in its parenthesis]) for a title line."""
    cls = h.split(' -- ', 1)[1].strip() if ' -- ' in h else ''
    libs = []
    m = re.search(r'\(([^()]*)\)$', cls)
    if m:
        cls = cls[:m.start()].strip()
        libs = [x for x in m.group(1).split('/') if LIBNAME.match(x)]
    return cls, libs


def _roster(page):
    """The function names a library's own article lists.

        gcd()..........Set variable to greatest common divisor
    """
    out = []
    p = osp(os.path.join(MANTREE, page))
    for line in open(p, errors='replace'):
        m = re.match(r'([A-Za-z_][A-Za-z0-9_]*)\(\)\.{2,}', _deov(line).strip())
        if m and m.group(1) not in out:
            out.append(m.group(1))
    return out


def man_libraries(idx=None):
    """{library: [page]} -- the manual, read for what each page documents."""
    idx = man_index() if idx is None else idx
    hdr = _man_headers()
    if not hdr:
        return {}
    lib = {}                    # page -> [library]

    def claim(page, l):
        if l not in lib.setdefault(page, []):
            lib[page].append(l)

    cls = {}
    for page, h in hdr.items():
        c, libs = _man_class(h)
        cls[page] = c
        for l in libs:
            claim(page, l)
    # A library's own article, and the roster it prints.
    for page, h in sorted(hdr.items()):
        name = os.path.basename(page)
        if not LIBNAME.match(name) or cls[page] not in ('Library', 'Overview'):
            continue
        claim(page, name)
        for fn in _roster(page):
            for key in (fn + '()', fn):
                if key in idx:
                    for pg in idx[key]:
                        if pg in hdr and not lib.get(pg):
                            claim(pg, name)
                    break
    # A class whose members unanimously name one library, over the pages whose
    # own parenthesis spoke.
    spoke = {}
    for page, h in hdr.items():
        _, libs = _man_class(h)
        if libs:
            spoke.setdefault(cls[page], set()).update(libs)
    for page in hdr:
        s = spoke.get(cls[page])
        if s and len(s) == 1 and page not in lib:
            claim(page, next(iter(s)))
    out = {}
    for page, ls in lib.items():
        for l in ls:
            out.setdefault(l, []).append(page)
    return {l: sorted(v) for l, v in out.items()}


def srcmap():
    """{binary: [source path relative to the repository root]}, or None.

    MERGED ACROSS EVERY ROOT of the search path rather than taken from the
    first that has one: this repository publishes the programs it builds, the
    kernel repository the console drivers it builds, and a component may draw
    from both.  First-wins would make the second map invisible and the failure
    would look like the drivers being unmapped.

    A `+' marks a path the program is not built from and a -src package still
    owes (ulsrcmap.py, THE `+' MARKER): the mark is dropped here and the path
    kept, because complete corresponding source is what the licence asks for
    and it does not ask what a rebuild would touch.
    """
    out, found = {}, False
    for p in oscands(SRCMAP):
        if not os.path.exists(p):
            continue
        found = True
        for line in open(p):
            line = line.split('#')[0].split()
            if len(line) >= 2:
                for rel in line[1:]:
                    rel = rel[1:] if rel.startswith('+') else rel
                    if rel not in out.setdefault(line[0], []):
                        out[line[0]].append(rel)
    return out if found else None


class Component:
    """One component: its list, resolved, and the kinds it publishes."""

    def __init__(self, name):
        for rel in listnames():
            m = list_meta(rel)
            if m['package'] == name:
                break
        else:
            die("no component `%s' (component.py components lists them)" % name)
        self.name = name
        self.meta = m
        self.kinds = meta_kinds(m)
        self.uland = m['userland'] or DEFAULT_ULAND
        self.roots = read_kv(os.path.join(DIST, 'userlands',
                                          self.uland + '.uland'))
        missing = [k for k in ULKEYS if k not in self.roots]
        if missing:
            die("%s: userlands/%s.uland names no %s; those are the roots a "
                "list entry with no src= is looked for in"
                % (name, self.uland, ", ".join(missing)))
        self.entries = []
        read_list(rel, set(), self.entries)

    # ---- where an entry's content comes from ---------------------------------
    def _src(self, e):
        if 'src' in e.keys:
            p = osp(e.keys['src'])
            return p if os.path.exists(p) else None
        if e.type in ('d', 'e', 'l'):
            return None
        rel = e.dest.lstrip('/')
        for c in ([os.path.join(DIST, 'files', rel),
                   os.path.join(DIST, 'files', rel + '.in')] +
                  oscands(os.path.join(self.roots['root'], rel)) +
                  oscands(os.path.join(self.roots['drv'], os.path.basename(rel))) +
                  oscands(os.path.join(self.roots['bin'], os.path.basename(rel)))):
            if os.path.exists(c):
                return c
        return None

    def files(self):
        return [(e.dest, self._src(e)) for e in self.entries
                if e.type in ('f', 't')]

    def unresolved(self):
        """Entries whose content this machine cannot find.  A component resolved
        against a tree that has not been built answers every question with a
        zero, and a zero is indistinguishable from a clean result."""
        return [e.dest for e in self.entries
                if e.type in ('f', 't') and self._src(e) is None]

    def mode_of(self, e, src):
        """The mode the image packer will give this entry: the declared one,
        else 755 for a source file with the x bit and 644 otherwise.  Stated
        once, here, so a package installs a program with the bits the image
        gives it."""
        if e.mode is not None:
            return e.mode
        if src and os.path.isfile(src) and os.access(src, os.X_OK):
            return 0o755
        return 0o644

    def _link_target(self, e):
        want = e.keys.get('to')
        for o in self.entries:
            if o.dest == want:
                return o
        return None

    # ---- the executables, which is what the other kinds are about ------------
    def executables(self):
        """[(dest, source, map-name)] -- every program this component installs.

        A `t' entry is EXPANDED: /usr/games is one line and thirty-four
        programs, and while this walked entries rather than files the games
        component reported two programs and full source coverage with ttycity's
        GPLv3 terms in no package at all.
        """
        out = []
        for e in self.entries:
            if e.type == 'l':
                # A hard link's own entry carries no mode -- the inode and its
                # permissions belong to the `to=' entry -- so the target's mode
                # is what says whether this name is a program.  /usr/bin/vi,
                # /bin/[ and /usr/bin/emacs are exactly this.
                tgt = self._link_target(e)
                src = self._src(tgt) if tgt else None
                mode = self.mode_of(tgt, src) if tgt else self.mode_of(e, None)
                if mode & 0o111:
                    out.append((e.dest, src, self.srcname(e)))
            elif e.type == 'f':
                src = self._src(e)
                if self.mode_of(e, src) & 0o111:
                    out.append((e.dest, src, self.srcname(e)))
            elif e.type == 't':
                src = self._src(e)
                if src and os.path.isdir(src):
                    for n in sorted(os.listdir(src)):
                        s = os.path.join(src, n)
                        if os.path.isfile(s) and os.access(s, os.X_OK):
                            out.append((e.dest.rstrip('/') + '/' + n, s, n))
        return out

    def programs(self):
        return {os.path.basename(d): d for d, _, _ in self.executables()}

    def srcname(self, e):
        """The name to look this entry up in the source map under: the basename
        of the SOURCE, not of the destination.  Eight programs ship under a name
        nothing built -- /bin/[ is test, emacs is me, sb and sx are sz, rb and
        rx are rz, del_route is add_route, gzcat is gzip -- and in every case the
        list entry already says so."""
        if e.type == 'l':
            tgt = self._link_target(e)
            if tgt:
                return self.srcname(tgt)
        s = self._src(e)
        return os.path.basename(s) if s else os.path.basename(e.dest)

    def osrel(self, p):
        """A resolved absolute path, back in the root-relative spelling the
        lists and the source map use.  Empty if it is under no root."""
        a = os.path.abspath(p)
        for r in OSPATH:
            r = os.path.abspath(r) + os.sep
            if a.startswith(r):
                return a[len(r):]
        return ''

    def is_build_output(self, p):
        """Is this a BUILT file rather than a source file?  Answered from the
        roots a build writes into, not by guessing from the path."""
        rel = self.osrel(p)
        return bool(rel) and any(
            rel.startswith(self.roots[k].rstrip('/') + '/') for k in ULKEYS)

    def is_text(self, p):
        """A file with no NUL in its first 512 bytes.  The guard on identity
        mapping: net/inet/inet is a linked binary sitting beside its own
        sources, and shipping a binary as its own corresponding source would be
        the licence claim this mechanism exists to make checkable."""
        try:
            with open(p, 'rb') as f:
                return b'\0' not in f.read(512)
        except OSError:
            return False

    def own_source(self, s):
        """Content that IS its own source: a distribution overlay file under
        dist/files.  /etc/rc, /etc/rc.net, /etc/reboot, /etc/shutdown and
        install's /build are shell scripts, and a shell script is its own
        source, so they are identity-mapped and never looked up."""
        if not s:
            return None
        if os.path.abspath(s).startswith(os.path.join(DIST, 'files') + os.sep):
            return self.osrel(s) or os.path.relpath(os.path.abspath(s), OS)
        return None

    def own_source_late(self, s):
        """The same rule for a source file the map has no row for because
        nothing compiled it: /bin/diff3 is base/cmd/diff3.sh and /bin/spell is
        base/cmd/spell/spellcmd.  Guarded by is_build_output and is_text, so a
        linked binary living in a source tree cannot pass itself off as one."""
        if not s or not os.path.isfile(s):
            return None
        if self.is_build_output(s) or not self.is_text(s):
            return None
        return self.osrel(s) or None

    def src(self, sm):
        """([source path], [program with no source named]).

        One place, so the coverage count and the packed archive can never
        disagree about what is covered.  In order: an overlay file is its own
        source; then the map, keyed on the name the program was BUILT as; then
        a source file the map has no row for; otherwise it is unmapped and is
        REPORTED rather than guessed at.
        """
        rels, missing = [], []
        for dest, s, n in self.executables():
            got = self.own_source(s)
            if not got:
                if n in (sm or {}):
                    for rel in sm[n]:
                        if rel not in rels:
                            rels.append(rel)
                    continue
                got = self.own_source_late(s)
            if got:
                if got not in rels:
                    rels.append(got)
                continue
            base = os.path.basename(dest)
            missing.append(base if n == base else "%s (as %s)" % (base, n))
        return rels, missing

    def man(self, idx=None):
        """({program: [section/page]}, [program with no page]).

        A program's pages are the index rows under its own name, and the rows
        of the articles its list declares for it (`articles <program>
        <article>...'): a driver is documented under its device.  A declared
        article the index has no row for contributes nothing, so the program
        counts as undocumented and the ratchet sees it; the producer's packer
        is what refuses to cut a -man package over a page it does not have.
        Articles declared for a program the component does not install are
        refused: the list would be documenting something it does not ship.
        """
        idx = man_index() if idx is None else idx
        have, miss = {}, []
        progs = self.programs()
        arts = self.meta['articles']
        for p in sorted(arts):
            if p not in progs:
                die("%s: %s declares articles for `%s', which it does not "
                    "install" % (self.name, self.meta['list'], p))
        for p in sorted(progs):
            pages = list(idx.get(p, []))
            for a in arts.get(p, []):
                pages += [pg for pg in idx.get(a, []) if pg not in pages]
            if pages:
                have[p] = pages
            else:
                miss.append(p)
        return have, miss

    def libraries(self):
        """The link-time libraries this component builds, by article name."""
        return list(self.meta['libraries'] or [])

    def libman(self, libidx=None):
        """{library: [section/page]} for the libraries this component builds.

        A LIBRARY IS DOCUMENTED WHERE IT IS BUILT, on the same rule its
        programs are: net builds libsocket and its twenty-six articles are
        net's, base/lib builds libmp and libcurses, and libc's are the
        toolchain's because the toolchain builds it.  A declared library the
        manual has no article for -- liby, libfs -- contributes nothing and is
        not an error: the Lexicon never described them.
        """
        libidx = man_libraries() if libidx is None else libidx
        return {l: libidx[l] for l in self.libraries() if libidx.get(l)}

    def manpages(self, idx=None, libidx=None):
        """Every page this component's -man package holds, deduplicated."""
        have, _ = self.man(idx)
        pages = []
        for src in (have, self.libman(libidx)):
            for k in sorted(src):
                for pg in src[k]:
                    if pg not in pages:
                        pages.append(pg)
        return pages


def component_kind(name, kind, out):
    """One component-kind's file set, one entry per line:

        <type> <path in the package> <mode> <uid> <gid> <source>

    the same six columns for every kind, so the packer reads one format.
    `source' is an absolute path on this machine, the `to=' target for a hard
    link, or `-' for a file the packer generates.

    A bin package's paths are the TARGET paths (/bin/ls), because that is what
    installing it means; a man package's are `<section>/<page>' as man.index
    spells them; a src package's are relative to this repository's root.
    """
    def emit(typ, path, mode, src):
        out.write("%s\t%s\t%o\t0\t1\t%s\n" % (typ, path, mode, src))
    if kind not in PKGKINDS:
        die("no kind `%s' (%s)" % (kind, "|".join(PKGKINDS)))
    c = Component(name)
    if kind not in c.kinds:
        die("component `%s' does not publish a `%s' package.  Its kinds are "
            "%s; add `%s' to the `kinds' line of %s to change that."
            % (name, kind, ",".join(c.kinds), kind, c.meta['list']))
    if kind == 'bin':
        miss = c.unresolved()
        if miss:
            die("%s: %d entr(y|ies) resolve to nothing, first %s.\n"
                "  A bin package cut from a half-built tree is a package of "
                "holes; build first (`make -C hostbuild')."
                % (name, len(miss), miss[0]))
        for e in c.entries:
            src = c._src(e)
            if e.type == 'd':
                emit('d', e.dest, e.mode if e.mode is not None else 0o755, '-')
            elif e.type == 'e':
                emit('e', e.dest, e.mode if e.mode is not None else 0o644, '-')
            elif e.type == 'l':
                emit('l', e.dest, 0, e.keys['to'])
            elif e.type == 'f':
                emit('f', e.dest, c.mode_of(e, src), src)
            elif e.type == 't':
                # A tree is DIRECTORY CONTENTS, and a package is a list of
                # files: expanded here so what the package carries is nameable
                # rather than "whatever that directory held".
                emit('d', e.dest, 0o755, '-')
                for n in sorted(os.listdir(src)):
                    s = os.path.join(src, n)
                    if os.path.isfile(s):
                        emit('f', e.dest.rstrip('/') + '/' + n,
                             0o755 if os.access(s, os.X_OK) else 0o644, s)
        return
    if kind == 'man':
        idx = man_index()
        if not idx:
            die("%s-man: no %s/man.index -- the manual is assembled from the "
                "pages every component tracks (`make -C hostbuild man'), and a "
                "man package cut without it would be empty and pass"
                % (name, MANTREE))
        have, miss = c.man(idx)
        pages = c.manpages(idx)
        if not pages:
            die("%s-man: not one of this component's %d program(s) has a page "
                "and it builds no\n  documented library, so there is no manual "
                "package to cut -- an empty archive\n  that unpacked and passed "
                "would read as coverage.  Owed: %s"
                % (name, len(miss), " ".join(miss[:8]) +
                   (" ..." if len(miss) > 8 else "")))
        for page in pages:
            p = osp(os.path.join(MANTREE, page))
            if not os.path.exists(p):
                die("%s-man: man.index names %s and the tree has no such "
                    "page" % (name, page))
            emit('f', page, 0o644, p)
        # The index rows for exactly these pages.  /bin/man scans man.index a
        # line at a time and finds nothing that is not in it, so a page shipped
        # without its row is a file the machine cannot reach.  The packer
        # generates it from the rows above, which is why the pages come first.
        emit('f', 'man.index', 0o644, '-')
        return
    if kind == 'src':
        sm = srcmap()
        if sm is None:
            die("%s-src: no source map at %s.\n"
                "  A -src package is how the licence obligations the BINARIES "
                "create are\n  discharged (gzip GPLv2, rcs GPLv1, ttycity "
                "GPLv3+s.7), so it may not be\n  assembled from a guess about "
                "which sources built what.  `make -C hostbuild\n  publish' "
                "writes it." % (name, SRCMAP))
        rels, missing = c.src(sm)
        if missing:
            die("%s-src: %d of %d program(s) are not in %s: %s\n"
                "  Every executable this component installs must have its "
                "corresponding source\n  named, or the package understates what "
                "was shipped." % (name, len(missing), len(c.programs()), SRCMAP,
                                  " ".join(sorted(missing))))
        for rel in sorted(rels):
            s = osp(rel)
            if not os.path.exists(s):
                die("%s-src: %s names %s, which is not in any checkout this "
                    "machine resolves" % (name, SRCMAP, rel))
            if os.path.isdir(s):
                # A source ROOT: the whole directory travels, licence text and
                # build recipe with it, because `complete corresponding source'
                # is not the .c files alone.
                for base, _, files in os.walk(s):
                    for n in sorted(files):
                        f = os.path.join(base, n)
                        emit('f', os.path.join(rel, os.path.relpath(f, s)),
                             0o755 if os.access(f, os.X_OK) else 0o644, f)
            else:
                emit('f', rel, 0o755 if os.access(s, os.X_OK) else 0o644, s)
        return
    if kind == 'dev':
        # THE INTERFACE, AS THE LIST DECLARES IT: `dev <install path> <source
        # path>', a header, a directory of headers, or a link-time library.  A
        # source directory travels whole, under the install path, because a
        # header that includes another one is useless without it.
        dirs, files = [], []
        for dest, rel in c.meta['dev']:
            src = osp(rel)
            if not os.path.exists(src):
                die("%s-dev: %s names %s, which is not in any checkout this "
                    "machine resolves.\n  A -dev package short of a header is "
                    "an interface nothing can compile against."
                    % (name, c.meta['list'], rel))
            if os.path.isdir(src):
                dirs.append(dest)
                for base, _, ns in os.walk(src):
                    sub = os.path.relpath(base, src)
                    d = dest if sub == '.' else os.path.join(dest, sub)
                    if d not in dirs:
                        dirs.append(d)
                    for n in sorted(ns):
                        files.append((os.path.join(d, n), os.path.join(base, n)))
            else:
                files.append((dest, src))
        # Every directory the files land in, parents first: manifest.tab is an
        # installation instruction, and an installer that mkdir's what it is
        # told cannot place /usr/include/net/gen/in.h without being told about
        # /usr/include.
        for dest, _ in files:
            d = os.path.dirname(dest)
            while d not in ('/', ''):
                if d not in dirs:
                    dirs.append(d)
                d = os.path.dirname(d)
        seen = set()
        for d in sorted(dirs):
            emit('d', d, 0o755, '-')
        for dest, src in files:
            if dest in seen:
                die("%s-dev: two `dev' lines install %s" % (name, dest))
            seen.add(dest)
            emit('f', dest, 0o755 if os.access(src, os.X_OK) else 0o644, src)
        return


def components(out):
    """One line per component: what it publishes, and what it is short of."""
    idx = man_index()
    libidx = man_libraries(idx)
    sm = srcmap()
    # `libman' is the pages for the LIBRARIES a component builds, which its
    # programs' count cannot show: net documents one of its twenty-three
    # programs and all twenty-six of libsocket's articles, and a row reporting
    # only the first reads as a component with no manual.
    out.write("%-16s %-14s %5s %5s %5s %6s %5s %6s %6s\n"
              % ("component", "kinds", "files", "progs", "man", "nopage",
                 "src", "nosrc", "libman"))
    # A COMPONENT IS COUNTED ONLY FOR THE KINDS IT PUBLISHES.  toolchain-1985 does
    # not publish `src' -- its programs are vendor binaries no
    # repository holds source for, and the list says so -- and counting those
    # against a total would report a shortfall nobody can close, which is the
    # state a gate gets switched off in.
    tp = tm = tmp = ts = tsp = tl = 0
    for n in all_components():
        c = Component(n)
        progs = c.programs()
        tp += len(progs)
        cols = []
        if 'man' in c.kinds:
            have, miss = c.man(idx)
            tm += len(have); tmp += len(progs)
            cols += ["%5d" % len(have), "%6d" % len(miss)]
        else:
            cols += ["%5s" % '-', "%6s" % '-']
        if 'src' in c.kinds:
            _, nosrc = c.src(sm)
            ts += len(progs) - len(nosrc); tsp += len(progs)
            cols += ["%5d" % (len(progs) - len(nosrc)), "%6d" % len(nosrc)]
        else:
            cols += ["%5s" % '-', "%6s" % '-']
        nlib = sum(len(v) for v in c.libman(libidx).values())
        tl += nlib
        out.write("%-16s %-14s %5d %5d %s %6s\n"
                  % (n, ",".join(c.kinds), len(c.files()), len(progs),
                     " ".join(cols), nlib if 'man' in c.kinds else '-'))
    out.write("-- %d component(s), %d program(s).  Of those in a component that "
              "publishes the kind:\n   manual %d of %d (%d owed), source %d of "
              "%d (%d owed); %d library page(s)\n"
              % (len(all_components()), tp, tm, tmp, tmp - tm, ts, tsp,
                 tsp - ts, tl))
    if not idx:
        out.write("-- no %s/man.index on this machine: the manual columns are "
                  "zero because nothing was read, not because nothing is "
                  "missing (`make -C hostbuild man')\n" % MANTREE)
    if sm is None:
        out.write("-- no %s, so no component can cut a -src package and the "
                  "source columns are zero for that reason\n" % SRCMAP)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'packages':
        packages(sys.stdout, sys.argv[2] if len(sys.argv) > 2 else None)
    elif cmd == 'components':
        # Validate first: a tree whose package declarations do not validate has
        # no components to report on.  packages() writes to a sink -- what is
        # wanted here is its refusals, not its table.
        packages(open(os.devnull, 'w'))
        components(sys.stdout)
    elif cmd == 'component':
        if len(sys.argv) != 4:
            die("usage: component.py component <name> <%s>" % "|".join(PKGKINDS))
        component_kind(sys.argv[2], sys.argv[3], sys.stdout)
    elif cmd == 'version':
        print(version())
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
