#!/bin/bash
cd "$(dirname "$0")/.." || exit 1

./build.sh || exit 1

pass=0
fail=0
failed=()

report_fail() {
  echo "FAIL $1"
  [ -n "$2" ] && echo "     $2"
  fail=$((fail + 1))
  failed+=("$1")
}

for src in tests/ok/*.txt; do
  name=${src%.txt}
  if [ ! -f "$name.ast" ]; then
    report_fail "$src" "missing $name.ast"
    continue
  fi
  got=$(./compiler --ast "$src" 2>&1)
  status=$?
  if [ $status -ne 0 ]; then
    report_fail "$src" "exited with $status: $got"
  elif [ "$got" != "$(cat "$name.ast")" ]; then
    report_fail "$src" "--ast output differs from $name.ast:"
    diff <(echo "$got") "$name.ast" | sed 's/^/       /'
  else
    echo "ok   $src"
    pass=$((pass + 1))
  fi
done

for src in tests/err/*.txt; do
  name=${src%.txt}
  if [ ! -f "$name.expected" ]; then
    report_fail "$src" "missing $name.expected"
    continue
  fi
  out=$(./compiler --ast "$src" 2>/dev/null)
  status=$?
  got=$(./compiler --ast "$src" 2>&1 >/dev/null)
  want=$(cat "$name.expected")
  if [ $status -eq 0 ]; then
    report_fail "$src" "parsed, but should have failed"
  elif [ -n "$out" ]; then
    report_fail "$src" "printed output despite the error"
  elif [ "$got" != "$want" ]; then
    report_fail "$src" "expected: $want"
    echo "     got:      $got"
  else
    echo "ok   $src"
    pass=$((pass + 1))
  fi
done

echo
echo "passed $pass, failed $fail"
for f in "${failed[@]}"; do
  echo "  $f"
done
[ $fail -eq 0 ]
