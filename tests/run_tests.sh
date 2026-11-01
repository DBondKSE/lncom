#!/bin/bash
cd "$(dirname "$0")/.." || exit 1

./build.sh || exit 1

flag=
if [ "$1" = "--optimize" ]; then
  flag=--optimize
elif [ -n "$1" ]; then
  echo "usage: $0 [--optimize]" >&2
  exit 1
fi

LLVM_CONFIG=${LLVM_CONFIG:-$(command -v llvm-config || echo /opt/homebrew/opt/llvm/bin/llvm-config)}
CLANG=${CLANG:-$("$LLVM_CONFIG" --bindir)/clang}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
failed=()

report_fail() {
  echo "FAIL $1"
  [ -n "$2" ] && echo "     $2"
  fail=$((fail + 1))
  failed+=("$1")
}

show_diff() {
  diff <(echo "$1") "$2" | sed 's/^/       /'
}

check_ast() {
  local src=$1 name=$2 opt=$3
  local got
  got=$(./compiler --ast $opt "$src" 2>&1)
  local status=$?
  if [ $status -ne 0 ]; then
    report_fail "$src" "--ast $opt exited with $status: $got"
    return 1
  elif [ "$got" != "$(cat "$name.ast")" ]; then
    report_fail "$src" "--ast $opt output differs from $name.ast:"
    show_diff "$got" "$name.ast"
    return 1
  fi
}

check_run() {
  local src=$1 name=$2 opt=$3 input=${4:-$2.in}
  local ll=$tmp/out.ll prog=$tmp/prog
  rm -f "$ll" "$prog"
  local got
  if ! got=$(./compiler $opt "$src" "$ll" 2>&1); then
    report_fail "$src" "did not compile with '$opt': $got"
    return 1
  fi
  if ! got=$("$CLANG" -Wno-override-module -pthread "$ll" -o "$prog" 2>&1); then
    report_fail "$src" "clang failed: $got"
    return 1
  fi
  [ -f "$input" ] || input=/dev/null
  got=$("$prog" <"$input" 2>"$tmp/stderr")
  local status=$?
  if [ "$got" != "$(cat "$name.expected")" ]; then
    report_fail "$src" "output with '$opt' differs from $name.expected:"
    show_diff "$got" "$name.expected"
    return 1
  fi
  if [ -f "$name.stderr" ]; then
    if [ $status -eq 0 ]; then
      report_fail "$src" "exited with 0, but $name.stderr expects a runtime error"
      return 1
    elif [ "$(cat "$tmp/stderr")" != "$(cat "$name.stderr")" ]; then
      report_fail "$src" "stderr differs from $name.stderr:"
      show_diff "$(cat "$tmp/stderr")" "$name.stderr"
      return 1
    fi
  elif [ $status -ne 0 ]; then
    report_fail "$src" "exited with $status: $(cat "$tmp/stderr")"
    return 1
  fi
}

for src in tests/ok/*.txt; do
  name=${src%.txt}
  if [ ! -f "$name.ast" ] && [ ! -f "$name.expected" ]; then
    report_fail "$src" "missing $name.ast or $name.expected"
    continue
  fi
  if [ -f "$name.ast" ] && [ -z "$flag" ]; then
    check_ast "$src" "$name" || continue
  fi
  if [ -f "$name.expected" ]; then
    check_run "$src" "$name" "$flag" || continue
  fi
  echo "ok   $src"
  pass=$((pass + 1))
done

for src in tests/opt/*.txt; do
  [ -e "$src" ] || continue
  name=${src%.txt}
  if [ ! -f "$name.ast" ] || [ ! -f "$name.expected" ]; then
    report_fail "$src" "missing $name.ast or $name.expected"
    continue
  fi
  check_ast "$src" "$name" --optimize || continue
  check_run "$src" "$name" "" || continue
  check_run "$src" "$name" --optimize || continue
  echo "ok   $src"
  pass=$((pass + 1))
done

for session in programs/*.expected; do
  [ -e "$session" ] || continue
  name=${session%.expected}
  src=${name%.*}.txt
  check_run "$src" "$name" "$flag" "$name.in" || continue
  echo "ok   $session"
  pass=$((pass + 1))
done

for src in tests/err/*.txt; do
  name=${src%.txt}
  if [ ! -f "$name.expected" ]; then
    report_fail "$src" "missing $name.expected"
    continue
  fi
  ll=$tmp/err.ll
  rm -f "$ll"
  out=$(./compiler $flag "$src" "$ll" 2>"$tmp/stderr")
  status=$?
  got=$(cat "$tmp/stderr")
  want=$(cat "$name.expected")
  if [ $status -eq 0 ]; then
    report_fail "$src" "compiled, but should have failed"
  elif [ -n "$out" ]; then
    report_fail "$src" "printed output despite the error"
  elif [ -e "$ll" ]; then
    report_fail "$src" "wrote an output file despite the error"
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
