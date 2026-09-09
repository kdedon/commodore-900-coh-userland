#!/usr/bin/env python3
"""
Find calls that pass a bare 0 (or 0L) where the
callee reads a 32-bit pointer.  Collects K&R and ANSI definitions from the given
files, works out which parameters are pointers, then scans every call.

Two sources of parameter types:

  * the definitions read out of the swept files, and
  * LIBC below, the entry points whose definitions are never swept because they
    live in libc, libmgr or the kernel.  Without these the sweep is blind in
    exactly the place the bug lives: `time(0)' writes four bytes through a
    pointer assembled from a two-byte push, and `select(0,0,0,0,&tv)' shifts
    the timeout six bytes away from where the callee fetches it.

A call resolves to a definition in its own file first, then to LIBC, then to a
definition in another swept file -- and that last step only when the name is
defined once across the sweep.  Two programs each with a static `update()' or
`die()' otherwise type each other's calls and report hits that are not there.
"""
import re
import sys

ANY = 'any'                     # every argument is a pointer (the execl family)

# Pointer-argument positions, zero-based, of the entry points defined outside
# the swept sources.  A position appears here only when the callee dereferences
# it: strchr(s,c) takes a pointer and an int, so it is {0}, not {0,1}.
LIBC = {
    # time and the clock
    'time': {0}, 'ftime': {0}, 'times': {0}, 'stime': {0},
    'localtime': {0}, 'gmtime': {0}, 'ctime': {0}, 'asctime': {0},
    'mktime': {0}, 'gettimeofday': {0, 1}, 'settimeofday': {0, 1},
    'utime': {0, 1},
    # descriptors and terminals
    'select': {1, 2, 3, 4}, 'ioctl': {2}, 'poll': {0}, 'pipe': {0},
    'read': {1}, 'write': {1}, 'stat': {0, 1}, 'lstat': {0, 1}, 'fstat': {1},
    'open': {0}, 'creat': {0}, 'access': {0}, 'unlink': {0}, 'chdir': {0},
    'chmod': {0}, 'chown': {0}, 'link': {0}, 'mknod': {0}, 'mount': {0, 1},
    'umount': {0}, 'ustat': {1}, 'getcwd': {0}, 'getwd': {0},
    # processes
    'execv': {1}, 'execvp': {1}, 'execve': {1, 2},
    'execl': ANY, 'execlp': ANY, 'execle': ANY,
    'wait': {0}, 'wait3': {0, 2}, 'waitpid': {1}, 'system': {0},
    'signal': {1}, 'sigset': {1}, 'setjmp': {0}, 'longjmp': {0},
    'nlist': {0, 1}, 'popen': {0, 1}, 'pclose': {0},
    # stdio
    'fopen': {0, 1}, 'freopen': {0, 1, 2}, 'fdopen': {1}, 'fclose': {0},
    'fflush': {0}, 'fseek': {0}, 'ftell': {0}, 'rewind': {0}, 'feof': {0},
    'ferror': {0}, 'clearerr': {0}, 'fileno': {0},
    'fgets': {0, 2}, 'fputs': {0, 1}, 'fgetc': {0}, 'fputc': {1}, 'gets': {0},
    'puts': {0}, 'fread': {0, 3}, 'fwrite': {0, 3},
    'setbuf': {0, 1}, 'setvbuf': {0, 1},
    'printf': {0}, 'fprintf': {0, 1}, 'sprintf': {0, 1},
    'scanf': {0}, 'fscanf': {0, 1}, 'sscanf': {0, 1},
    'perror': {0},
    # memory, strings, sorting
    'free': {0}, 'realloc': {0},
    'qsort': {0, 3}, 'bsearch': {0, 1, 4},
    'memset': {0}, 'memcpy': {0, 1}, 'memmove': {0, 1}, 'memcmp': {0, 1},
    'memchr': {0},
    'strcpy': {0, 1}, 'strncpy': {0, 1}, 'strcat': {0, 1}, 'strncat': {0, 1},
    'strcmp': {0, 1}, 'strncmp': {0, 1}, 'strlen': {0}, 'strchr': {0},
    'strrchr': {0}, 'strstr': {0, 1}, 'strpbrk': {0, 1}, 'strspn': {0, 1},
    'strcspn': {0, 1}, 'strtok': {0, 1}, 'strdup': {0},
    'strtol': {0, 1}, 'strtoul': {0, 1}, 'strtod': {0, 1},
    'atoi': {0}, 'atol': {0}, 'atof': {0},
    'index': {0}, 'rindex': {0},
    # users, hosts, environment
    'getenv': {0}, 'putenv': {0}, 'getpwnam': {0}, 'getgrnam': {0},
    'gethostname': {0}, 'uname': {0}, 'initgroups': {0},
    'gethostbyname': {0}, 'gethostbyaddr': {0},
    # sockets
    'socket': set(), 'bind': {1}, 'connect': {1}, 'accept': {1, 2},
    'listen': set(), 'send': {1}, 'recv': {1},
    'sendto': {1, 4}, 'recvfrom': {1, 4},
    'getsockopt': {3, 4}, 'setsockopt': {3},
    'getsockname': {1, 2}, 'getpeername': {1, 2},
}

KR_DEF = re.compile(
    r'^([A-Za-z_][A-Za-z0-9_ \t\*]*?\b)?([A-Za-z_]\w*)\s*\(([^;{)]*)\)\s*\n'
    r'((?:[ \t]*[A-Za-z_#][^;{]*;[ \t]*\n)*)'
    r'[ \t]*\{', re.M)


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', lambda m: ' ' + '\n' * m.group(0).count('\n'), s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    return s


def split_args(s):
    out, d, cur = [], 0, ''
    for c in s:
        if c in '([':
            d += 1
        elif c in ')]':
            d -= 1
        if c == ',' and d == 0:
            out.append(cur)
            cur = ''
        else:
            cur += c
    if cur.strip() or out:
        out.append(cur)
    return out


def collect(path, text, local):
    """Record path's own definitions in local[path]."""
    for m in KR_DEF.finditer(text):
        name = m.group(2)
        params = [p.strip() for p in m.group(3).split(',') if p.strip()]
        decls = m.group(4)
        line = text[:m.start(2)].count('\n') + 1
        if not params:
            local.setdefault(path, {})[name] = (set(), line)
            continue
        # ANSI style: types are in the parameter list itself
        if any(re.search(r'\b(char|int|long|short|unsigned|float|double|void|'
                         r'struct|union|BITMAP|DATA|FILE|WINDOW|fd_set)\b', p)
               for p in params):
            ptrs = set(i for i, p in enumerate(params) if '*' in p or '[' in p)
            local.setdefault(path, {})[name] = (ptrs, line)
            continue
        # K&R: names in the list, types in the declarations that follow
        types = {}
        for d in re.finditer(r'([^;]*);', decls):
            for part in split_args(d.group(1).strip()):
                part = part.strip()
                nm = re.findall(r'[A-Za-z_]\w*', part)
                if not nm:
                    continue
                if '*' in part or '[' in part:
                    types[nm[-1]] = 'ptr'
        ptrs = set(i for i, p in enumerate(params) if types.get(p) == 'ptr')
        local.setdefault(path, {})[name] = (ptrs, line)


def resolve(path, name, local, unique):
    """Pointer positions for name as seen from path, with where they came from."""
    here = local.get(path, {}).get(name)
    if here is not None:
        return here[0], '%s:%d' % (path, here[1])
    if name in LIBC:
        return LIBC[name], 'libc'
    where = unique.get(name)
    if where is not None:
        ptrs, line = local[where][name]
        return ptrs, '%s:%d' % (where, line)
    return None, None


def scan(path, text, local, unique):
    hits = []
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', text):
        name = m.group(1)
        ptrs, origin = resolve(path, name, local, unique)
        if not ptrs:
            continue
        i, d = m.end() - 1, 0
        while i < len(text):
            if text[i] in '([':
                d += 1
            elif text[i] in ')]':
                d -= 1
                if d == 0:
                    break
            i += 1
        args = split_args(text[m.end():i])
        positions = range(len(args)) if ptrs is ANY else sorted(ptrs)
        for pos in positions:
            if pos < len(args) and re.fullmatch(r'\s*0[Ll]?\s*', args[pos]):
                line = text[:m.start()].count('\n') + 1
                hits.append((path, line, name, pos + 1, args[pos].strip(), origin))
    return hits


files = sys.argv[1:]
texts = {}
for f in files:
    try:
        texts[f] = strip_comments(open(f, errors='replace').read())
    except IsADirectoryError:
        continue

local = {}
for f, t in texts.items():
    collect(f, t, local)

# A name defined in exactly one swept file may type calls made from the others.
seen = {}
for f, defs in local.items():
    for name in defs:
        seen.setdefault(name, []).append(f)
unique = {n: v[0] for n, v in seen.items() if len(v) == 1}
shared = sorted(n for n, v in seen.items() if len(v) > 1)

hits = []
for f, t in texts.items():
    hits += scan(f, t, local, unique)
for h in sorted(set(hits)):
    print("%s:%d: %s() arg %d = %s (pointer parameter, from %s)" % h)
print("%d files, %d libc entry points, %d local definitions "
      "(%d names defined more than once, typed per file), %d hits"
      % (len(texts), len(LIBC), sum(len(d) for d in local.values()),
         len(shared), len(set(hits))))
