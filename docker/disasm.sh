#!/bin/sh
# ReMinix — эталонные дизассемблеры для подшагов, которые не должны менять
# машинный код (docs/work-plan.md §3). Запускается на хосте, без Docker:
# objdump берётся из кросс-тулчейна дерева (obj/<flavor>/tooldir).
#
#   docker/disasm.sh snapshot <метка> [flavor ...]
#       flavor — i386, i386-smp (по умолчанию оба). Для каждого ELF-файла
#       obj/<flavor>/minix/{kernel,servers,drivers,fs,net,commands,usr.bin,
#       usr.sbin} (кроме тестов и .o) и каждой статической lib*.a из
#       obj/<flavor>/{minix/lib,lib} — objdump -d -s (код и содержимое всех
#       секций) в obj/disasm/<метка>/<flavor>/<путь>.dis.gz, список — MANIFEST.
#   docker/disasm.sh compare [--funcs] <метка-до> <метка-после> [flavor ...]
#       сравнить два снимка: какие файлы различаются, появились, пропали.
#       Код возврата 0 — снимки совпадают. --funcs — для каждого различающегося
#       файла ещё и функции, чей код изменился (адреса и байты отброшены, цели
#       переходов — символами, константы >= 0x1000 — '#': сдвиг раскладки не
#       считается изменением; сдвиг __LINE__ в assert/panic — считается).
#
# Метка — обычно короткий хэш коммита (fc5cf831c) или имя подшага.

set -eu

top=$(cd "$(dirname "$0")/.." && pwd)
out="$top/obj/disasm"

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

[ $# -ge 2 ] || usage
cmd=$1; shift

list_files() {
	o=$1
	for d in kernel servers drivers fs net commands usr.bin usr.sbin; do
		[ -d "$o/minix/$d" ] || continue
		find "$o/minix/$d" -type f ! -name '*.o' ! -name '*.a' ! -name '*.so*' \
			-perm -u+x
	done | while read -r f; do
		# только ELF
		[ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] && echo "$f"
	done
	find "$o/minix/lib" "$o/lib" -type f -name 'lib*.a' \
		! -name '*_pic.a' ! -name '*_p.a' ! -name '*_g.a'
}

# Функции, чей нормализованный код различается в двух .dis.gz.
funcdiff() {
	python3 -I - "$1" "$2" <<'PY'
import gzip, re, sys

FUNC = re.compile(r'^[0-9a-f]+ <(.+)>:$')
INSN = re.compile(r'^\s*[0-9a-f]+:\t(?:[0-9a-f]{2} )+\s*\t?(.*)$')
BIG = re.compile(r'0x[0-9a-f]{4,}')
TGT = re.compile(r'\b[0-9a-f]+ (<[^>]+>)')


def load(path):
    funcs, cur, member, text = {}, None, '', False
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('Disassembly of section'):
                text = True
                continue
            if line.startswith(('Contents of section', 'In archive')):
                text = False
            if line.endswith('file format elf32-i386-minix'):
                member = line.split(':')[0]
                continue
            m = FUNC.match(line)
            if m and text:
                cur = member + ':' + m.group(1)
                funcs.setdefault(cur, [])
                continue
            m = INSN.match(line)
            if m and cur and text:
                funcs[cur].append(BIG.sub('#', TGT.sub(r'\1', m.group(1))))
    return funcs


a, b = load(sys.argv[1]), load(sys.argv[2])
out = [('изменена', k) for k in a if k in b and a[k] != b[k]]
out += [('пропала ', k) for k in a if k not in b]
out += [('новая   ', k) for k in b if k not in a]
for what, k in out:
    print(f'    {what} {k}')
if not out:
    print('    функции совпадают (различие только в данных или раскладке)')
PY
}

case $cmd in
snapshot)
	label=$1; shift
	[ $# -gt 0 ] || set -- i386 i386-smp
	for fl in "$@"; do
		o="$top/obj/$fl"
		od="$o/tooldir/bin/i586-elf32-minix-objdump"
		[ -x "$od" ] || { echo "$od: нет (сначала hdimage SMP=...)" >&2; exit 1; }
		dst="$out/$label/$fl"
		rm -rf "$dst"; mkdir -p "$dst"
		list_files "$o" | sed "s|^$o/||" | LC_ALL=C sort > "$dst/MANIFEST"
		n=0
		while read -r rel; do
			mkdir -p "$dst/$(dirname "$rel")"
			# имя файла в заголовке objdump — относительное, чтобы снимки
			# разных деревьев сравнивались напрямую
			(cd "$o" && "$od" -d -s "$rel") | gzip -n > "$dst/$rel.dis.gz"
			n=$((n + 1))
		done < "$dst/MANIFEST"
		git -C "$top" rev-parse --short HEAD > "$dst/HEAD"
		git -C "$top" status --short > "$dst/STATUS"
		echo "$fl: $n файлов -> $dst ($(du -sh "$dst" | cut -f1))"
	done
	;;
compare)
	funcs=no
	if [ "${1:-}" = --funcs ]; then funcs=yes; shift; fi
	[ $# -ge 2 ] || usage
	a=$1 b=$2; shift 2
	[ $# -gt 0 ] || set -- i386 i386-smp
	rc=0
	for fl in "$@"; do
		da="$out/$a/$fl" db="$out/$b/$fl"
		for d in "$da" "$db"; do
			[ -f "$d/MANIFEST" ] || { echo "$d: нет снимка" >&2; exit 1; }
		done
		same=0 diff=0
		for rel in $(LC_ALL=C comm -23 "$da/MANIFEST" "$db/MANIFEST"); do
			echo "$fl: пропал  $rel"; rc=1
		done
		for rel in $(LC_ALL=C comm -13 "$da/MANIFEST" "$db/MANIFEST"); do
			echo "$fl: новый   $rel"; rc=1
		done
		for rel in $(LC_ALL=C comm -12 "$da/MANIFEST" "$db/MANIFEST"); do
			if cmp -s "$da/$rel.dis.gz" "$db/$rel.dis.gz"; then
				same=$((same + 1))
			else
				echo "$fl: разный  $rel"; diff=$((diff + 1)); rc=1
				[ $funcs = yes ] && funcdiff "$da/$rel.dis.gz" "$db/$rel.dis.gz"
			fi
		done
		echo "$fl: совпадают $same, различаются $diff"
	done
	[ $rc -eq 0 ] && echo "RESULT: SAME" || echo "RESULT: DIFFERENT (zdiff obj/disasm/$a/<flavor>/<файл>.dis.gz obj/disasm/$b/...)"
	exit $rc
	;;
*)
	usage
	;;
esac
