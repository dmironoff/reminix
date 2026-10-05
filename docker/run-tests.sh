#!/usr/bin/env bash
#
# ReMinix: run minix/tests unattended in QEMU (i386) and check the result.
# Called by docker/build.mk (target test-i386) inside the build container;
# see docs/testing.md.
#
# The kernel and boot modules are loaded directly by QEMU (multiboot via
# -kernel/-initrd), the root file system comes from the disk image, opened
# with snapshot=on so the image itself is never modified. The kernel
# command line carries testrun=1, which makes /etc/rc.d/minixtests run the
# suite in TAP mode on the serial console and power the machine off.
#
# usage: run-tests.sh MODDIR IMAGE CPUS TESTLIST TIMEOUT LOG KNOWN
#   MODDIR    i386: directory with kernel and modNN_* (destdir/boot/minix/.temp)
#             earm: directory with the kernel ELF (obj/.../minix/kernel), only
#             for the symbols of snapshots and post-mortems
#   IMAGE     disk image from x86_hdimage.sh / SD image from arm_sdimage.sh
#   CPUS      number of virtual CPUs (-smp)
#   TESTLIST  "" for the whole suite, or comma separated names ("43,71,sh1")
#   TIMEOUT   seconds before the whole run is declared hung
#             (env HANG_IDLE: seconds of console silence, default 600)
#             (env KARGS: extra kernel arguments; CPUS>1 adds no_apic=0)
#             (env TEST_JOBS: run TESTLIST in groups of N concurrent tests,
#              rc.d/minixtests testjobs=N; default 1)
#             (env SNAPSHOT_AT: seconds; snapshot of the running system)
#             (env ACCEL: tcg or kvm instead of the automatic choice)
#             (env TARGET: i386 (default) or earm; earm needs env MACHINE,
#              the QEMU machine of the board, and boots the SD image)
#   LOG       where to write the full serial console log
#   KNOWN     file with known failures, one test name per line, # comments
#
# signals (sent to the container, whose PID 1 this script is):
#   SIGINT, SIGTERM  (Ctrl-C, make test-stop) post-mortem of the machine as it
#                    is, then stop QEMU; a second one skips the post-mortem
#   SIGUSR1          (make test-snapshot) snapshot of the running machine,
#                    the run continues
#
# exit status: 0 no new failures, 1 new failures, 2 setup error,
#              3 hang / crash / incomplete / interrupted run

set -u
export LC_NUMERIC=C	# "." in $EPOCHREALTIME and awk numbers

if [ $# -ne 7 ]; then
	sed -n '/^# usage:/,/^# exit status/p' "$0" >&2
	exit 2
fi

MODDIR=$1 IMG=$2 CPUS=$3 LIST=$4 TMO=$5 LOG=$6 KNOWN=$7

TARGET=${TARGET:-i386}
[ -f "$MODDIR/kernel" ] || { echo "run-tests: no kernel in $MODDIR" >&2; exit 2; }
[ -f "$IMG" ] || { echo "run-tests: no image $IMG" >&2; exit 2; }

if [ "$TARGET" = i386 ]; then
	mods=$(ls "$MODDIR"/mod[0-9][0-9]_* 2>/dev/null | sort | paste -sd, -)
	[ -n "$mods" ] || { echo "run-tests: no boot modules in $MODDIR" >&2; exit 2; }
fi

append="rootdevname=c0d0p0 console=tty00 testrun=1"
[ -n "$LIST" ] && append="$append testlist=$LIST"
[ "${TEST_JOBS:-1}" -gt 1 ] && append="$append testjobs=$TEST_JOBS"
# The kernel defaults to no_apic=1, which makes an SMP kernel fall back to
# a single CPU on the 8259 PIC: the other CPUs would never be started.
# KARGS can override this (KARGS=no_apic=1 tests the single CPU fallback).
case " ${KARGS:-} " in
*" no_apic="*) ;;
*) [ "$CPUS" -gt 1 ] && append="$append no_apic=0" ;;
esac
# extra kernel arguments (env KARGS, e.g. "no_smp=1" or "no_apic=1")
[ -n "${KARGS:-}" ] && append="$append $KARGS"

accel=tcg
[ "$TARGET" = i386 ] && [ -w /dev/kvm ] && accel=kvm
# env ACCEL=tcg|kvm overrides (TCG: to tell guest bugs from KVM effects)
[ -n "${ACCEL:-}" ] && accel=$ACCEL

# QEMU command line of the target, without the serial console
case "$TARGET" in
i386)
	qemu_cmd=(qemu-system-i386 -machine pc,accel="$accel" -m 1024 -smp "$CPUS"
		-drive file="$IMG",format=raw,if=ide,snapshot=on
		-kernel "$MODDIR/kernel" -initrd "$mods" -append "$append")
	;;
earm)
	[ -n "${MACHINE:-}" ] || { echo "run-tests: TARGET=earm needs MACHINE" >&2; exit 2; }
	# On earm U-Boot loads the kernel from the SD card and hands it the
	# kernel arguments; delivering testrun/testlist/KARGS through the U-Boot
	# environment comes with the Orange Pi boot path (docs/modernization.md
	# B2). Without them the tests would never start.
	echo "run-tests: TARGET=earm: kernel arguments ($append) cannot be passed" \
	    "to MINIX yet -- they go through U-Boot, which comes with step B2" >&2
	exit 2
	qemu_cmd=(qemu-system-arm -machine "$MACHINE",accel="$accel" -m 1024 -smp "$CPUS"
		-drive file="$IMG",format=raw,if=sd,snapshot=on)
	;;
*)
	echo "run-tests: unknown TARGET=$TARGET" >&2; exit 2 ;;
esac

mkdir -p "$(dirname "$LOG")"
echo ">>> $("${qemu_cmd[0]}" --version | head -1)"
echo ">>> ${TARGET}${MACHINE:+ ($MACHINE)}, ${CPUS} CPU, accel=${accel}, timeout ${TMO}s, tests: ${LIST:-all}"
echo ">>> kernel args: $append"
echo ">>> log: $LOG"

# Hang detection: the run is declared hung if the console stays silent for
# HANG_IDLE seconds (in TAP mode a test prints only when it finishes, so this
# must exceed the longest single test) or the whole run exceeds TIMEOUT.
# On a hang the state of all CPUs and the key kernel memory is saved through
# the QEMU monitor (qemu-postmortem.py) before QEMU is killed.
HANG_IDLE=${HANG_IDLE:-600}
monsock=$(mktemp -u /tmp/reminix-mon.XXXXXX)
pmdir="${LOG%.log}.postmortem"
here=$(cd "$(dirname "$0")" && pwd)

# Signals. This script is PID 1 of the container, and the kernel applies no
# default action to PID 1: without handlers SIGINT/SIGTERM were simply lost,
# and a Ctrl-C left QEMU running in the background. Handlers only set flags;
# the main loop waits with "wait" (which a trapped signal interrupts at once)
# and acts on them. During the final post-mortem any SIGINT/SIGTERM kills the
# capture (the "skip it" escape).
interrupted=0		# SIGINT/SIGTERM received
snap_req=0		# SIGUSR1 received
helper_pid=		# capture running in the background
in_postmortem=no
on_stop() {
	interrupted=$((interrupted + 1))
	if [ "$in_postmortem" = yes ] && [ -n "$helper_pid" ]; then
		kill "$helper_pid" 2>/dev/null
	fi
}
trap on_stop INT TERM
trap 'snap_req=$((snap_req + 1))' USR1

# Run a command in the background and wait for it, so that signal handlers
# run while it works. Returns the command's exit status.
run_helper() {
	local st
	"$@" &
	helper_pid=$!
	while :; do
		wait "$helper_pid"
		st=$?
		kill -0 "$helper_pid" 2>/dev/null || break	# finished, not a signal
	done
	helper_pid=
	return $st
}

# Snapshot of the running machine (same contents as a post-mortem, the machine
# continues): <log>.snapshot-<seconds since start>s/
take_snapshot() {
	local t=$(( $(date +%s) - start ))
	local dir="${LOG%.log}.snapshot-${t}s"
	echo
	echo ">>> snapshot at ${t}s ($1): $dir"
	run_helper python3 "$here/qemu-postmortem.py" "$monsock" "$MODDIR/kernel" \
		"$dir" "$CPUS" cont || echo ">>> snapshot failed"
}

# Test timing. The console is echoed line by line and the lines that mark the
# suite (REMINIX-TESTS-BEGIN/END, TAP results) are written to <log>.times with
# the host time of their arrival: the duration of the suite does not depend on
# the guest clock and on boot/shutdown, and since a TAP result is printed when
# its test ends, the gap between two results is the duration of a test.
tsfile="${LOG%.log}.times"
stamp_console() {
	local l
	while IFS= read -r l || [ -n "$l" ]; do
		printf '%s\n' "$l"
		case $l in
		"# "*) ;;
		*REMINIX-TESTS-*|*"ok test "*)
			printf '%s %s\n' "$EPOCHREALTIME" "${l%$'\r'}" >> "$tsfile" ;;
		esac
	done
}

start=$(date +%s)
: > "$LOG"
: > "$tsfile"
"${qemu_cmd[@]}" \
	-display none -monitor unix:"$monsock",server,nowait \
	-serial file:"$LOG" -no-reboot \
	</dev/null &
qemu_pid=$!
tail -n +1 -s 0.2 -f --pid=$qemu_pid "$LOG" | stamp_console &
tail_pid=$!

stop_reason=		# hung | interrupted: QEMU is stopped by us
last_size=-1
last_change=$start
snap_done=no
while kill -0 $qemu_pid 2>/dev/null; do
	# do not sleep if a signal came in while a snapshot was being taken
	if [ "$interrupted" -eq 0 ] && [ "$snap_req" -eq 0 ]; then
		sleep 5 &
		sleep_pid=$!
		wait $sleep_pid		# returns at once on a trapped signal
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
	# env SNAPSHOT_AT=N: one snapshot of the running system after N seconds
	if [ -n "${SNAPSHOT_AT:-}" ] && [ "$snap_done" = no ] &&
	   [ $((now - start)) -ge "$SNAPSHOT_AT" ]; then
		snap_done=yes
		take_snapshot "SNAPSHOT_AT"
	fi
	size=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
	if [ "$size" != "$last_size" ]; then
		last_size=$size
		last_change=$now
	fi
	if [ $((now - last_change)) -ge "$HANG_IDLE" ] || [ $((now - start)) -ge "$TMO" ]; then
		stop_reason=hung
		break
	fi
done

if [ -n "$stop_reason" ]; then
	echo
	if [ "$stop_reason" = hung ]; then
		echo ">>> no console output for $((now - last_change))s (run time $((now - start))s): saving post-mortem"
		qemu_status=124
	else
		stopped_at=$((now - start))
		echo ">>> interrupted at ${stopped_at}s: saving post-mortem (signal again to skip)"
		qemu_status=130
	fi
	in_postmortem=yes
	if [ "$interrupted" -gt 1 ]; then
		echo ">>> post-mortem skipped"
	else
		run_helper python3 "$here/qemu-postmortem.py" "$monsock" "$MODDIR/kernel" \
			"$pmdir" "$CPUS" || echo ">>> post-mortem capture failed or skipped"
	fi
	in_postmortem=no
	kill $qemu_pid 2>/dev/null
	sleep 2
	kill -9 $qemu_pid 2>/dev/null
	wait $qemu_pid 2>/dev/null
else
	# QEMU exited by itself; a signal may still interrupt this wait
	while :; do
		wait $qemu_pid
		qemu_status=$?
		kill -0 $qemu_pid 2>/dev/null || break
	done
fi
# tail ends by itself once QEMU is gone (--pid); give it time to pass the
# last lines on to stamp_console
for i in 1 2 3 4 5 6 7 8 9 10; do
	kill -0 $tail_pid 2>/dev/null || break
	sleep 0.5
done
kill $tail_pid 2>/dev/null
wait $tail_pid 2>/dev/null
rm -f "$monsock"
elapsed=$(( $(date +%s) - start ))

# ---- analysis ---------------------------------------------------------

clean=$(mktemp)
trap 'rm -f "$clean"' EXIT
tr -d '\r' < "$LOG" > "$clean"

# TAP results; console messages of servers may be glued to the same line,
# so match anywhere, but skip TAP diagnostics ("# ...").
results=$(grep -v '^# ' "$clean" | grep -oE '(not ok|ok) test [[:alnum:]_]+')
plan=$(grep -oE '^1\.\.[0-9]+' "$clean" | head -1 | cut -d. -f3)
passed=$(printf '%s\n' "$results" | grep -c '^ok test ' || true)
failed_list=$(printf '%s\n' "$results" | grep '^not ok test ' | awk '{print $4}' | sort -u)
failed=$(printf '%s' "$failed_list" | grep -c . || true)

known_list=$( [ -f "$KNOWN" ] && sed -e 's/#.*//' -e 's/[[:space:]]//g' "$KNOWN" | grep . | sort -u )
new_fail=$(comm -23 <(printf '%s\n' "$failed_list" | grep .) <(printf '%s\n' "$known_list" | grep .))
fixed=$(comm -13 <(printf '%s\n' "$failed_list" | grep .) <(printf '%s\n' "$known_list" | grep .))

# Duration of the suite: REMINIX-TESTS-BEGIN to REMINIX-TESTS-END (if the end
# marker was lost or the run did not finish: to the last TAP result). Each
# test: from the previous result (or BEGIN) to its own, in <log>.durations.
durfile="${LOG%.log}.durations"
rm -f "$durfile"
tests_time=$(awk -v out="$durfile" '
	/REMINIX-TESTS-BEGIN/ { begin = prev = $1; next }
	/REMINIX-TESTS-END/   { if (begin != "") end = $1; next }
	begin != "" && match($0, /(not ok|ok) test [[:alnum:]_]+/) {
		split(substr($0, RSTART, RLENGTH), w, " ")
		res = (w[1] == "not") ? "FAIL" : "ok"
		printf "%9.1f  %-4s  %s\n", $1 - prev, res, w[res == "FAIL" ? 4 : 3] > out
		prev = last = $1
	}
	END {
		if (begin == "") exit
		if (end != "") printf "%.1fs", end - begin
		else if (last != "") printf "%.1fs (to the last result, no end marker)", last - begin
		else printf "none (no test finished)"
	}' "$tsfile")

# The summary goes to the terminal and to <log>.result: when the docker client
# is gone (terminal closed, make interrupted) the verdict is still on disk.
summary() {
local verdict=0
echo
echo "================ ReMinix test summary ================"
echo "CPUs: $CPUS   accel: $accel   run time: ${elapsed}s   tests: ${LIST:-all}${TEST_JOBS:+   jobs: $TEST_JOBS}"
echo "plan: ${plan:-?}   passed: $passed   failed: $failed"
if [ -n "$tests_time" ]; then
	echo "tests time: $tests_time   (per test: $(basename "$durfile"))"
else
	echo "tests time: - (the suite did not start)"
fi
[ -n "$failed_list" ] && echo "failed: $(echo $failed_list)"
[ -n "$fixed" ] && echo "known failures that passed now: $(echo $fixed)"

complete=no
if grep -q '^REMINIX-TESTS-END' "$clean"; then
	complete=yes
elif [ -n "$plan" ] && [ $((passed + failed)) -ge "$plan" ] &&
     grep -q 'MINIX will now be shut down' "$clean"; then
	# all results are in and the system shut down cleanly; only the end
	# marker itself was lost in the console output queue
	echo "note: end marker missing, but all $plan results present and clean shutdown"
	complete=yes
fi

if [ "$complete" = no ]; then
	if [ "$qemu_status" = 124 ]; then
		echo "RESULT: HANG (console silent ${HANG_IDLE}s or run over ${TMO}s)"
		if [ -f "$pmdir/monitor.txt" ]; then
			echo "post-mortem: $pmdir"
		else
			echo "post-mortem: skipped or incomplete"
		fi
	elif [ "$qemu_status" = 130 ]; then
		echo "RESULT: INTERRUPTED (SIGINT/SIGTERM at ${stopped_at}s)"
		if [ -f "$pmdir/monitor.txt" ]; then
			echo "post-mortem: $pmdir"
		else
			echo "post-mortem: skipped or incomplete"
		fi
	else
		echo "RESULT: CRASH (QEMU exited with status $qemu_status before all results were in)"
	fi
	echo "last console lines:"
	tail -15 "$clean" | sed 's/^/  | /'
	verdict=3
elif [ -n "$plan" ] && [ $((passed + failed)) -lt "$plan" ]; then
	echo "RESULT: INCOMPLETE ($((passed + failed)) of $plan results)"
	verdict=3
elif [ -n "$new_fail" ]; then
	echo "RESULT: FAIL, new failures: $(echo $new_fail)"
	verdict=1
else
	echo "RESULT: PASS"
fi
for d in "${LOG%.log}".snapshot-*s; do
	[ -d "$d" ] && echo "snapshot: $d"
done
echo "======================================================"
return $verdict
}

summary | tee "${LOG%.log}.result"
exit ${PIPESTATUS[0]}
