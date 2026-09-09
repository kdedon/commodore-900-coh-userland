#!/usr/bin/env python3
"""licences.py -- collect the release's licence texts from dist/licences.tab.

    python3 dist/licences.py --collect <dir>

WHAT THIS IS FOR.  A licence obligation is discharged by handing the terms over
with the thing they govern.  The thing handed over is the release, so the texts
are collected ONCE into a directory beside it: an index.tab naming every regime
and which component is under it, and beside that each regime's licence text,
verbatim, as the repository that holds the code holds it.

A source package needs nothing from here.  It carries the source root of every
program it ships, so each program's own COPYING travels beside the code it
governs, which is where upstream put it and where a reader looks for it.

dist/licences.tab is the register those rows are read from -- one row per
component per licence regime, with the source and notice obligations each
carries.  A `?' in the `ship' column is a recorded judgement, unsettled and
treated conservatively; it travels into the package as it stands.

The texts are copied, never transcribed: a transcription would be this project's
words about somebody else's terms.  Nothing here is legal advice.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))               # dist/
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'hostbuild'))
import component as d                                           # noqa: E402

TAB = os.path.join(HERE, 'licences.tab')


def die(msg):
    print("licences: %s" % msg, file=sys.stderr)
    sys.exit(2)



def read_tab(path):
    """({id: lic}, [use]) from licences.tab.

    A malformed row is a fault and not a row to skip: a table nobody can parse
    is a table nobody is checking, and this file is the only record that anybody
    looked at the terms at all.
    """
    lics, uses = {}, []
    for n, line in enumerate(open(path), 1):
        line = line.split('#')[0].rstrip()
        if not line.strip():
            continue
        f = line.split(None, 7)
        where = "%s:%d" % (os.path.basename(path), n)
        if f[0] == 'lic':
            if len(f) < 7:
                die("%s: `lic' wants id src notice ship text marker what" % where)
            lid, src, notice, ship, text, marker = f[1:7]
            what = f[7] if len(f) > 7 else ''
            for col, v, allowed in (('src', src, 'yn?'), ('notice', notice, 'yn?'),
                                    ('ship', ship, 'y?')):
                if v not in allowed:
                    die("%s: %s=%s, wants one of %s"
                        % (where, col, v, "|".join(allowed)))
            if text == '-' and ship != '?':
                die("%s: %s has no licence text and ship=%s.  `-' records that "
                    "we looked and found nothing, which is only honest with the "
                    "question left open." % (where, lid, ship))
            if lid in lics:
                die("%s: %s declared twice" % (where, lid))
            lics[lid] = dict(id=lid, src=src, notice=notice, ship=ship,
                             text=text, marker=marker, what=what, where=where)
        elif f[0] == 'use':
            if len(f) < 3:
                die("%s: `use' wants component and licence id" % where)
            uses.append(dict(comp=f[1], lic=f[2],
                             why=" ".join(f[3:]), where=where))
        else:
            die("%s: unknown record `%s' (lic|use)" % (where, f[0]))
    return lics, uses



def resolve(text, marker='-'):
    """The licence text on this machine, or None.

    `LICENSE' with no slash is this repository's own, which pack-component.sh
    already copies into every package.  Anything else is a root-relative path,
    and THE MARKER DECIDES WHICH CANDIDATE WINS rather than the search order.
    First-wins is right for a descriptor -- this repository's copy must not be
    shadowed -- and wrong here: `../LICENSE' names the Mark Williams release,
    several roots on this machine have a file at that path, and the one reached
    first is this repository's own BSD-3 notice, which does not carry Swartz's
    line and is not the document being pointed at.  So the path narrows the
    search and the marker settles it.
    """
    if text == '-':
        return None
    if text == 'LICENSE':
        p = os.path.join(os.path.dirname(HERE), 'LICENSE')
        return p if os.path.exists(p) else None
    want = marker.replace('.', ' ')
    fallback = None
    for p in d.oscands(text):
        if not os.path.isfile(p):
            continue
        if fallback is None:
            fallback = p
        if want == '-' or want in open(p, errors='replace').read():
            return p
    # Nothing carried the marker.  The first candidate is returned so that
    # lic.text reports WHICH file it read and what it wanted, rather than
    # reporting the path as absent when a file is sitting there.
    return fallback


def collect(into):
    """Write <into>/index.tab and one verbatim licence text per regime.

    ONE COPY, BESIDE THE RELEASE.  The terms are handed over with the thing they
    govern, and the thing handed over is the release: the images and the
    component archives sit in one directory and the licences sit beside them,
    the way an installed system keeps its copyrights in one place rather than
    scattering a copy through every package.  A source package needs nothing
    from here -- it carries each program's own COPYING beside the code it
    governs, which is where upstream put it.

    The text files are named for the regime, so licences/gpl1-screen is the
    GPLv1 as the producing repository holds it, byte for byte.  A transcription
    would be this project's words about somebody else's terms.
    """
    lics, uses = read_tab(TAB)
    os.makedirs(into, exist_ok=True)
    with open(os.path.join(into, 'index.tab'), 'w') as f:
        f.write("# index.tab -- the terms the components in this release are "
                "under.\n"
                "#\n"
                "# One `lic' row per licence regime, cut from "
                "dist/licences.tab; `src' and\n"
                "# `notice' say what the regime obliges, and `ship' is `?' "
                "where this project\n"
                "# has an open question about redistributing under it at all.  "
                "The file column\n"
                "# names the verbatim text beside this one.  One `use' row per "
                "component says\n"
                "# which of them govern it.  Nothing here is legal advice.\n"
                "#\n"
                "#\tlic <id> <src> <notice> <ship> <file> <what>\n"
                "#\tuse <component> <id>\n")
        n = 0
        for lid in sorted(lics):
            L = lics[lid]
            src = resolve(L['text'], L['marker'])
            name = L['id'] if src else '-'
            if src:
                with open(os.path.join(into, L['id']), 'wb') as o:
                    o.write(open(src, 'rb').read())
                n += 1
            elif L['text'] != '-':
                die("%s names %s and no root of the search path has it, so the "
                    "release cannot carry the notice it owes.  Refusing rather "
                    "than collecting a licences/ with a hole in it."
                    % (L['id'], L['text']))
            f.write("lic\t%s\t%s\t%s\t%s\t%s\t%s\n"
                    % (L['id'], L['src'], L['notice'], L['ship'], name,
                       L['what']))
        for u in sorted(uses, key=lambda x: (x['comp'], x['lic'])):
            f.write("use\t%s\t%s\n" % (u['comp'], u['lic']))
    print("licences: %d regime(s), %d text(s), %d component row(s) in %s"
          % (len(lics), n, len(uses), into))


def main(argv):
    if len(argv) != 3 or argv[1] != '--collect':
        die("usage: licences.py --collect <dir>")
    collect(argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
