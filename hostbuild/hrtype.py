#!/usr/bin/env python3
"""hrtype.py -- type on the VIDEO console's keyboard, paced.

    python3 hrtype.py [-s URL] [-d SECONDS] 'text with \\n for Enter'

simtype.py is the serial equivalent; this is what the LR and HR consoles need,
because their input does not arrive over a serial line at all.

Two things make a naive version useless.  The sim has two injection endpoints and
only one of them is typing: /keyboard/inject-scancode calls serialOut directly
and bypasses the key matrix, so a string handed to it arrives faster than the
guest can drain it -- 25 characters of a mount command came out as
"/etc/mounTERM=2", interleaved with the next line and with no Enter taking effect
at all.  /keyboard/queue-scancode pushes onto the keyboard MCU's playback FIFO
and lets the firmware's scan loop emit each code, which is the path a real
keyboard takes.  And even then it needs pacing: the FIFO is short, and the guest
echoes through a console driver that is slower than the poster.

So: one character at a time, queued, with a delay, and the delay is per
CHARACTER rather than per line because that is where the queue overflows.
"""
import argparse
import json
import sys
import time
import urllib.request

# XT set-1 make codes.  Only what a shell session needs -- letters, digits, the
# punctuation in a path or an assignment, space and Enter.  A character absent
# here is reported rather than silently skipped, because a dropped character in
# a test looks exactly like a bug in what is being tested.
UNSHIFTED = {
    '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05, '5': 0x06, '6': 0x07,
    '7': 0x08, '8': 0x09, '9': 0x0A, '0': 0x0B, '-': 0x0C, '=': 0x0D,
    '\b': 0x0E, '\t': 0x0F, '\x1b': 0x01,	# ESC -- vi needs it
    'q': 0x10, 'w': 0x11, 'e': 0x12, 'r': 0x13, 't': 0x14, 'y': 0x15,
    'u': 0x16, 'i': 0x17, 'o': 0x18, 'p': 0x19, '[': 0x1A, ']': 0x1B,
    '\n': 0x1C,
    'a': 0x1E, 's': 0x1F, 'd': 0x20, 'f': 0x21, 'g': 0x22, 'h': 0x23,
    'j': 0x24, 'k': 0x25, 'l': 0x26, ';': 0x27, "'": 0x28, '`': 0x29,
    '\\': 0x2B,
    'z': 0x2C, 'x': 0x2D, 'c': 0x2E, 'v': 0x2F, 'b': 0x30, 'n': 0x31,
    'm': 0x32, ',': 0x33, '.': 0x34, '/': 0x35,
    ' ': 0x39,
}

# The shifted face of the same key.
SHIFTED = {
    '!': 0x02, '@': 0x03, '#': 0x04, '$': 0x05, '%': 0x06, '^': 0x07,
    '&': 0x08, '*': 0x09, '(': 0x0A, ')': 0x0B, '_': 0x0C, '+': 0x0D,
    '{': 0x1A, '}': 0x1B, ':': 0x27, '"': 0x28, '~': 0x29, '|': 0x2B,
    '<': 0x33, '>': 0x34, '?': 0x35,
}
for _c, _s in list(UNSHIFTED.items()):
    if _c.isalpha():
        SHIFTED[_c.upper()] = _s


def post(url, path, obj):
    req = urllib.request.Request(url + path,
                                 data=json.dumps(obj).encode(),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def type_text(url, text, delay):
    missing = []
    for ch in text:
        if ch in UNSHIFTED:
            code, shift = UNSHIFTED[ch], False
        elif ch in SHIFTED:
            code, shift = SHIFTED[ch], True
        else:
            missing.append(ch)
            continue
        post(url, '/keyboard/queue-scancode', {'scancode': code, 'shift': shift})
        time.sleep(delay)
    if missing:
        print("hrtype: no scancode for %r -- NOT typed" % ''.join(missing),
              file=sys.stderr)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-s', '--server', default='http://localhost:7800')
    ap.add_argument('-d', '--delay', type=float, default=0.35,
                    help='seconds between characters (default 0.35)')
    ap.add_argument('text')
    a = ap.parse_args()
    return type_text(a.server, a.text.replace('\\n', '\n'), a.delay)


if __name__ == '__main__':
    sys.exit(main())
