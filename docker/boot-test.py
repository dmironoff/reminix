#!/usr/bin/env python3
"""
ReMinix: boot check of an i386 image in QEMU (docs/testing.md §1е).

Boots the system from the medium MEDIA, logs in as root on the serial
console, looks at what came up and powers the machine off. Called by
docker/build.mk (target boot-test) inside the build container.

usage: boot-test.py MODDIR IMAGE CPUS LOG
  MODDIR, IMAGE, CPUS   as for docker/run-console.sh (see docker/qemu-i386.sh)
  LOG                   serial console log; the summary goes to <log>.result
env:
  MEDIA         multiboot (default), hd, cd, usb, ram
  KARGS         extra kernel arguments (multiboot and ram only)
  ENTRY         boot loader menu entry to choose (hd, cd, usb; the image must
                be built with CONSOLE=serial so the menu is on the serial line)
  EXPECT_CPUS   expected sysctl hw.ncpuonline (default: CPUS)
  BOOT_TIMEOUT  seconds from start to the shell prompt (default 300)
  ACCEL         tcg or kvm instead of the automatic choice

Steps: [ENTRY at the boot menu] -> "login:" -> root -> uname -a,
hw.ncpuonline, mount -> halt -p -> QEMU exits. A step that times out, or
SIGTERM (make test-stop), saves a post-mortem (qemu-postmortem.py) into
<log>.postmortem/. The last line printed is "RESULT: PASS" or
"RESULT: FAIL (<reason>)"; the exit status is 0 or 1.
"""
import os
import re
import signal
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# the root file system each medium should end up with (a line of mount)
ROOT_RE = {
    "multiboot": r"^/dev/c0d0p0 on / ",
    "hd": r"^/dev/c0d0p0 on / ",
    "cd": r"^/dev/c\dd\d on / type isofs",
    "usb": r"^bootramdisk on / type mfs",
    "ram": r"^bootramdisk on / type mfs",
}


class Console:
    """Serial console of the QEMU child: logged, searchable."""

    def __init__(self, proc, log):
        self.proc = proc
        self.log = log
        self.buf = ""
        self.pos = 0            # expect() searches from here
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            data = self.proc.stdout.read1(4096)
            if not data:
                break
            self.log.write(data)
            self.log.flush()
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
            with self.lock:
                self.buf += data.decode("latin-1").replace("\r", "")

    def send(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def expect(self, regex, deadline, stop):
        """Wait for regex after the last match; return the match or None."""
        r = re.compile(regex, re.M)
        while time.time() < deadline and not stop():
            with self.lock:
                m = r.search(self.buf, self.pos)
                if m:
                    self.pos = m.end()
                    return m
            if self.proc.poll() is not None:
                return None
            time.sleep(0.2)
        return None


def qemu_command(media, accel, cpus, moddir, img, kargs):
    """qemu_cmd and qemu_args from docker/qemu-i386.sh."""
    script = ('. "$0"; qemu_i386 "$@" || exit 2; '
              'printf "%s\\0" "$qemu_args" "${qemu_cmd[@]}"')
    out = subprocess.run(["bash", "-c", script, os.path.join(HERE, "qemu-i386.sh"),
                          media, accel, str(cpus), moddir, img, kargs],
                         stdout=subprocess.PIPE, check=True).stdout.decode()
    words = out.split("\0")[:-1]
    return words[1:], words[0]


def main():
    if len(sys.argv) != 5:
        sys.stderr.write(__doc__)
        return 2
    moddir, img, cpus, logpath = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    media = os.environ.get("MEDIA") or "multiboot"
    kargs = os.environ.get("KARGS", "")
    entry = os.environ.get("ENTRY", "")
    expect_cpus = int(os.environ.get("EXPECT_CPUS") or cpus)
    boot_timeout = int(os.environ.get("BOOT_TIMEOUT") or 300)
    accel = "kvm" if os.access("/dev/kvm", os.W_OK) else "tcg"
    accel = os.environ.get("ACCEL") or accel
    if media not in ROOT_RE:
        print(f"boot-test: unknown MEDIA={media}", file=sys.stderr)
        return 2
    if entry and media not in ("hd", "cd", "usb"):
        print(f"boot-test: ENTRY: MEDIA={media} has no boot menu", file=sys.stderr)
        return 2

    try:
        cmd, args = qemu_command(media, accel, cpus, moddir, img, kargs)
    except subprocess.CalledProcessError:
        return 2

    os.makedirs(os.path.dirname(logpath) or ".", exist_ok=True)
    base = logpath[:-4] if logpath.endswith(".log") else logpath
    monsock = f"/tmp/reminix-mon.{os.getpid()}"
    stopped = []
    signal.signal(signal.SIGTERM, lambda *a: stopped.append("SIGTERM"))
    signal.signal(signal.SIGINT, lambda *a: stopped.append("SIGINT"))

    print(f">>> boot-test: i386, MEDIA={media}, {cpus} CPU, accel={accel}, "
          f"kernel args: {args}{', menu entry ' + entry if entry else ''}")
    print(f">>> log: {logpath}", flush=True)
    start = time.time()
    log = open(logpath, "wb")
    proc = subprocess.Popen(cmd + ["-display", "none",
                                   "-monitor", f"unix:{monsock},server,nowait",
                                   "-serial", "stdio", "-no-reboot"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    con = Console(proc, log)
    stop = lambda: bool(stopped)
    deadline = start + boot_timeout
    failure = None
    found = {}

    def step(name, regex, until):
        nonlocal failure
        m = con.expect(regex, until, stop)
        if m is None and failure is None:
            failure = (f"{name}: {stopped[0]}" if stopped else
                       f"{name}: QEMU exited" if proc.poll() is not None else
                       f"{name}: timeout at {int(time.time() - start)}s")
        return m

    if entry:
        if step("boot menu", r"Choose an option", deadline):
            con.send(entry)
    if failure is None and step("login prompt", r"login: *$", deadline):
        con.send("root\r")
        time.sleep(2)
        con.send("echo BT-READY\r")
    if failure is None and step("shell", r"^BT-READY$", deadline):
        con.send("uname -a; echo BT-NCPU=$(sysctl -n hw.ncpuonline); "
                 "mount; echo BT-END\r")
    if failure is None and step("commands", r"^BT-END$", time.time() + 60):
        with con.lock:
            out = con.buf
        m = re.search(r"^BT-NCPU=(\d+)$", out, re.M)
        found["ncpu"] = int(m.group(1)) if m else None
        m = re.search(r"^(\S+) on / .*$", out, re.M)
        found["root"] = m.group(0) if m else None
        found["uname"] = (re.findall(r"^Minix .*$", out, re.M) or [None])[-1]
        if found["ncpu"] != expect_cpus:
            failure = f"hw.ncpuonline {found['ncpu']}, expected {expect_cpus}"
        elif not found["root"] or not re.match(ROOT_RE[media], found["root"]):
            failure = f"root file system: {found['root']}"
        con.send("halt -p\r")
        try:
            proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            failure = failure or "halt -p: QEMU still running after 120s"

    pmdir = None
    if proc.poll() is None:
        if failure and proc.poll() is None:
            pmdir = base + ".postmortem"
            print(f"\n>>> {failure}: saving post-mortem to {pmdir}", flush=True)
            subprocess.run(["python3", os.path.join(HERE, "qemu-postmortem.py"),
                            monsock, os.path.join(moddir, "kernel"), pmdir, str(cpus)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        proc.kill()
        proc.wait()
    time.sleep(0.5)
    log.close()
    try:
        os.unlink(monsock)
    except OSError:
        pass

    result = "PASS" if failure is None else f"FAIL ({failure})"
    lines = [
        "================ ReMinix boot test ================",
        f"media: {media}   CPUs: {cpus}   accel: {accel}   "
        f"time: {int(time.time() - start)}s",
        f"kernel args: {args}" + (f"   menu entry: {entry}" if entry else ""),
        f"uname: {found.get('uname')}",
        f"hw.ncpuonline: {found.get('ncpu')} (expected {expect_cpus})",
        f"root: {found.get('root')}",
    ]
    if pmdir:
        lines.append(f"post-mortem: {pmdir}")
    lines += [f"log: {logpath}", f"RESULT: {result}",
              "==================================================="]
    with open(base + ".result", "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n" + "\n".join(lines), flush=True)
    return 0 if failure is None else 1


if __name__ == "__main__":
    sys.exit(main())
