#!/bin/sh
# Run host test programs (TAP), print their output and a summary.
#   run.sh <objdir> <test binary>...
# A program counts as passed when it exits 0 and reports as many "ok"
# lines as its plan says.  Exit status: 0 PASS, 1 FAIL.

objdir=$1
shift

total=0
failed=0
failed_list=
start=$(date +%s)

for bin in "$@"; do
	name=${bin#"$objdir"/}
	echo "### $name"
	out=$("$bin" 2>&1)
	status=$?
	printf '%s\n' "$out"
	plan=$(printf '%s\n' "$out" | sed -n 's/^1\.\.\([0-9][0-9]*\)$/\1/p' | head -1)
	ok=$(printf '%s\n' "$out" | grep -c '^ok ')
	total=$((total + 1))
	if [ "$status" -ne 0 ] || [ -z "$plan" ] || [ "$ok" -ne "$plan" ]; then
		failed=$((failed + 1))
		failed_list="$failed_list $name"
		echo "### $name: FAIL (exit $status, ${ok}/${plan:-?} ok)"
	fi
done

if [ "$failed" -eq 0 ] && [ "$total" -gt 0 ]; then
	result=PASS
else
	result=FAIL
fi
echo
echo "================ ReMinix host test summary ================"
echo "programs: $total   failed: $failed   time: $(( $(date +%s) - start ))s"
[ -n "$failed_list" ] && echo "failed:$failed_list"
echo "RESULT: $result"
echo "==========================================================="
[ "$result" = PASS ]
