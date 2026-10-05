#!/usr/bin/env python3
"""IPC between system services from a ReMinix i386 dump ("ipcstat=1").

Usage: ipcpairs.py <dump dir> [--base <earlier dump>] [--min N] [--cpus]

Reads dbg_ipc_pairs.bin, dbg_ipc_xcpu.bin, priv.bin, proc.bin, symbols.txt
of a snapshot or post-mortem (docker/qemu-postmortem.py) of a kernel booted
with ipcstat=1 (dbg_ipc_count() in minix/kernel/proc.c). Ids are privilege
ids: every system process has its own, all user processes share the one of
init ("user"). A service restarted under a new id appears twice, an id
reused by another service is shown under the last one.

Prints the pairs (both directions summed) by traffic: synchronous sends
(send/sendrec, each call once -- a sendrec is a round trip), notifies,
asynchronous deliveries, the share sent across cpus, and the cpu of each
side at the time of the dump. --cpus: per cpu, the services on it and the
traffic that stays on it vs. leaves it. --base: only the traffic since an
earlier dump of the same run (counters subtracted).
"""
import os
import struct
import sys

NR_TASKS = 5
NR_SYS_PROCS = 64
P_RTS, P_CPU, P_NAME, P_EP = 0x50, 0x6c, 0x100, 0x110


def main():
    args = sys.argv[1:]
    d = args[0]
    nmin = int(args[args.index('--min') + 1]) if '--min' in args else 1
    syms = {}
    for line in open(os.path.join(d, 'symbols.txt')):
        f = line.split()
        if len(f) == 4:
            syms[f[3]] = (int(f[0], 16), int(f[1], 16))

    def load(name):
        return open(os.path.join(d, name + '.bin'), 'rb').read()

    pairs = load('dbg_ipc_pairs')
    xcpu = load('dbg_ipc_xcpu')
    if '--base' in args:
        b = args[args.index('--base') + 1]
        def sub(cur, name):
            old = open(os.path.join(b, name + '.bin'), 'rb').read()
            n = len(cur) // 4
            c, o = struct.unpack('<%dI' % n, cur), struct.unpack('<%dI' % n, old)
            return struct.pack('<%dI' % n, *[(x - y) & 0xffffffff
                                             for x, y in zip(c, o)])
        pairs = sub(pairs, 'dbg_ipc_pairs')
        xcpu = sub(xcpu, 'dbg_ipc_xcpu')
    praw = load('proc')
    vraw = load('priv')
    nproc = 256 + NR_TASKS
    psz = syms['proc'][1] // nproc
    vsz = syms['priv'][1] // NR_SYS_PROCS

    def cnt(buf, *idx):
        off = 0
        for i in idx:
            off = off * NR_SYS_PROCS + i
        return struct.unpack_from('<I', buf, 4 * off)[0]

    name, cpu = {}, {}
    for i in range(NR_SYS_PROCS):
        nr, sid = struct.unpack_from('<ih', vraw, i * vsz)
        slot = nr + NR_TASKS
        if sid != i or not 0 <= slot < nproc:
            continue
        b = praw[slot * psz:(slot + 1) * psz]
        if struct.unpack_from('<I', b, P_RTS)[0] & 1:
            n = '(free)'
        else:
            n = b[P_NAME:P_NAME + 16].split(b'\0')[0].decode('latin1')
        if n == 'init':
            n = 'user'
        name[i] = n
        cpu[i] = struct.unpack_from('<I', b, P_CPU)[0]

    def nm(i):
        return name.get(i, 'id%d' % i)

    rows = []
    for a in range(NR_SYS_PROCS):
        for b in range(a, NR_SYS_PROCS):
            k = [cnt(pairs, t, a, b) + (cnt(pairs, t, b, a) if b != a else 0)
                 for t in range(3)]
            x = cnt(xcpu, a, b) + (cnt(xcpu, b, a) if b != a else 0)
            if sum(k) >= nmin:
                rows.append((sum(k), a, b, k, x))
    rows.sort(reverse=True)
    total = sum(r[0] for r in rows) or 1
    print('%-14s %-14s %4s %4s %11s %11s %9s %9s %6s %6s' % ('a', 'b', 'cpuA',
          'cpuB', 'all', 'sync', 'notify', 'async', 'x-cpu', 'share'))
    for s, a, b, k, x in rows:
        print('%-14s %-14s %4d %4d %11d %11d %9d %9d %5.0f%% %5.1f%%' % (
              nm(a), nm(b), cpu.get(a, -1), cpu.get(b, -1), s, k[0], k[1],
              k[2], 100.0 * x / s, 100.0 * s / total))

    if '--cpus' in args:
        print()
        cpus = sorted(set(cpu.values()))
        for c in cpus:
            ids = [i for i in name if cpu[i] == c and name[i] != 'user']
            inside = sum(r[0] for r in rows if cpu.get(r[1]) == c and
                         cpu.get(r[2]) == c)
            out = sum(r[0] for r in rows if (cpu.get(r[1]) == c) !=
                      (cpu.get(r[2]) == c))
            print('cpu%d: %d msgs within, %d to/from other cpus: %s' % (
                  c, inside, out, ' '.join(sorted(nm(i) for i in ids))))


if __name__ == '__main__':
    main()
