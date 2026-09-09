#!/usr/bin/env python3
"""simtype.py <base-url> <text> -- type text into the sim's serial channel B.

One character at a time with a gap: the SCC Rx FIFO overruns if a whole line
arrives in one burst, and `read -n1' is not available in dash, so the pacing
lives here rather than in the shell.

The line is terminated with CR, not LF, on purpose: getty learns the terminal's
line ending from the login name's terminator, so CR keeps CRMOD on for the whole
session (see sim-boot-login.sh).
"""
import json
import sys
import time
import urllib.request

DELAY = 0.12


def put(base, ch):
    body = json.dumps({"channel": 1, "text": ch}).encode()
    req = urllib.request.Request(base + "/serial/send", data=body,
                                 headers={"Content-Type": "application/json"})
    urllib.request.urlopen(req).read()
    time.sleep(DELAY)


def main():
    base, text = sys.argv[1], sys.argv[2]
    for c in text:
        put(base, c)
    put(base, "\r")


if __name__ == '__main__':
    main()
