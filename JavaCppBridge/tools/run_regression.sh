#!/usr/bin/env bash
# run_regression.sh - LangBridge end-to-end regression (cross-platform).
#
# Probes, in order: g++ / clang++ / cl (in PATH). On Windows with MSVC where
# cl needs vcvars64.bat and is not on PATH, use tools/run_regression_msvc.bat
# instead of this script.
#
# Checks:
#   1. examples/java/*.java  -> C++ ; with a C++ compiler, compile & run the
#      generated C++ and compare stdout to tests/expect/<stem>_j2c.txt
#   2. examples/cpp/*.cpp    -> Java ; with javac, compile & run the generated
#      Java and compare stdout to tests/expect/<stem>_c2j.txt
#   Without a compiler, only checks that generated files are non-empty.
#
# Exit code: 0 = all available checks passed.
set -u
cd "$(dirname "$0")/.." || exit 2
ROOT="$(pwd)"
BUILD="$ROOT/build"
GEN="$BUILD/gen"
EXPECT="$ROOT/tests/expect"

mkdir -p "$GEN"
PASS=0; FAIL=0
note() { printf '[regression] %s\n' "$*"; }
ok()   { PASS=$((PASS+1)); printf '  PASS: %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL: %s\n' "$1"; }

# ------------------------------------------------------------- find tools
GXX=""
for c in g++ clang++; do
  if command -v "$c" >/dev/null 2>&1; then GXX="$c"; break; fi
done
if [ -z "$GXX" ] && command -v cl >/dev/null 2>&1; then GXX="cl"; fi
command -v javac >/dev/null 2>&1 && HAVE_JAVAC=1 || HAVE_JAVAC=0

# ------------------------------------------------------------ build/use CLI
LBEXE="$BUILD/langbridge"
[ -f "$LBEXE.exe" ] && LBEXE="$LBEXE.exe"
if [ ! -f "$LBEXE" ]; then
  note "building langbridge CLI..."
  if [ "$GXX" = "g++" ] || [ "$GXX" = "clang++" ]; then
    "$GXX" -std=c++17 -O2 -I"$ROOT/include" -o "$BUILD/langbridge" \
       src/lexer.cpp src/parser.cpp src/analyze.cpp src/gen_common.cpp \
       src/gen_j2c.cpp src/gen_c2j.cpp src/main.cpp || { bad "CLI build"; exit 1; }
    LBEXE="$BUILD/langbridge"
  elif [ "$GXX" = "cl" ]; then
    cl /nologo /std:c++17 /EHsc /W3 /O2 /I"$ROOT\include" /Fe:"$BUILD\langbridge.exe" \
       src\lexer.cpp src\parser.cpp src\analyze.cpp src\gen_common.cpp \
       src\gen_j2c.cpp src\gen_c2j.cpp src\main.cpp || { bad "CLI build"; exit 1; }
  else
    bad "no C++ compiler found (g++/clang++/cl); build langbridge manually first"
    exit 1
  fi
  note "CLI built"
fi

# ------------------------------------------------------------ C++ execution
run_cpp_case() { # $1 stem  $2 generated cpp
  local stem="$1" genfile="$2" expect="$EXPECT/${stem}_j2c.txt"
  local exe="$GEN/${stem}_cpp" out="$GEN/${stem}_j2c.out"
  if [ -n "$GXX" ] && [ "$GXX" != "cl" ]; then
    if "$GXX" -std=c++17 -O2 -o "$exe" "$genfile" 2>"$GEN/${stem}_cc.log"; then
      "$exe" > "$out" 2>/dev/null
      if diff -q "$expect" "$out" >/dev/null 2>&1; then ok "$stem (C++ run matches)"; else bad "$stem: C++ output mismatch"; fi
    else bad "$stem: generated C++ did not compile"; fi
  elif [ "$GXX" = "cl" ]; then
    bad "$stem: MSVC cl compile requires run_regression_msvc.bat; generated file checked only"
    [ -s "$genfile" ] && ok "$stem (file generated, nonempty)" || bad "$stem: empty C++ output"
  else
    [ -s "$genfile" ] && ok "$stem (compiler absent; file generated)" || bad "$stem: empty C++ output"
  fi
}

# ------------------------------------------------------------ Java execution
run_java_case() { # $1 stem  $2 generated java (contains public class Main)
  local stem="$1" genfile="$2" expect="$EXPECT/${stem}_c2j.txt"
  local jdir="$GEN/${stem}_j2j" out="$GEN/${stem}_c2j.out"
  rm -rf "$jdir"; mkdir -p "$jdir"
  cp "$genfile" "$jdir/Main.java"
  if [ "$HAVE_JAVAC" -eq 1 ]; then
    if ( cd "$jdir" && javac -encoding UTF-8 Main.java >compile.log 2>&1 ); then
      if ( cd "$jdir" && java Main >"$out" 2>run.log ); then
        if diff -q "$expect" "$out" >/dev/null 2>&1; then ok "$stem (Java run matches)"; else bad "$stem: Java output mismatch"; fi
      else bad "$stem: java run failed"; fi
    else bad "$stem: javac failed"; fi
  else
    [ -s "$genfile" ] && ok "$stem (javac absent; file generated)" || bad "$stem: empty Java output"
  fi
}

# ------------------------------------------------------------------- run
note "translating and verifying examples..."
for src in "$ROOT"/examples/java/*.java; do
  [ -e "$src" ] || continue
  stem="$(basename "$src" .java)"
  "$LBEXE" --mode java2cpp -i "$src" -o "$GEN/${stem}.cpp" >/dev/null 2>&1
  run_cpp_case "$stem" "$GEN/${stem}.cpp"
done

for src in "$ROOT"/examples/cpp/*.cpp; do
  [ -e "$src" ] || continue
  stem="$(basename "$src" .cpp)"
  "$LBEXE" --mode cpp2java -i "$src" -o "$GEN/${stem}.java" >/dev/null 2>&1
  run_java_case "$stem" "$GEN/${stem}.java"
done

echo
echo "regression result: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]