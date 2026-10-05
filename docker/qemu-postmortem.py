#!/usr/bin/env python3
"""
ReMinix: post-mortem capture of a hung QEMU guest (docs/testing.md).

Talks to the QEMU human monitor over a unix socket, saves the state of every
virtual CPU and the interesting kernel memory regions, so that a hang can be
analysed offline without anybody attaching a debugger by hand.

usage: qemu-postmortem.py MONITOR_SOCKET KERNEL_ELF OUTDIR NCPUS [cont]

With "cont" the machine is resumed afterwards (a snapshot of a running system).
env TARGET=earm (as set by run-tests.sh / run-console.sh): no x86-only parts
(LAPIC, PIC, the stack through $esp, the VGA screen).

Writes into OUTDIR:
  monitor.txt     info cpus / registers / lapic / stack per CPU, info pic, info irq
  symbols.txt     nm -n -S of the kernel (addresses of everything below)
  kernel          copy of the kernel ELF (objdump -d)
  <symbol>.bin    raw kernel memory of the regions listed in REGIONS
  bkl.txt         decoded BKL owner and last lock/unlock events (SMP kernel
                  built with BKL_DEBUG=yes)
  vga.txt         text screen of the VGA console: a kernel panic is printed
                  there directly, the serial console never shows it
  kmessages.txt   printable text of the kernel message buffer
"""
import bisect
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

# Kernel symbols worth saving. A name maps to its own size (from nm -S);
# a (start, end) pair saves the range between two symbols.
REGIONS = [
    "proc", "priv", "__cpu_local_vars", "cpus", "ncpus", "bsp_cpu_id",
    "big_kernel_lock", "boot_lock", "dispq_lock", "smp_cpu_lock",
    "kinfo", "kclockinfo", "kmessages",
    # BKL debugging, smp.c (SMP kernels built with BKL_DEBUG=yes)
    "bkl_owner_cpu", "bkl_owner_pc", "bkl_trace_seq", "bkl_trace",
    "bkl_relock_pc", "bkl_relock_owner",
    # clock debugging (tickdebug=N), APIC timer calibration
    "tick_debug_ticks", "tick_debug_busy", "lapic_bus_freq",
    # per CPU [timer_int_handler, context_stop_idle, sched IPI, idle halts,
    #          hardware IRQs taken]
    "dbg_cpu_events",
    # per CPU, 64 bytes each: [kernel_ticks u64, bkl_ticks u64, bkl_tries,
    # bkl_succ] (glo.h); sched IPI requests [flags, data] (smp.c, static)
    "bkl_stats", "sched_ipi_data",
    "lapic_addr", "lapic_eoi_addr",
    # [tag, cpu, tsc>>8, cr3, lapic_eoi_addr, ISR 224..255] (arch_clock.c)
    "dbg_boot_ev", "dbg_boot_nev",
    ("k_stacks_start", "k_stacks_end"),
]
MAX_REGION = 4 << 20


class Monitor:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(30)
        self.s.connect(path)
        self._read_prompt()

    def _read_prompt(self):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            chunk = self.s.recv(65536)
            if not chunk:
                break
            buf += chunk
        return buf.decode("utf-8", "replace")

    def cmd(self, line):
        self.s.sendall(line.encode() + b"\n")
        out = self._read_prompt()
        # drop the echoed command and the trailing prompt, strip terminal codes
        out = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", out)
        out = out.replace("\r", "")
        if out.endswith("(qemu) "):
            out = out[: -len("(qemu) ")]
        lines = out.split("\n")
        if lines and line in lines[0]:
            lines = lines[1:]
        return "\n".join(lines).rstrip() + "\n"


def kernel_symbols(kernel):
    syms = {}
    out = subprocess.run(["nm", "-n", "-S", kernel], capture_output=True,
                         text=True).stdout
    for l in out.splitlines():
        f = l.split()
        if len(f) == 4:
            syms[f[3]] = (int(f[0], 16), int(f[1], 16))
        elif len(f) == 3:
            syms.setdefault(f[2], (int(f[0], 16), 0))
    return syms, out


def text_symbolizer(syms):
    text = sorted((a, n) for n, (a, sz) in syms.items())

    def name(addr):
        i = bisect.bisect_right(text, (addr, "\xff")) - 1
        if addr == 0 or i < 0:
            return "0x%08x" % addr
        return "0x%08x %s+0x%x" % (addr, text[i][1], addr - text[i][0])
    return name


def u32(outdir, name):
    path = os.path.join(outdir, name + ".bin")
    if not os.path.exists(path):
        return None
    return struct.unpack("<I", open(path, "rb").read(4))[0]


def bkl_report(outdir, syms):
    """Decode the BKL debugging state of smp.c into bkl.txt."""
    if u32(outdir, "bkl_owner_cpu") is None:
        return
    name = text_symbolizer(syms)
    out = []
    owner = u32(outdir, "bkl_owner_cpu")
    lock = u32(outdir, "big_kernel_lock")
    out.append("big_kernel_lock = %s" % lock)
    out.append("owner cpu       = %s" % ("none" if owner == 0xffffffff else owner))
    out.append("owner pc        = %s  (last releaser if free)"
               % name(u32(outdir, "bkl_owner_pc")))
    relock = u32(outdir, "bkl_relock_pc")
    if relock:
        out.append("RE-LOCK by holder at %s, lock taken at %s"
                   % (name(relock), name(u32(outdir, "bkl_relock_owner"))))
    seq = u32(outdir, "bkl_trace_seq")
    raw = open(os.path.join(outdir, "bkl_trace.bin"), "rb").read()
    events = [struct.unpack_from("<IIIHH", raw, o) for o in range(0, len(raw) - 15, 16)]
    events = sorted(e for e in events if e[1] and e[0] < seq)
    out.append("")
    out.append("last %d of %d BKL events (oldest first):" % (len(events), seq))
    for s_, pc, caller, cpu, op in events:
        out.append("%8d cpu%d %-6s %-40s <- %s"
                   % (s_, cpu, "lock" if op else "unlock", name(pc), name(caller)))
    with open(os.path.join(outdir, "bkl.txt"), "w") as f:
        f.write("\n".join(out) + "\n")


def text_reports(outdir):
    """vga.txt from vga.bin, kmessages.txt from kmessages.bin."""
    def printable(b):
        return "".join(chr(c) if 32 <= c < 127 or c == 10 else "" for c in b)
    vga = os.path.join(outdir, "vga.bin")
    if os.path.exists(vga):
        b = open(vga, "rb").read()
        rows = ["".join(chr(b[(r * 80 + c) * 2]) if 32 <= b[(r * 80 + c) * 2] < 127
                        else " " for c in range(80)).rstrip()
                for r in range(len(b) // 160)]
        with open(os.path.join(outdir, "vga.txt"), "w") as f:
            f.write("\n".join(rows).rstrip("\n") + "\n")
    km = os.path.join(outdir, "kmessages.bin")
    if os.path.exists(km):
        with open(os.path.join(outdir, "kmessages.txt"), "w") as f:
            f.write(printable(open(km, "rb").read()) + "\n")


def main():
    if len(sys.argv) not in (5, 6) or (len(sys.argv) == 6 and sys.argv[5] != "cont"):
        print(__doc__, file=sys.stderr)
        return 2
    sock, kernel, outdir, ncpus = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    resume = len(sys.argv) == 6
    os.makedirs(outdir, exist_ok=True)

    syms, nm_out = kernel_symbols(kernel)
    with open(os.path.join(outdir, "symbols.txt"), "w") as f:
        f.write(nm_out)
    # keep the exact kernel binary for offline disassembly (objdump -d)
    shutil.copy(kernel, os.path.join(outdir, "kernel"))

    x86 = os.environ.get("TARGET", "i386") == "i386"
    mon = Monitor(sock)
    mon.cmd("stop")                     # freeze all vCPUs for a consistent view
    report = []
    report.append("### info cpus\n" + mon.cmd("info cpus"))
    for cpu in range(ncpus):
        mon.cmd("cpu %d" % cpu)
        report.append("### cpu %d: info registers\n" % cpu + mon.cmd("info registers"))
        if not x86:
            continue
        report.append("### cpu %d: info lapic\n" % cpu + mon.cmd("info lapic"))
        # raw stack of the vCPU (virtual addresses through its own CR3);
        # return addresses can be looked up in symbols.txt
        report.append("### cpu %d: stack (x/64xw $esp)\n" % cpu + mon.cmd("x/64xw $esp"))
    mon.cmd("cpu 0")
    if x86:
        report.append("### info pic\n" + mon.cmd("info pic"))
    report.append("### info irq\n" + mon.cmd("info irq"))

    saved = []
    for r in REGIONS:
        if isinstance(r, tuple):
            if r[0] not in syms or r[1] not in syms:
                continue
            name, addr = r[0], syms[r[0]][0]
            size = syms[r[1]][0] - addr
        else:
            if r not in syms:
                continue
            name, (addr, size) = r, syms[r]
            if size == 0:
                size = 4
        if size <= 0 or size > MAX_REGION:
            continue
        path = os.path.join(os.path.abspath(outdir), name + ".bin")
        # the file name must be quoted: unquoted, HMP parses "SIZE /path" as
        # the expression SIZE / path and fails with "invalid char"
        res = mon.cmd('memsave 0x%x %d "%s"' % (addr, size, path))
        saved.append("%-20s 0x%08x %8d %s" % (name, addr, size, res.strip()))
    # VGA text mode screen, 80x25 (char, attribute) pairs, physical 0xb8000
    if x86:
        vga = os.path.join(os.path.abspath(outdir), "vga.bin")
        mon.cmd('pmemsave 0xb8000 4000 "%s"' % vga)
    report.append("### saved regions (name, address, size)\n" + "\n".join(saved) + "\n")
    if resume:
        mon.cmd("cont")

    with open(os.path.join(outdir, "monitor.txt"), "w") as f:
        f.write("\n".join(report))
    try:
        text_reports(outdir)
    except Exception as e:
        print("vga.txt/kmessages.txt not written: %s" % e)
    try:
        bkl_report(outdir, syms)
    except Exception as e:              # the dump itself is still useful
        print("bkl.txt not written: %s" % e)
    print("post-mortem saved to %s (%d regions)" % (outdir, len(saved)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
