#!/usr/bin/env bash
#
# ReMinix: boot the system in QEMU (i386) with its console on the serial line
# attached to this terminal -- an interactive session with the debugging
# features of the test runner (run-tests.sh). Called by docker/build.mk
# (target run-i386) inside the build container; see docs/testing.md.
#
# Kernel and boot modules are loaded directly by QEMU (multiboot), the root
# file system comes from the disk image opened with snapshot=on: nothing the
# session does is written back to the image.
#
# usage: run-console.sh MODDIR IMAGE CPUS LOG
#   MODDIR  directory with kernel and modNN_* (destdir/boot/minix/.temp)
#   IMAGE   disk image from x86_hdimage.sh
#   CPUS    number of virtual CPUs (-smp)
#   LOG     where to write the full serial console log
#           (env KARGS: extra kernel arguments; CPUS>1 adds no_apic=0)
#           (env SNAPSHOT_AT: seconds; one snapshot of the running system)
#           (env ACCEL: tcg or kvm instead of the automatic choice)
#           (env PANIC_RE: lines of the serial console or of the VGA screen
#            (polled every 10 s; kernel panics show up only there) that
#            trigger an automatic snapshot; extended regexp, default
#            "panic"; "" disables)
#
# keys in the terminal (the console is raw: Ctrl-C goes to MINIX):
#   Ctrl-A x   quit QEMU at once, no dump;   Ctrl-A h   other QEMU keys
# from another terminal (the container is labelled like test-i386):
#   make -C docker -f build.mk test-snapshot   snapshot, the session goes on
#   make -C docker -f build.mk test-stop       post-mortem, then stop QEMU
#
# Snapshots go to <log>.snapshot-<N>s/, the post-mortem to <log>.postmortem/,
# a short summary to <log>.result.

set -u

if [ $# -ne 4 ]; then
	sed -n '/^# usage:/,/^# Snapshots/p' "$0" >&2
	exit 2
fi

MODDIR=$1 IMG=$2 CPUS=$3 LOG=$4

[ -f "$MODDIR/kernel" ] || { echo "run-console: no kernel in $MODDIR" >&2; exit 2; }
[ -f "$IMG" ] || { echo "run-console: no image $IMG" >&2; exit 2; }

mods=$(ls "$MODDIR"/mod[0-9][0-9]_* 2>/dev/null | sort | paste -sd, -)
[ -n "$mods" ] || { echo "run-console: no boot modules in $MODDIR" >&2; exit 2; }

append="rootdevname=c0d0p0 console=tty00"
case " ${KARGS:-} " in
*" no_apic="*) ;;
*) [ "$CPUS" -gt 1 ] && append="$append no_apic=0" ;;
esac
[ -n "${KARGS:-}" ] && append="$append $KARGS"

accel=tcg
[ -w /dev/kvm ] && accel=kvm
[ -n "${ACCEL:-}" ] && accel=$ACCEL

PANIC_RE=${PANIC_RE-panic}

mkdir -p "$(dirname "$LOG")"
monsock=$(mktemp -u /tmp/reminix-mon.XXXXXX)
pmdir="${LOG%.log}.postmortem"
here=$(cd "$(dirname "$0")" && pwd)

# QEMU puts the terminal into raw mode: end our own lines with CR LF.
say() { printf '\r\n>>> %s\r\n' "$*"; }

say "$(qemu-system-i386 --version | head -1)"
say "${CPUS} CPU, accel=${accel}, kernel args: $append"
say "log: $LOG"
say "Ctrl-A x: quit at once; make -C docker -f build.mk test-snapshot | test-stop from another terminal"

# Signals: this script is PID 1 of the container (see run-tests.sh). Ctrl-C
# in the terminal does not reach us: QEMU keeps the tty raw (signal=off) and
# passes it to MINIX. SIGTERM comes from "make test-stop".
interrupted=0
snap_req=0
helper_pid=
in_postmortem=no
on_stop() {
	interrupted=$((interrupted + 1))
	if [ "$in_postmortem" = yes ] && [ -n "$helper_pid" ]; then
		kill "$helper_pid" 2>/dev/null
	fi
}
trap on_stop INT TERM
trap 'snap_req=$((snap_req + 1))' USR1

run_helper() {
	local st
	"$@" &
	helper_pid=$!
	while :; do
		wait "$helper_pid"
		st=$?
		kill -0 "$helper_pid" 2>/dev/null || break
	done
	helper_pid=
	return $st
}

snapshots=0
take_snapshot() {
	local t=$(( $(date +%s) - start ))
	local dir="${LOG%.log}.snapshot-${t}s"
	[ -e "$dir" ] && dir="$dir.$$.$snapshots"
	snapshots=$((snapshots + 1))
	say "snapshot at ${t}s ($1): $dir"
	run_helper python3 "$here/qemu-postmortem.py" "$monsock" "$MODDIR/kernel" \
		"$dir" "$CPUS" cont >/dev/null 2>&1 || say "snapshot failed"
	say "snapshot done"
}

start=$(date +%s)
: > "$LOG"
# A background job of a non-interactive shell gets /dev/null as stdin unless
# redirected explicitly: hand the terminal to QEMU through fd 3.
exec 3<&0
qemu-system-i386 -machine pc,accel="$accel" -m 1024 -smp "$CPUS" \
	-display none -monitor unix:"$monsock",server,nowait \
	-chardev stdio,id=con,mux=on,signal=off,logfile="$LOG",logappend=off \
	-serial chardev:con -no-reboot \
	-drive file="$IMG",format=raw,if=ide,snapshot=on \
	-kernel "$MODDIR/kernel" -initrd "$mods" -append "$append" \
	<&3 &
qemu_pid=$!
exec 3<&-

stop_reason=
panics=0
vga_panics=0
vga_checked=$start
snap_done=no
while kill -0 $qemu_pid 2>/dev/null; do
	if [ "$interrupted" -eq 0 ] && [ "$snap_req" -eq 0 ]; then
		sleep 2 &
		sleep_pid=$!
		wait $sleep_pid
		kill $sleep_pid 2>/dev/null
	fi
	now=$(date +%s)
	if [ "$interrupted" -gt 0 ]; then
		stop_reason=interrupted
		break
	fi
	if [ "$snap_req" -gt 0 ]; then
		snap_req=0
		take_snapshot "SIGUSR1"
	fi
	if [ -n "${SNAPSHOT_AT:-}" ] && [ "$snap_done" = no ] &&
	   [ $((now - start)) -ge "$SNAPSHOT_AT" ]; then
		snap_done=yes
		take_snapshot "SNAPSHOT_AT"
	fi
	# a new console line matching PANIC_RE: snapshot while the state is
	# fresh (a panicking server is restarted by RS, the machine goes on)
	if [ -n "$PANIC_RE" ]; then
		n=$(tr -d '\r' < "$LOG" | grep -cE -- "$PANIC_RE")
		if [ "$n" -gt "$panics" ]; then
			panics=$n
			take_snapshot "console: $(tr -d '\r' < "$LOG" | grep -E -- "$PANIC_RE" | tail -1 | cut -c1-60)"
		fi
	fi
	# A kernel panic goes to the VGA screen only (the serial console is
	# driven by TTY, which is dead by then): look at the screen as well.
	if [ -n "$PANIC_RE" ] && [ $((now - vga_checked)) -ge 10 ]; then
		vga_checked=$now
		vga=$(python3 "$here/qemu-vga.py" "$monsock" /tmp/reminix-vga.$$ 2>/dev/null)
		n=$(printf '%s\n' "$vga" | grep -cE -- "$PANIC_RE")
		if [ "$n" -gt "$vga_panics" ]; then
			vga_panics=$n
			take_snapshot "VGA: $(printf '%s\n' "$vga" | grep -E -- "$PANIC_RE" | tail -1 | cut -c1-60)"
		fi
	fi
done

if [ "$stop_reason" = interrupted ]; then
	stopped_at=$(( $(date +%s) - start ))
	say "stop requested at ${stopped_at}s: saving post-mortem (signal again to skip)"
	in_postmortem=yes
	if [ "$interrupted" -gt 1 ]; then
		say "post-mortem skipped"
	else
		run_helper python3 "$here/qemu-postmortem.py" "$monsock" "$MODDIR/kernel" \
			"$pmdir" "$CPUS" >/dev/null 2>&1 || say "post-mortem capture failed or skipped"
	fi
	in_postmortem=no
	kill $qemu_pid 2>/dev/null
	sleep 2
	kill -9 $qemu_pid 2>/dev/null
fi
while :; do
	wait $qemu_pid
	qemu_status=$?
	kill -0 $qemu_pid 2>/dev/null || break
done
rm -f "$monsock"
elapsed=$(( $(date +%s) - start ))

{
	echo "================ ReMinix console session ================"
	echo "CPUs: $CPUS   accel: $accel   time: ${elapsed}s"
	echo "kernel args: $append"
	if [ "$stop_reason" = interrupted ]; then
		echo "END: stopped at ${stopped_at}s (make test-stop)"
		[ -f "$pmdir/monitor.txt" ] && echo "post-mortem: $pmdir"
	else
		echo "END: QEMU exited with status $qemu_status (poweroff, Ctrl-A x or crash)"
	fi
	pl=$(tr -d '\r' < "$LOG" | grep -E -- "${PANIC_RE:-^\$^}" | head -20)
	[ -n "$pl" ] && { echo "console lines matching \"$PANIC_RE\":"; echo "$pl" | sed 's/^/  | /'; }
	for d in "${LOG%.log}".snapshot-*; do
		[ -d "$d" ] && echo "snapshot: $d"
	done
	echo "log: $LOG"
	echo "=========================================================="
} | tee "${LOG%.log}.result" | sed 's/$/\r/'
exit 0
