#!/usr/bin/env python3
"""argwidth.py -- find arguments whose width does not match the callee's.

WHY THIS EXISTS.  ARGS() expands to () on this compiler, so a call site is the
only place an argument's width is decided: nothing widens it.  int is 16 bits,
long is 32, and a far pointer is 32.  Four shapes therefore break:

  * a bare 0 (or any int constant) where a pointer or long is expected -- two
    bytes into a four-byte slot, which also drags every FOLLOWING argument out
    of place, so the callee stores through a half-formed pointer;
  * an int variable passed to a long/time_t/off_t parameter -- same;
  * a long (or an unsuffixed decimal above 32767, which is typed long) passed
    to an int parameter -- four bytes into a two-byte slot, same displacement
    in the other direction;
  * a function returning a pointer or a long with no declaration in scope,
    whose result is then passed on as an argument.

The tool knows BOTH sides.  Callee parameter widths come from

  * sys/z8001/src/tab.c -- the authoritative I/L/P width column for every
    system call, which is what a libc/sys/*.s stub traps into unchanged; and
  * every K&R or prototype function DEFINITION in the trees swept.

Argument widths come from a scope table built per function (parameters, block
declarations, then file-scope), plus the literal forms (constants, string
literals, casts, &x, sizeof).  An argument whose width cannot be decided is
skipped rather than guessed -- this reports what it can prove, and silence
about a call is not a clean bill of health for it.

Usage:  argwidth.py [dir ...]        (default: cmd libc net games)
        argwidth.py --defs NAME      show the recorded signature of NAME
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.dirname(HERE)

# ---------------------------------------------------------------- lexing ----

def strip(text):
    """Remove comments and preprocessor lines, keeping byte offsets stable."""
    out = []
    i, n = 0, len(text)
    bol = True
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i+1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
            continue
        if c == '"' or c == "'":
            # Blank the INTERIOR of a literal, keeping the quotes: an
            # identifier followed by `(' inside a format string
            # ("selfix(%P, %C, ") is not a call.
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + ''.join(ch if ch == '\n' else ' '
                                   for ch in text[i+1:j-1]) + text[j-1:j])
            i = j
            bol = False
            continue
        if c == '#' and bol:
            j = i
            while j < n:
                j = text.find('\n', j)
                if j < 0:
                    j = n
                    break
                if text[j-1] != '\\':
                    break
                j += 1
            out.append(''.join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
            continue
        if c == '\n':
            bol = True
        elif not c.isspace():
            bol = False
        out.append(c)
        i += 1
    return ''.join(out)

IDENT = re.compile(r'[A-Za-z_]\w*')

# ---------------------------------------------------------------- widths ----

TYPEDEF_W = {
    # <sys/types.h> and friends.  Only the ones whose width is not 2.
    'time_t': 4, 'off_t': 4, 'daddr_t': 4, 'paddr_t': 4, 'clock_t': 4,
    'size_t': 2, 'ssize_t': 2, 'ino_t': 2, 'dev_t': 2, 'mode_t': 2,
    'FILE': 0, 'i32_t': 4, 'u32_t': 4, 'i16_t': 2, 'u16_t': 2,
    'i8_t': 2, 'u8_t': 2, 'ipaddr_t': 4, 'acc_t': 0, 'clck_t': 4,
    'jmp_buf': 4, 'va_list': 4,
}
# `reg' is the BSD games' abbreviation for `register' (games/bsd/mille/mille.h).
STORAGE = set('register reg const volatile static auto extern'.split())
TYPES = set('''char short int long unsigned signed float double void
    struct union enum'''.split())
TYPEWORDS = STORAGE | TYPES

def width(decl):
    """Width in bytes of a parameter/variable declared by `decl' (type + name).
    0 = do not know / do not care (aggregates, typedefs we have not modelled)."""
    d = decl.strip()
    if '*' in d or '[' in d or '(' in d:
        return 4                       # pointer, array-as-pointer, func ptr
    words = re.findall(r'\w+', d)
    if not words:
        return 0
    if 'double' in words or 'float' in words:
        return 8                       # K&R promotes float to double
    if 'long' in words:
        return 4
    if 'struct' in words or 'union' in words:
        return 0                       # by value: size unknown here
    for w in words:
        if w in TYPEDEF_W:
            return TYPEDEF_W[w]
    for w in words:
        if w in ('char', 'short', 'int', 'unsigned', 'signed', 'enum'):
            return 2                   # all promote to int
    return 0

# ------------------------------------------------------- signature tables ---

def kernel_siblings():
    """Candidate checkouts of commodore-900-coh-kernel3, mirroring the
    `siblings()' search in mk/deps.sh: three parents out from this
    repository's root, then a repos/ directory beside it."""
    root = OS                             # this repo's root
    cands = []
    d = root
    for _ in range(3):
        d = os.path.dirname(d)
        cands.append(os.path.join(d, 'commodore-900-coh-kernel3'))
    cands.append(os.path.join(root, 'repos', 'commodore-900-coh-kernel3'))
    return cands

def find_tab_c():
    """Resolve sys/z8001/src/tab.c, which left this repository for
    commodore-900-coh-kernel3 in the kernel/userland split.  $C900_KERNEL
    wins if set; otherwise the same sibling search mk/deps.sh uses for its
    other out-of-tree edges (emu, toolchain, lexicon, dist).  The search is
    reimplemented here rather than routed through mk/deps.sh because that
    file has no `kernel' case of its own -- the dist repository's mk/deps.sh
    does, since packing a bootable image is what actually needs the kernel;
    a plain userland/argwidth check does not, so this is this script's own
    narrower edge and does not belong in the shared resolver.
    Returns the resolved path, or None (with the candidates tried) on
    failure -- never silently."""
    given = os.environ.get('C900_KERNEL')
    cands = [given] if given else kernel_siblings()
    for c in cands:
        p = os.path.join(c, 'os/sys/z8001/src/tab.c')
        if os.path.isfile(p):
            return p, cands
    return None, cands

def syscall_sigs():
    """Parameter widths of every system call, from the kernel's own table.

    An empty table here means every raw system call goes UNCHECKED with no
    warning -- exactly the silent-pass this tool exists to prevent for
    userland call sites, so a missing or unparseable table is a loud
    failure, not a quietly empty dict."""
    path, cands = find_tab_c()
    if path is None:
        given = os.environ.get('C900_KERNEL')
        sys.exit(
            "argwidth.py: cannot find sys/z8001/src/tab.c -- the kernel "
            "table of system-call signatures.  tab.c moved to the "
            "commodore-900-coh-kernel3 repository and this tool needs it; "
            "an empty signature table would leave every raw system call "
            "unchecked with no warning, so refusing instead of proceeding.\n"
            + ("*** C900_KERNEL=%s does not have it (os/sys/z8001/src/tab.c).\n" % given
               if given else
               "*** C900_KERNEL is unset; the checkouts tried were:\n"
               + ''.join("***     %s\n" % c for c in cands))
            + "*** Clone commodore-900-coh-kernel3 beside this repository, "
              "or set C900_KERNEL to its checkout.")
    sigs = {}
    try:
        text = open(path, errors='replace').read()
    except OSError as e:
        sys.exit("argwidth.py: found %s but could not read it: %s" % (path, e))
    body = text[text.find('sysitab[NMICALL]'):]
    body = body[:body.find('};')]
    for m in re.finditer(r'^\s*([0IPL+ \t]+),\s*\w+,\s*\w+,\s*/\*\s*\d+\s*=\s*([\w.]+)',
                         body, re.M):
        expr, name = m.group(1).strip(), m.group(2)
        if expr == '0':
            sigs[name] = ([], 'tab.c', [])
            continue
        w = []
        ok = True
        for tok in expr.split('+'):
            tok = tok.strip()
            if tok == 'I':
                w.append(2)
            elif tok in ('P', 'L'):
                w.append(4)
            else:
                ok = False
        if ok:
            sigs[name] = (w, 'tab.c', [False]*len(w))
    # names the stub file spells differently from the table comment
    for a, b in (('exec', 'execve'), ('break', 'brk')):
        if a in sigs:
            sigs[b] = sigs[a]
    if not sigs:
        sys.exit(
            "argwidth.py: %s parsed to ZERO system-call signatures -- "
            "either sysitab[NMICALL] is not there any more or its row "
            "format changed.  Every raw system call would then go "
            "unchecked with no warning, so refusing instead." % path)
    return sigs

# A function definition: at brace depth 0, IDENT '(' ... ')' then either '{'
# (ANSI) or K&R declarations then '{'.
# MWC style puts a function definition's NAME at column 0 (the return type, if
# any, sits on the line above), which is what makes this cheap and exact.  The
# scan is procedural rather than one regex: the K&R declaration block between
# `)' and `{' is a repeated group, and as a regex that backtracks catastrophically
# on real files.
HEADRE = re.compile(r'^([A-Za-z_][\w \t*]*?)\(', re.M)

def find_defs(text):
    """Yield (start, name, paramtext, krdecls, body_open) for each definition."""
    n = len(text)
    for m in HEADRE.finditer(text):
        ids = IDENT.findall(m.group(1))
        if not ids or ids[-1] in NOTFUNC:
            continue
        i, depth = m.end(), 1
        while i < n and depth:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        if depth:
            continue
        plist, j = text[m.end():i-1], i
        while j < n and text[j] not in '{};':
            if text[j] in ')(':
                break
            j += 1
        if j >= n or text[j] != '{':
            # Nothing between `)' and the `;' means this is a DECLARATION, not a
            # definition -- `static int parse_cidr();' otherwise swallowed the
            # next function's body as its K&R parameter block and lent every call
            # in it the wrong scope.
            if not text[i:j].strip():
                continue
            # K&R parameter declarations, then the body
            k, ok = j, True
            while k < n and text[k] == ';':
                k += 1
                while k < n and text[k] not in '{};':
                    k += 1
            if k >= n or text[k] != '{':
                continue
            yield m.start(), ids[-1], plist, text[i:k], k
        else:
            yield m.start(), ids[-1], plist, '', j

NOTFUNC = set('''if while for switch return sizeof defined do else case'''.split())

def kr_types(kr):
    """name -> declaration text, for a K&R parameter declaration block."""
    types = {}
    for d in re.finditer(r'([^;]+);', kr):
        decl = d.group(1).strip()
        # Peel the type words off the front.  `char* commandname' and
        # `struct hshentry *delta, *seq[]' both occur in this tree, so this
        # cannot be one regex over "words then space".
        cut, want = 0, True
        for t in re.finditer(r'\w+|\*|\[|,', decl):
            w = t.group(0)
            if w in STORAGE:
                cut = t.end()
            elif w in TYPES:
                cut, want = t.end(), w in ('struct', 'union', 'enum')
            elif want and IDENT.fullmatch(w):
                cut, want = t.end(), False        # a typedef name
            else:
                break
        if not cut:
            continue
        btype = decl[:cut]
        for one in split_args(decl[cut:]):
            nm = IDENT.search(one)
            if nm:
                types[nm.group(0)] = btype + ' ' + one
    return types

def parse_defs(path, sigs, varargs):
    raw = open(path, errors='replace').read()
    text = strip(raw)
    for start, name, plist, kr, _ in find_defs(text):
        params = [p.strip() for p in plist.split(',') if p.strip()]
        if params and any(re.search(r'\b(char|int|long|short|unsigned|struct|'
                                    r'void|float|double|register)\b|\*', p)
                          for p in params):
            w = [width(p) for p in params]          # ANSI-style list
            if len(params) == 1 and params[0].strip() == 'void':
                w = []
            imp = [False] * len(w)
        else:
            names = params
            types = kr_types(kr)
            w = [width(types.get(p, 'int ' + p)) for p in names]
            # A K&R parameter with NO declaration is an implicit int -- 2 bytes
            # for something the caller may well be pushing 4 for.  Flagged
            # separately because the callee, not the call, is then the defect.
            imp = [p not in types for p in names]
        # /* VARARGS */, or a body that only takes the parameter's ADDRESS and
        # walks the list with %r: the declared width is then not the truth and
        # not load-bearing either.
        if '%r' in raw[start:start + 4000] or 'VARARGS' in raw[max(0, start-200):start+120]:
            varargs.add(name)
        prev = sigs.get(name)
        if prev and prev[1] == 'tab.c':
            continue                                 # kernel table wins
        if prev and prev[0] != w:
            varargs.add(name)                        # conflicting defs: skip
        sigs[name] = (w, '%s' % path, imp)

# --------------------------------------------------------- argument sides ---

def split_args(s):
    args, depth, cur, i, n = [], 0, [], 0, len(s)
    while i < n:
        c = s[i]
        if c in '"\'':
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j+1]); i = j + 1; continue
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        if c == ',' and depth == 0:
            args.append(''.join(cur)); cur = []; i += 1; continue
        cur.append(c); i += 1
    if ''.join(cur).strip():
        args.append(''.join(cur))
    return [a.strip() for a in args]

NUM = re.compile(r'^[+-]?(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)$')

def const_width(a):
    m = NUM.match(a)
    if not m:
        return None
    suf = m.group(2).lower()
    if 'l' in suf:
        return 4
    lit = m.group(1)
    base = 16 if lit[:2].lower() == '0x' else (8 if len(lit) > 1 and lit[0] == '0' else 10)
    try:
        v = int(lit, base)
    except ValueError:
        return None
    if base != 10:
        return 4 if v > 0xFFFF else 2
    return 4 if abs(v) > 32767 else 2

def arg_width(a, scope, sigs, kernel_null):
    a = a.strip()
    while a.startswith('(') and a.endswith(')') and split_args(a[1:-1]) == [a[1:-1].strip()]:
        inner = a[1:-1].strip()
        if not inner:
            break
        a = inner
    if not a:
        return None
    if a.startswith('"'):
        return 4
    if a.startswith("'"):
        return 2
    if a.startswith('&'):
        return 4
    if a == 'NULL':
        return 2 if kernel_null else 4
    w = const_width(a)
    if w:
        return w
    m = re.match(r'^\(\s*([^()]*?)\s*\)\s*(.+)$', a)     # a cast
    if m and re.search(r'\b(char|int|long|short|unsigned|struct|void|float|'
                       r'double|time_t|off_t|daddr_t|u\d+_t|i\d+_t)\b', m.group(1)):
        return width(m.group(1)) or None
    if a.startswith('sizeof'):
        return 2
    if IDENT.fullmatch(a):
        return scope.get(a)
    m = re.fullmatch(r'([A-Za-z_]\w*)\s*\((.*)\)', a, re.S)   # a call
    if m and m.group(1) in sigs:
        return None                     # return width: PTRAUDIT-NOTES covers it
    return None

DECL = re.compile(r'^[ \t]*((?:(?:register|static|auto|const|volatile|unsigned|'
                  r'signed|struct|union|enum|extern)\s+)*'
                  r'(?:char|short|int|long|float|double|void|FILE|[A-Za-z_]\w*_t|'
                  r'struct\s+\w+|union\s+\w+|\w+)\s+)([^;={]*)[=;]', re.M)

def build_scope(text):
    """name -> width, for declarations anywhere in `text' (params + locals)."""
    sc = {}
    for m in DECL.finditer(text):
        base, rest = m.group(1), m.group(2)
        if re.match(r'^\s*(return|goto|break|continue|case|else|typedef)\b', base):
            continue
        for one in split_args(rest):
            nm = IDENT.search(one)
            if not nm:
                continue
            if '(' in one and '*' not in one.split('(')[0]:
                continue                       # a function declaration
            w = width(base + one)
            if w:
                sc.setdefault(nm.group(0), w)
    return sc

CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')

SKIP = set('''printf fprintf sprintf scanf fscanf sscanf execl execle execlp
    open ioctl fcntl syscall main if while for switch return sizeof defined
    do else case va_start va_arg va_end setjmp longjmp'''.split())

def scan(path, sigs, varargs, hits):
    src = open(path, errors='replace').read()
    text = strip(src)
    kernel_null = bool(re.search(r'#\s*include\s*<(sys/)?coherent\.h>', src))
    filescope = build_scope(text)
    # per-function scopes
    funcs = []
    n = len(text)
    for start, _, plist, kr, bopen in find_defs(text):
        depth, i = 0, bopen
        while i < n:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        sc = build_scope(text[start:i])
        # A K&R parameter with no declaration is an implicit int and must not
        # fall through to a same-named file-scope variable: `delexit(s) { ... }'
        # reading `s' as some other file's `char *s' invented three findings.
        # Parameters come from the K&R block, which build_scope cannot see when
        # it is written on the header's own line (`f(a, b) char *a; int b;').
        # A parameter with no declaration at all is an implicit int.
        types = kr_types(kr)
        for p_ in split_args(plist):
            p_ = p_.strip()
            if not IDENT.fullmatch(p_):
                continue
            sc[p_] = width(types[p_]) if p_ in types else 2
        funcs.append((start, i, sc))
    defstarts = set()
    for _s, _n, _p, _k, _b in find_defs(text):
        defstarts.add(text.find(_n + '(', _s))
    for m in CALL.finditer(text):
        name = m.group(1)
        if m.start() in defstarts:
            continue
        if name in SKIP or name in varargs or name not in sigs:
            continue
        pre = text[:m.start()].rstrip()
        if pre.endswith('#') or re.search(r'\b(define)\s*$', pre):
            continue
        depth, i, n = 1, m.end(), len(text)
        while i < n and depth:
            c = text[i]
            if c in '"\'':
                j = i + 1
                while j < n and text[j] != c:
                    j += 2 if text[j] == '\\' else 1
                i = j + 1
                continue
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        if depth:
            continue
        inner = text[m.end():i-1]
        if not inner.strip():
            continue
        args = split_args(inner)
        pw, where, imp = sigs[name]
        if len(args) != len(pw):
            continue                      # arity: reported separately, noisy
        scope = filescope
        for a, b, sc in funcs:
            if a <= m.start() <= b:
                scope = dict(filescope); scope.update(sc)
                break
        for k, (a, p) in enumerate(zip(args, pw)):
            if p == 0:
                continue
            w = arg_width(a, scope, sigs, kernel_null)
            if not w or w == p or (p == 8 and w == 4):
                continue
            # A: the argument is a literal/cast/&x -- its width is not in doubt.
            # B: the callee's parameter has no declaration (implicit int) and the
            #    caller passes something wider -- the callee is the defect.
            # C: decided from a scope table, so a same-named variable elsewhere
            #    can mislead it: review before believing.
            lit = not IDENT.fullmatch(a.strip())
            cls = 'A' if lit else ('B' if k < len(imp) and imp[k] else 'C')
            line = text[:m.start()].count('\n') + 1
            hits.append((cls, path, line, name, k + 1, a.strip()[:40], w, p, where))

def unit_of(rel):
    """The link unit a source file belongs to.  Signatures must NOT be shared
    across programs: cmd/dc has its own new(), the compiler's n0 has another,
    and one
    global table would check every call against whichever it saw last."""
    parts = rel.split(os.sep)
    if parts[0] in ('libc', 'libm', 'libmp', 'liby') or rel.startswith('games/lib'):
        return None                       # base: linked into everything
    if len(parts) == 2:
        return rel                        # a single-file command
    return os.sep.join(parts[:-1])        # the command's directory

def main():
    dirs = sys.argv[1:] or ['cmd', 'libc', 'net', 'games']
    base = syscall_sigs()
    basevar = set(SKIP)
    files = []
    for d in dirs:
        for root, _, names in os.walk(os.path.join(OS, d)):
            if '/hostbuild/build/' in root + '/' or '/archive/' in root + '/':
                continue                  # stale build copies, not sources
            for f in names:
                if f.endswith('.c'):
                    files.append(os.path.join(root, f))
    units = {}
    for f in files:
        units.setdefault(unit_of(os.path.relpath(f, OS)), []).append(f)
    for f in units.get(None, []):
        try:
            parse_defs(f, base, basevar)
        except Exception:
            pass
    hits = []
    for u, fs in units.items():
        if u is None:
            continue
        sigs, varargs = dict(base), set(basevar)
        for f in fs:
            try:
                parse_defs(f, sigs, varargs)
            except Exception:
                pass
        for f in fs:
            try:
                scan(f, sigs, varargs, hits)
            except Exception as e:
                print('!! %s: %s' % (f, e), file=sys.stderr)
    for h in sorted(hits):
        cls, path, line, name, k, a, w, p, where = h
        print('%s %s:%d: %s arg %d = `%s\' is %d bytes, parameter is %d  [%s]'
              % (cls, os.path.relpath(path, OS), line, name, k, a, w, p,
                 os.path.relpath(where, OS) if where != 'tab.c' else 'tab.c'))
    for c in 'ABC':
        print('%s: %d' % (c, sum(1 for h in hits if h[0] == c)), file=sys.stderr)

if __name__ == '__main__':
    main()
