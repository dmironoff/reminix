#!/usr/bin/env python3
"""
scan64: whole-tree 64-bit portability scan with clang (docs/types-audit.md).

Every C source of the kernel, servers, file systems, network services,
drivers and their libraries is compiled -fsyntax-only with the flags of the
real build (taken from nbmake in the build container) in several modes:

  m32   i586, headers as installed                  (the baseline)
  m64   x86_64 with the i386 headers and an LP64 machine/ shim
        (-D__i386__: the existing i386 code paths, 64-bit long/pointers)
  p64   i586 with phys_bytes = uint64_t             (64-bit physical addresses)
  a16   i586 with vir_bytes, phys_bytes = unsigned int (the definitions of
        step A1.6: vir_addr_t = uintptr_t; formats not using PRIx* show up)

each for the uniprocessor and the SMP kernel configuration.  Diagnostics
that appear in m64/p64 but not in m32 are the raw list of places to look at;
record layouts (clang -fdump-record-layouts) that differ are the structures
whose layout depends on the width.  The message size checks of ipc.h are
switched off here (abi64 measures them).  The output is raw material: each
place still has to be read.

Needs: a built tree (obj/i386, obj/i386-smp: DESTDIR, tooldir, as after
"make -C docker -f build.mk hdimage [SMP=yes]"), docker with the reminix-build
image, clang (clang-18 or newer, -Wshorten-64-to-32) on the host.

  scan64.py [--top TREE] [--obj OBJDIR] [--out DIR] [--modes m32,m64,p64,a16]
            [--no-layouts] [--jobs N]

Results in --out (default <obj>/abi64-scan): new-<mode>.txt (file:line,
flag, message), ldiff-<mode>.txt (type, i386 vs <mode> size/align),
summary.txt.
"""
import argparse, collections, json, os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CONTAINER = "/work/reminix"

# Component directories (programs and libraries) that are scanned.
ROOTS = ["minix/kernel", "minix/servers", "minix/fs", "minix/net",
         "minix/drivers", "minix/lib"]
SKIP = [r"^minix/lib/libc$", r"libgcc_s_empty", r"libmagicrt", r"/examples"]

WARN = [
    "-Wshorten-64-to-32", "-Wpointer-to-int-cast", "-Wint-to-pointer-cast",
    "-Wvoid-pointer-to-int-cast", "-Wformat", "-Wconstant-conversion",
    "-Wtautological-constant-out-of-range-compare", "-Wshift-count-overflow",
    "-Wbitfield-constant-conversion", "-Wint-conversion",
    "-Wincompatible-pointer-types", "-Wpointer-integer-compare",
]
RELAX = [
    "-Wno-error", "-Wno-error=implicit-function-declaration",
    "-Wno-error=int-conversion", "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=incompatible-pointer-types", "-Wno-error=implicit-int",
    "-Wno-error=return-type", "-ferror-limit=0", "-fno-caret-diagnostics",
    "-fdiagnostics-show-option",
]
# Warnings that are not about the width of types.
NOISE = {"-Wdeprecated-non-prototype", "-Wformat-pedantic",
         "-Wunused-but-set-variable", "-Wstrict-prototypes",
         "-Wmisleading-indentation", "-Wempty-body", "-Winfinite-recursion",
         "-Wmissing-noreturn", "-Wswitch"}

DIAG = re.compile(r"^(/[^:]+):(\d+):(\d+): (warning|error|fatal error): "
                  r"(.*?)(?: \[(-W[^\]]+|[^\]]*)\])?$")


def find_clang():
    for c in ("clang-20", "clang-19", "clang-18", "clang"):
        if shutil.which(c):
            return c
    sys.exit("scan64: no clang on the host")


def component_dirs(top):
    out = []
    for root in ROOTS:
        for d, _, files in os.walk(os.path.join(top, root)):
            if "Makefile" not in files:
                continue
            rel = os.path.relpath(d, top)
            if any(re.search(p, rel) for p in SKIP):
                continue
            mk = open(os.path.join(d, "Makefile"), errors="replace").read()
            if re.search(r"^(PROG|LIB)\s*[:+?]?=", mk, re.M):
                out.append(rel)
    # MINIX system call stubs (messages built on the user side) live in libc.
    return sorted(out) + ["lib/libc"]


def collect_vars(args, smp, dirs):
    """nbmake -V of CPPFLAGS/CFLAGS/SRCS/.PATH per directory, in the build
    container (read-only mount), for the UP or SMP configuration."""
    arch = "i386-smp" if smp else "i386"
    extra = "CONFIG_SMP=y CONFIG_MAX_CPUS=8" if smp else ""
    script = os.path.join(args.out, "vars.sh")
    with open(script, "w") as f:
        f.write("#!/bin/bash\n")
        for d in dirs:
            f.write(f"echo '@@DIR {d}'\n")
            f.write(f"(cd {CONTAINER}/{d} && {CONTAINER}/obj/{arch}/tooldir/bin/"
                    f"nbmake-i386 {extra} -V '${{CPPFLAGS}}' -V '${{CFLAGS}}' "
                    f"-V '${{SRCS}}' -V '${{.PATH}}' 2>&1)\n")
    p = subprocess.run(
        ["docker", "run", "--rm",
         "-e", f"DESTDIR={CONTAINER}/obj/{arch}/destdir.i386",
         "-v", f"{args.repo}:{CONTAINER}:ro", "-v", f"{args.out}:/scan",
         args.image, "bash", "/scan/vars.sh"],
        capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"scan64: nbmake in the container failed:\n{p.stderr}")
    dirs_v = []
    lines = p.stdout.split("\n")
    for i, l in enumerate(lines):
        if l.startswith("@@DIR "):
            cpp, cfl, srcs, vpath = (lines[i + 1:i + 5] + [""] * 4)[:4]
            dirs_v.append(dict(dir=l[6:].strip(), cpp=cpp.split(), cfl=cfl.split(),
                               srcs=srcs.split(), path=vpath.split()))
    return dirs_v


def make_sysroot(args, mode):
    """Copy of the installed headers; the LP64 shim (as mkinc.sh) for m64,
    64-bit phys_bytes for p64, 32-bit unsigned int vir_bytes and phys_bytes
    for a16, the PRIx* of minix/memtypes.h to match; message size checks off
    for all three."""
    root = os.path.join(args.out, f"sysroot-{mode}")
    inc = root + "/usr/include"
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root + "/usr")
    shutil.copytree(args.obj + "/i386/destdir.i386/usr/include", inc, symlinks=True)
    src = os.path.join(args.top, "minix/include/minix")
    for f in os.listdir(src):
        if f.endswith(".h"):
            shutil.copy(os.path.join(src, f), os.path.join(inc, "minix", f))

    def sub(path, pat, rep):
        s = open(path).read()
        s2, n = re.subn(pat, rep, s)
        if n == 0:
            sys.exit(f"scan64: pattern {pat!r} not found in {path}")
        open(path, "w").write(s2)

    m = inc + "/i386"
    if mode == "m64":
        sub(m + "/ansi.h", r"(_BSD_PTRDIFF_T_\s+)int", r"\1long")
        sub(m + "/ansi.h", r"(_BSD_SIZE_T_\s+)unsigned int", r"\1unsigned long")
        sub(m + "/ansi.h", r"(_BSD_SSIZE_T_\s+)int", r"\1long")
        sub(m + "/types.h", r"typedef int(\s+)register_t;", r"typedef long\1register_t;")
        sub(m + "/limits.h", r"0xffffffffUL(\s)", r"0xffffffffffffffffUL\1")
        sub(m + "/limits.h", r"0x7fffffffL(\s)", r"0x7fffffffffffffffL\1")
        sub(m + "/limits.h", r"\(-0x7fffffffL-1\)", "(-0x7fffffffffffffffL-1)")
        sub(m + "/limits.h", r"SSIZE_MAX(\s+)INT_MAX", r"SSIZE_MAX\1LONG_MAX")
        sub(m + "/limits.h", r"LONG_BIT(\s+)32", r"LONG_BIT\g<1>64")
    if mode == "p64":
        sub(inc + "/minix/type.h", r"typedef unsigned long phys_bytes;",
            "typedef uint64_t phys_bytes;")
        sub(inc + "/minix/memtypes.h", r'(#define\s+PRIxPHYS\s+)"lx"', r'\1"llx"')
    if mode == "a16":
        sub(inc + "/minix/type.h", r"typedef unsigned long phys_bytes;",
            "typedef unsigned int phys_bytes;")
        sub(inc + "/minix/type.h", r"typedef long unsigned int vir_bytes;",
            "typedef unsigned int vir_bytes;")
        sub(inc + "/minix/memtypes.h", r'(#define\s+PRIxPHYS\s+)"lx"', r'\1"x"')
        sub(inc + "/minix/memtypes.h", r'(#define\s+PRIxVIR\s+)"lx"', r'\1"x"')
    if mode in ("m64", "p64", "a16"):
        sub(inc + "/minix/ipcconst.h", r"typedef int _ASSERT_##msg_type\[[^\]]*\]",
            "struct _ASSERT_##msg_type { int dummy; }")
        sub(inc + "/minix/ipc.h", r"typedef int _ASSERT_message\[[^\]]*\];",
            "struct _ASSERT_message { int dummy; };")
    return root


def map_path(args, p):
    if p.startswith(CONTAINER + "/obj/"):
        return args.repo + p[len(CONTAINER):]
    if p.startswith(CONTAINER):
        return args.top + p[len(CONTAINER):]
    return p


def find_src(args, d, s):
    if not s.endswith(".c"):
        return None
    for p in d["path"]:
        if p != ".":
            cand = os.path.join(map_path(args, p), s)
            if os.path.isfile(cand):
                return cand
    cand = os.path.join(args.top, d["dir"], s)
    return cand if os.path.isfile(cand) else None


def command(args, d, src, mode, root, extra):
    if mode == "m64":
        target = ["--target=x86_64-unknown-none-elf", "-m64", "-mno-red-zone",
                  "-D__i386__=1", "-D__i386=1"]
    else:
        target = ["--target=i586-unknown-none-elf", "-march=i586"]
    flags = []
    for f in d["cpp"] + d["cfl"]:
        if f.startswith("--sysroot") or f == "-Werror" or f.startswith("-O"):
            continue
        if f.startswith("-I"):
            f = "-I" + map_path(args, f[2:])
        flags.append(f)
    after = []
    for p in ("minix", "", "sys", "minix/fs", "minix/servers", "sys/lib/libsa",
              "common/lib/libc/stdlib"):
        after += ["-idirafter", os.path.join(args.top, p)]
    return [args.clang] + target + [
        "-nostdinc", "-isystem", root + "/usr/include/clang-3.6",
        "-isystem", root + "/usr/include",
        "-D__minix=3", "-D__minix__=3", "-D__Minix__=3", "-D__unix__=1",
        "-std=gnu99", "-fsyntax-only"] + flags + after + extra + [src]


def rel(args, root, f):
    return f.replace(args.top + "/", "").replace(root + "/usr/include/", "<sysroot>/")


def run_diag(job):
    args, d, src, mode, root = job
    p = subprocess.run(command(args, d, src, mode, root, RELAX + WARN),
                       capture_output=True, text=True,
                       cwd=os.path.join(args.top, d["dir"]))
    out = []
    for line in p.stderr.splitlines():
        m = DIAG.match(line)
        if m and (m.group(6) or "") not in NOISE:
            out.append((rel(args, root, m.group(1)), int(m.group(2)),
                        m.group(6) or m.group(4), m.group(5)))
    return out, (p.returncode != 0)


HDR = re.compile(r"^\s*0 \| (struct|union) (.*)$")


def run_layout(job):
    args, d, src, mode, root = job
    cmd = [c for c in command(args, d, src, mode, root, []) if not c.startswith("-W")]
    cmd += ["-w", "-Xclang", "-fdump-record-layouts",
            "-Xclang", "-fdump-record-layouts-complete"]
    p = subprocess.run(cmd, capture_output=True, text=True,
                       cwd=os.path.join(args.top, d["dir"]))
    res = {}
    for b in p.stdout.split("*** Dumping AST Record Layout"):
        lines = [l for l in b.strip("\n").split("\n") if l.strip()]
        if not lines or not HDR.match(lines[0]):
            continue
        name = rel(args, root, " ".join(HDR.match(lines[0]).groups()))
        name = re.sub(r"(\(unnamed at [^:]+:\d+):\d+\)", r"\1)", name)
        offs, size = [], None
        for l in lines[1:]:
            if "[sizeof=" in l:
                size = l.split("[")[1].rstrip("]")
                break
            mm = re.match(r"^\s*(\d+)(?::\d+-\d+)? \|   \S", l)
            if mm:
                offs.append(int(mm.group(1)))
        res[name] = (size, offs)
    return res


def scan(args, mode, smp, dirs_v, layouts):
    root = make_sysroot(args, mode)
    jobs = []
    for d in dirs_v:
        for s in d["srcs"]:
            src = find_src(args, d, s)
            if src and (d["dir"] != "lib/libc" or "/minix/lib/libc/" in src):
                jobs.append((args, d, src, mode, root))
    diags, failed = set(), 0
    with ThreadPoolExecutor(args.jobs) as ex:
        for out, bad in ex.map(run_diag, jobs):
            diags.update(out)
            failed += bad
    lay = {}
    if layouts:
        with ThreadPoolExecutor(args.jobs) as ex:
            for res in ex.map(run_layout, jobs):
                for k, v in res.items():
                    lay.setdefault(k, v)
    return diags, lay, len(jobs), failed


def main():
    ap = argparse.ArgumentParser()
    top_default = os.path.abspath(os.path.join(HERE, "../../../.."))
    ap.add_argument("--top", default=top_default, help="source tree to scan")
    ap.add_argument("--repo", default=None,
                    help="checkout with obj/ (default: --top)")
    ap.add_argument("--obj", default=None, help="obj dir (default: <repo>/obj)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--image", default="reminix-build:latest")
    ap.add_argument("--modes", default="m32,m64,p64")
    ap.add_argument("--no-layouts", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    args = ap.parse_args()
    args.top = os.path.abspath(args.top)
    args.repo = os.path.abspath(args.repo or args.top)
    args.obj = os.path.abspath(args.obj or os.path.join(args.repo, "obj"))
    args.out = os.path.abspath(args.out or os.path.join(args.obj, "abi64-scan"))
    args.clang = find_clang()
    os.makedirs(args.out, exist_ok=True)
    modes = args.modes.split(",")
    if modes[0] != "m32":
        modes = ["m32"] + [m for m in modes if m != "m32"]

    dirs = component_dirs(args.top)
    summary = [f"tree {args.top}, {len(dirs)} directories, clang {args.clang}"]
    base, seen = {}, collections.defaultdict(set)
    for smp in (False, True):
        tag = "smp" if smp else "up"
        dirs_v = collect_vars(args, smp, dirs)
        for mode in modes:
            diags, lay, n, failed = scan(args, mode, smp, dirs_v,
                                         not args.no_layouts and not smp)
            summary.append(f"{mode}/{tag}: {n} files, {failed} with errors, "
                           f"{len(diags)} diagnostics")
            if mode == "m32":
                base[tag] = (diags, lay)
                continue
            # SMP: only what the UP configuration has not shown already.
            new = sorted(d for d in diags - base[tag][0]
                         if d[:3] not in seen[mode])
            seen[mode].update(d[:3] for d in new)
            with open(os.path.join(args.out, f"new-{mode}.txt"), "a" if smp else "w") as f:
                for fl, ln, flag, msg in new:
                    f.write(f"{fl}:{ln}\t{flag}\t{msg}\t[{tag}]\n")
            cnt = collections.Counter(n[2] for n in new)
            summary.append(f"  new in {mode}/{tag}: {len(new)} " + str(dict(cnt)))
            if lay:
                b = base[tag][1]
                diff = sorted(k for k in b if k in lay and b[k] != lay[k])
                with open(os.path.join(args.out, f"ldiff-{mode}.txt"), "w") as f:
                    for k in diff:
                        f.write(f"{k}\t{b[k][0]}\t{lay[k][0]}\n")
                summary.append(f"  layouts differing in {mode}: {len(diff)} of {len(b)}")
    open(os.path.join(args.out, "summary.txt"), "w").write("\n".join(summary) + "\n")
    print("\n".join(summary))
    print(f"results: {args.out}")


main()
