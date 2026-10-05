#!/usr/bin/env python3
"""Decode a ReMinix i386 SMP post-mortem / snapshot directory.

Usage: snapdump.py <dir> [--all]

Reads proc.bin, __cpu_local_vars.bin, dbg_cpu_events.bin, symbols.txt.
Offsets are for the i386 SMP kernel (struct proc 576 bytes); the
__cpu_local_vars stride is taken from the symbol size / number of cpus
(CONFIG_MAX_CPUS from dbg_cpu_events: 5 u32 per cpu).
"""
import os, struct, sys

NR_TASKS = 5
P_RTS, P_MISC, P_PRIO, P_CPU = 0x50, 0x54, 0x58, 0x6c
P_NEXTREADY, P_NAME, P_EP = 0xdc, 0x100, 0x110
P_CALLER_Q, P_Q_LINK, P_GETFROM, P_SENDTO = 0xe0, 0xe4, 0xe8, 0xec
# <minix/endpoint.h>: _ENDPOINT_SLOT_TOP = 32768 - MAX_NR_TASKS (1023)
MAGIC_EP = {31744: 'ANY', 31743: 'NONE', 31742: 'SELF'}
# __cpu_local_vars fields (objdump of lin_lin_copy/enqueue, cpulocals.h)
CL_PROC_PTR, CL_BILL_PTR, CL_PTPROC = 0x0, 0x4, 0x254
CL_RUNQ_HEAD, CL_RUNQ_TAIL, CL_STARVED, CL_IDLE = 0x258, 0x298, 0x2d8, 0x2dc
CL_FPU_OWNER, CL_VMINHIBIT_HIT = 0x300, 0x304
NR_SCHED_QUEUES = 16

RTS = ['SLOT_FREE', 'PROC_STOP', 'SENDING', 'RECEIVING', 'SIGNALED',
       'SIG_PENDING', 'P_STOP', 'NO_PRIV', 'NO_ENDPOINT', 'VMINHIBIT',
       'PAGEFAULT', 'VMREQUEST', 'VMREQTARGET', None, 'PREEMPTED',
       'NO_QUANTUM', 'BOOTINHIBIT']
MISC = {0x1: 'REPLY_PEND', 0x8: 'KCALL_RESUME', 0x40: 'DELIVERMSG',
        0x80: 'SIG_DELAY', 0x1000: 'FPU_INIT', 0x2000: 'SENDING_FROM_KERNEL',
        0x4000: 'CONTEXT_SET', 0x10000: 'FLUSH_TLB', 0x20000: 'SENDA_VM_MISS',
        0x80000: 'MSGFAILED', 0x100000: 'NICED', 0x200000: 'YIELD',
        0x400000: 'STARVE_SLOT'}


def flags(v, names):
    out = []
    if isinstance(names, list):
        for i, n in enumerate(names):
            if v & (1 << i):
                out.append(n or hex(1 << i))
                v &= ~(1 << i)
    else:
        for b, n in names.items():
            if v & b:
                out.append(n)
                v &= ~b
    if v:
        out.append(hex(v))
    return '|'.join(out) or '-'


def main():
    d = sys.argv[1]
    show_all = '--all' in sys.argv
    syms = {}
    for line in open(os.path.join(d, 'symbols.txt')):
        f = line.split()
        if len(f) == 4:
            syms[f[3]] = (int(f[0], 16), int(f[1], 16))
    proc_base, proc_size = syms['proc']
    nproc = 256 + NR_TASKS
    psz = proc_size // nproc
    praw = open(os.path.join(d, 'proc.bin'), 'rb').read()
    maxcpu = os.path.getsize(os.path.join(d, 'dbg_cpu_events.bin')) // 20

    cl_base, cl_size = syms['__cpu_local_vars']

    def pname(addr):
        if addr == 0:
            return '-'
        if cl_base <= addr < cl_base + cl_size:
            return 'idle@cpu%d' % ((addr - cl_base) // (cl_size // maxcpu))
        i, r = divmod(addr - proc_base, psz)
        if r or not 0 <= i < nproc:
            return '?%08x' % addr
        b = praw[i * psz:(i + 1) * psz]
        return '%s(%d)' % (b[P_NAME:P_NAME + 16].split(b'\0')[0].decode('latin1'),
                           struct.unpack_from('<i', b, P_EP)[0])

    names = {}
    for i in range(nproc):
        b = praw[i * psz:(i + 1) * psz]
        if not struct.unpack_from('<I', b, P_RTS)[0] & 1:
            names[struct.unpack_from('<i', b, P_EP)[0]] = \
                b[P_NAME:P_NAME + 16].split(b'\0')[0].decode('latin1')

    def epname(ep):
        if ep in MAGIC_EP:
            return MAGIC_EP[ep]
        return '%s(%d)' % (names.get(ep, '?'), ep)

    print('struct proc %d bytes' % psz)
    print('%5s %-14s %7s %4s %3s  %-28s %-24s %s' % ('slot', 'name', 'ep',
          'prio', 'cpu', 'rts', 'misc', 'ipc'))
    for i in range(nproc):
        b = praw[i * psz:(i + 1) * psz]
        rts, misc, prio = struct.unpack_from('<IIb', b, P_RTS)
        if rts & 1:
            continue
        cpu = struct.unpack_from('<I', b, P_CPU)[0]
        name = b[P_NAME:P_NAME + 16].split(b'\0')[0].decode('latin1')
        ep = struct.unpack_from('<i', b, P_EP)[0]
        if not show_all and i < NR_TASKS:
            continue
        ipc = []
        if rts & 0x4:
            ipc.append('-> ' + epname(struct.unpack_from('<i', b, P_SENDTO)[0]))
        if rts & 0x8:
            ipc.append('<- ' + epname(struct.unpack_from('<i', b, P_GETFROM)[0]))
        q, waiting = struct.unpack_from('<I', b, P_CALLER_Q)[0], []
        while q and len(waiting) < 64:
            waiting.append(pname(q))
            j = (q - proc_base) // psz
            q = struct.unpack_from('<I', praw, j * psz + P_Q_LINK)[0]
        if waiting:
            ipc.append('senders: ' + ' '.join(waiting))
        print('%5d %-14s %7d %4d %3d  %-28s %-24s %s' % (i - NR_TASKS, name, ep,
              prio, cpu, flags(rts, RTS), flags(misc, MISC), '; '.join(ipc)))

    ev = open(os.path.join(d, 'dbg_cpu_events.bin'), 'rb').read()
    # ncpus drops to 1 when the cpus are stopped (panic): count the cpus
    # that took timer interrupts instead
    ncpus = max(c + 1 for c in range(maxcpu)
                if struct.unpack_from('<I', ev, c * 20)[0]) if any(ev) else 1
    clraw = open(os.path.join(d, '__cpu_local_vars.bin'), 'rb').read()
    stride = syms['__cpu_local_vars'][1] // maxcpu
    print('\n__cpu_local_vars stride %#x, %d cpus' % (stride, ncpus))
    for c in range(ncpus):
        b = clraw[c * stride:(c + 1) * stride]
        g = lambda off: struct.unpack_from('<I', b, off)[0]
        e = struct.unpack_from('<5I', ev, c * 20)
        print('cpu%d current=%s bill=%s ptproc=%s idle=%d starved=%s '
              'fpu=%s vminhibit_hit=%s' % (c, pname(g(CL_PROC_PTR)),
              pname(g(CL_BILL_PTR)), pname(g(CL_PTPROC)), g(CL_IDLE),
              pname(g(CL_STARVED)), pname(g(CL_FPU_OWNER)),
              pname(g(CL_VMINHIBIT_HIT))))
        print('     events: timer=%d stop_idle=%d sched_ipi=%d hlt=%d irq=%d' % e)
        for q in range(NR_SCHED_QUEUES):
            p, chain = g(CL_RUNQ_HEAD + 4 * q), []
            while p and len(chain) < 64:
                chain.append(pname(p))
                i = (p - proc_base) // psz
                p = struct.unpack_from('<I', praw, i * psz + P_NEXTREADY)[0]
            if chain:
                print('     q%-2d %s' % (q, ' '.join(chain)))


if __name__ == '__main__':
    main()
