# ReMinix: QEMU command line for i386 by boot medium. Sourced (bash) by
# docker/run-console.sh and docker/boot-test.py; see docs/testing.md §1в.
#
# qemu_i386 MEDIA ACCEL CPUS MODDIR IMAGE KARGS
#   MEDIA   multiboot  kernel and modules of MODDIR loaded by QEMU directly,
#                      root file system from the disk image IMAGE (as test-i386)
#           hd | usb   the disk image IMAGE booted by its own boot loader
#           cd         the ISO IMAGE booted by its own boot loader
#           ram        kernel and modules of MODDIR (x86_ramimage.sh work
#                      directory: mod06_memory holds the root file system),
#                      no disk; IMAGE is ignored
#   KARGS   extra kernel arguments (multiboot and ram only; with a boot
#           loader the arguments come from boot.cfg of the image)
# Sets the array qemu_cmd (without the serial console and the monitor) and
# qemu_args (kernel arguments, or where they come from). Returns 2 on error.

qemu_i386() {
	local media=$1 accel=$2 cpus=$3 moddir=$4 img=$5 kargs=$6 mods
	local base=(qemu-system-i386 -machine pc,accel="$accel" -smp "$cpus")

	case "$media" in
	multiboot|ram)
		mods=$(ls "$moddir"/mod[0-9][0-9]_* 2>/dev/null | sort | paste -sd, -)
		[ -n "$mods" ] || { echo "qemu_i386: no boot modules in $moddir" >&2; return 2; }
		[ -f "$moddir/kernel" ] || { echo "qemu_i386: no kernel in $moddir" >&2; return 2; }
		;;
	hd|usb|cd)
		[ -f "$img" ] || { echo "qemu_i386: no image $img" >&2; return 2; }
		[ -n "$kargs" ] && echo "qemu_i386: MEDIA=$media: KARGS ignored, kernel arguments come from boot.cfg of the image" >&2
		;;
	*)
		echo "qemu_i386: unknown MEDIA=$media (multiboot, hd, cd, usb, ram)" >&2
		return 2 ;;
	esac

	case "$media" in
	multiboot)
		qemu_args="rootdevname=c0d0p0 console=tty00${kargs:+ $kargs}"
		qemu_cmd=("${base[@]}" -m 1024
			-drive file="$img",format=raw,if=ide,snapshot=on
			-kernel "$moddir/kernel" -initrd "$mods" -append "$qemu_args") ;;
	ram)
		# the root file system (~300 MB) is part of a boot module
		qemu_args="bootramdisk=1 console=tty00${kargs:+ $kargs}"
		qemu_cmd=("${base[@]}" -m 2048
			-kernel "$moddir/kernel" -initrd "$mods" -append "$qemu_args") ;;
	hd)
		qemu_args="(boot.cfg of $img)"
		qemu_cmd=("${base[@]}" -m 1024
			-drive file="$img",format=raw,if=ide,snapshot=on -boot c) ;;
	usb)
		# the root file system is loaded with the boot modules
		qemu_args="(boot.cfg of $img)"
		qemu_cmd=("${base[@]}" -m 2048
			-drive file="$img",format=raw,if=ide,snapshot=on -boot c) ;;
	cd)
		qemu_args="(boot.cfg of $img)"
		qemu_cmd=("${base[@]}" -m 1024
			-cdrom "$img" -boot d) ;;
	esac
}
