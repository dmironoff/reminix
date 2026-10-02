#!/usr/bin/env python3
"""
ReMinix: print the VGA text screen (80x25 at 0xb8000) of a running QEMU guest.

usage: qemu-vga.py MONITOR_SOCKET TMPFILE

A kernel panic is written straight to video memory and never reaches the
serial console; run-console.sh polls the screen with this to notice it.
TMPFILE must be a path QEMU can write (it runs in the same container).
"""
import os
import socket
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    sock, tmp = sys.argv[1], os.path.abspath(sys.argv[2])
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(sock)

    def prompt():
        buf = b""
        while not buf.endswith(b"(qemu) "):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk

    prompt()
    s.sendall(('pmemsave 0xb8000 4000 "%s"\n' % tmp).encode())
    prompt()
    b = open(tmp, "rb").read()
    os.unlink(tmp)
    for r in range(len(b) // 160):
        print("".join(chr(b[(r * 80 + c) * 2]) if 32 <= b[(r * 80 + c) * 2] < 127
                      else " " for c in range(80)).rstrip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
