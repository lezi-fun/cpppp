#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/c+++_test"
TMP="${TMPDIR:-/tmp}/c+++_regression_$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT"

echo "[test] build c+++"
g++ -std=c++17 -o "$BIN" main.cpp builder.cpp menusystem.cpp agent.cpp cmake.cpp

cat > "$TMP/main.cpp" <<'CPP'
#include <iostream>
int main(){ std::cout << "ok" << std::endl; return 0; }
CPP

cat > "$TMP/a.cpp" <<'CPP'
int a(){ return 1; }
CPP
cat > "$TMP/b.cpp" <<'CPP'
int b(){ return 2; }
CPP
cat > "$TMP/multi.cpp" <<'CPP'
int a(); int b(); int main(){ return a()+b()==3 ? 0 : 1; }
CPP

assert_contains() {
  local haystack="$1"
  local needle="$2"
  if ! grep -Fq -- "$needle" <<<"$haystack"; then
    echo "Expected output to contain: $needle" >&2
    echo "Actual output:" >&2
    echo "$haystack" >&2
    exit 1
  fi
}

echo "[test] dry-run does not create binary and prints commands"
out="$($BIN "$TMP/main.cpp" -o "$TMP/app" --dry-run 2>&1)"
assert_contains "$out" "DRY-RUN"
assert_contains "$out" "$TMP/main.cpp"
if [[ -e "$TMP/app" ]]; then
  echo "dry-run unexpectedly created binary" >&2
  exit 1
fi

echo "[test] -j parallel build option works"
out="$($BIN "$TMP/multi.cpp" "$TMP/a.cpp" "$TMP/b.cpp" -o "$TMP/multi" -j 2 2>&1)"
assert_contains "$out" "编译成功"
"$TMP/multi"

echo "[test] doctor text mode works"
out="$($BIN doctor 2>&1)"
assert_contains "$out" "Doctor"
assert_contains "$out" "compiler"

echo "[test] agent doctor json works"
out="$($BIN --agent doctor 2>&1)"
assert_contains "$out" '"compiler"'
assert_contains "$out" '"hello_world"'

echo "[test] clean removes cache"
$BIN "$TMP/main.cpp" -o "$TMP/app" >/dev/null 2>&1
[[ -d .c+++_cache ]]
out="$($BIN clean 2>&1)"
assert_contains "$out" "清理完成"
[[ ! -d .c+++_cache ]]

echo "[test] agent clean json works"
$BIN "$TMP/main.cpp" -o "$TMP/app" >/dev/null 2>&1
out="$($BIN --agent clean 2>&1)"
assert_contains "$out" '"cleaned"'
[[ ! -d .c+++_cache ]]

echo "All regression tests passed."
