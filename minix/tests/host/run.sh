#!/bin/sh
# Run host test programs (TAP), print their output and a summary.
#   run.sh <objdir> <test binary>...
# A program counts as passed when it exits 0 and reports as many "ok"
# lines (plus "not ok ... # TODO" lines: known, not yet fixed problems) as
# its plan says.  "ok ... # TODO" -- a known problem that no longer shows
# -- is listed so that the TODO mark can be removed.  Exit status: 0 PASS,
# 1 FAIL.

objdir=$1
shift

total=0
failed=0
failed_list=
todo=0
todo_fixed=
start=$(date +%s)

for bin in "$@"; do
	name=${bin#"$objdir"/}
	echo "### $name"
	out=$("$bin" 2>&1)
	status=$?
	printf '%s\n' "$out"
	plan=$(printf '%s\n' "$out" | sed -n 's/^1\.\.\([0-9][0-9]*\)$/\1/p' | head -1)
	ok=$(printf '%s\n' "$out" | grep -c '^ok ')
	nok_todo=$(printf '%s\n' "$out" | grep -c '^not ok .* # TODO$')
	ok=$((ok + nok_todo))
	todo=$((todo + nok_todo))
	for t in $(printf '%s\n' "$out" | sed -n 's/^ok [0-9]* - \(.*\) # TODO$/\1/p'); do
		todo_fixed="$todo_fixed $name:$t"
	done
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
[ "$todo" -gt 0 ] && echo "known problems (not ok # TODO): $todo"
[ -n "$todo_fixed" ] && echo "TODO tests that pass now, remove TODO:$todo_fixed"
echo "RESULT: $result"
echo "==========================================================="
[ "$result" = PASS ]
