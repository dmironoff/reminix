#!/bin/sh
# Build the include tree the abi64 probe is compiled against, from the
# source tree (no DESTDIR needed), see docs/types-audit.md.
#   mkinc.sh <top of tree> <output dir> <32|64>
#
# The tree mirrors what build.sh installs for i386 (machine/ = the NetBSD
# i386 headers plus minix/include/arch/i386/include), so the probe sees the
# same headers as ReMinix code.  For 64 a minimal LP64 shim replaces the
# C-model types of machine/ (size_t, ssize_t, ptrdiff_t, register_t,
# LONG_*); everything MINIX-specific stays as on i386: the point is to see
# what the current headers give with 64-bit long and pointers.
#
# minix/ipc.h is copied with its whole-message check
# (sizeof(message) == 64, a negative-size array) turned into a recorded
# entry, so that the probe compiles on 64 and reports the size instead.

set -e
top=$1
out=$2
bits=$3

rm -rf "$out"
mkdir -p "$out/machine" "$out/minix"

for d in "$top/sys/arch/i386/include" "$top/minix/include/arch/i386/include"; do
	for f in "$d"/*.h; do
		ln -sf "$f" "$out/machine/"
	done
done
ln -s machine "$out/i386"
ln -s "$top/sys/arch/x86/include" "$out/x86"

# Top-level headers that the installed tree has as links into sys/.
for f in cdbr fcntl md4 md5 poll rmd160 sha1 sha2 stdarg stdbool stdint \
    syslog termios; do
	ln -s "$top/sys/sys/$f.h" "$out/$f.h"
done
ln -s "$top/sys/sys/exec_elf.h" "$out/elf.h"
ln -s machine/float.h "$out/float.h"

sed 's/^typedef int _ASSERT_message\[.*\];$/_ASSERT_MSG_UNION(message);/' \
	"$top/minix/include/minix/ipc.h" > "$out/minix/ipc.h"
grep -q '^_ASSERT_MSG_UNION(message);$' "$out/minix/ipc.h"

# Replace the machine/ header $1 by a copy edited with sed script $2;
# every substitution must apply (a changed header must fail loudly).
shim() {
	src="$top/sys/arch/i386/include/$1"
	rm "$out/machine/$1"
	sed "$2" "$src" > "$out/machine/$1"
	if cmp -s "$src" "$out/machine/$1"; then
		echo "mkinc.sh: shim for $1 changed nothing" >&2
		exit 1
	fi
}

if [ "$bits" = 64 ]; then
	shim ansi.h '
		s/\(_BSD_PTRDIFF_T_[[:space:]]*\)int/\1long/
		s/\(_BSD_SIZE_T_[[:space:]]*\)unsigned int/\1unsigned long/
		s/\(_BSD_SSIZE_T_[[:space:]]*\)int/\1long/'
	shim types.h 's/^typedef int\([[:space:]]*\)register_t;/typedef long\1register_t;/'
	shim limits.h '
		s/0xffffffffUL\([[:space:]]\)/0xffffffffffffffffUL\1/
		s/0x7fffffffL\([[:space:]]\)/0x7fffffffffffffffL\1/
		s/(-0x7fffffffL-1)/(-0x7fffffffffffffffL-1)/
		s/SSIZE_MAX\([[:space:]]*\)INT_MAX/SSIZE_MAX\1LONG_MAX/
		s/LONG_BIT\([[:space:]]*\)32/LONG_BIT\164/'
	for f in ansi.h types.h; do
		grep -q 'long' "$out/machine/$f"
	done
	grep -q 'SSIZE_MAX[[:space:]]*LONG_MAX' "$out/machine/limits.h"
	grep -q 'register_t' "$out/machine/types.h"
fi
