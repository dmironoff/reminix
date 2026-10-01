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
#   MODDIR    directory with kernel and modNN_* (destdir/boot/minix/.temp)
#   IMAGE     disk image from x86_hdimage.sh
#   CPUS      number of virtual CPUs (-smp)
#   TESTLIST  "" for the whole suite, or comma separated names ("43,71,sh1")
#   TIMEOUT   seconds before the run is declared hung
#   LOG       where to write the full serial console log
#   KNOWN     file with known failures, one test name per line, # comments
#
# exit status: 0 no new failures, 1 new failures, 2 setup error,
#              3 hang / crash / incomplete run

set -u

if [ $# -ne 7 ]; then
	sed -n '/^# usage:/,/^# exit status/p' "$0" >&2
	exit 2
fi

MODDIR=$1 IMG=$2 CPUS=$3 LIST=$4 TMO=$5 LOG=$6 KNOWN=$7

[ -f "$MODDIR/kernel" ] || { echo "run-tests: no kernel in $MODDIR" >&2; exit 2; }
[ -f "$IMG" ] || { echo "run-tests: no image $IMG" >&2; exit 2; }

mods=$(ls "$MODDIR"/mod[0-9][0-9]_* 2>/dev/null | sort | paste -sd, -)
[ -n "$mods" ] || { echo "run-tests: no boot modules in $MODDIR" >&2; exit 2; }

append="rootdevname=c0d0p0 console=tty00 testrun=1"
[ -n "$LIST" ] && append="$append testlist=$LIST"

accel=tcg
[ -w /dev/kvm ] && accel=kvm

mkdir -p "$(dirname "$LOG")"
echo ">>> $(qemu-system-i386 --version | head -1)"
echo ">>> ${CPUS} CPU, accel=${accel}, timeout ${TMO}s, tests: ${LIST:-all}"
echo ">>> log: $LOG"

start=$(date +%s)
timeout --foreground -k 10 "$TMO" \
	qemu-system-i386 -machine pc,accel="$accel" -m 1024 -smp "$CPUS" \
		-display none -monitor none -serial stdio -no-reboot \
		-drive file="$IMG",format=raw,if=ide,snapshot=on \
		-kernel "$MODDIR/kernel" -initrd "$mods" -append "$append" \
		</dev/null | tee "$LOG"
qemu_status=${PIPESTATUS[0]}
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

echo
echo "================ ReMinix test summary ================"
echo "CPUs: $CPUS   accel: $accel   time: ${elapsed}s   tests: ${LIST:-all}"
echo "plan: ${plan:-?}   passed: $passed   failed: $failed"
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

verdict=0
if [ "$complete" = no ]; then
	if [ "$qemu_status" = 124 ] || [ "$qemu_status" = 137 ]; then
		echo "RESULT: HANG (no end marker within ${TMO}s)"
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
echo "======================================================"
exit $verdict
