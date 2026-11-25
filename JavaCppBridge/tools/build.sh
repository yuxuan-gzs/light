#!/usr/bin/env bash
# build.sh - cross-platform build for LangBridge (git-bash / MSYS / Linux / macOS).
#
# Probes, in order: cl (MSVC, Windows only) -> g++ -> clang++.
# On Windows with MSVC, cl usually needs vcvars64.bat for INCLUDE/LIB; if this
# script cannot find a usable cl, it prints a hint to use tools/build_local_msvc.bat.
#
# Usage:  bash tools/build.sh [--clean]
# Output: build/langbridge[.exe]
set -u
cd "$(dirname "$0")/.." || exit 2
ROOT="$(pwd)"
SRC="src/preprocess.cpp src/lexer.cpp src/parser.cpp src/analyze.cpp src/gen_common.cpp src/gen_j2c.cpp src/gen_c2j.cpp src/main.cpp"
OUT="build/langbridge"

mkdir -p build

if [ "${1:-}" = "--clean" ]; then
  rm -rf build
  mkdir -p build
fi

CXX=""
if command -v cl >/dev/null 2>&1 && cl /? 2>&1 | grep -qi "Microsoft"; then
  CXX="cl"
elif command -v g++ >/dev/null 2>&1; then
  CXX="g++"
elif command -v clang++ >/dev/null 2>&1; then
  CXX="clang++"
fi

case "$CXX" in
  cl)
    echo "[build] using MSVC cl"
    cl /nologo /std:c++17 /EHsc /W3 /O2 /I"$ROOT\\include" /Fe:"$ROOT\\$OUT.exe" \
       src\\preprocess.cpp src\\lexer.cpp src\\parser.cpp src\\analyze.cpp src\\gen_common.cpp \
       src\\gen_j2c.cpp src\\gen_c2j.cpp src\\main.cpp || exit 1
    ;;
  g++|clang++)
    echo "[build] using $CXX"
    "$CXX" -std=c++17 -O2 -Wall -I"$ROOT/include" -o "$OUT" $SRC || exit 1
    ;;
  *)
    echo "[build] no C++ compiler found in PATH (cl / g++ / clang++)." >&2
    echo "[build] On Windows with MSVC, run tools/build_local_msvc.bat (or run_regression_msvc.bat)." >&2
    exit 1
    ;;
esac

echo "[build] done: $OUT"
ls -la "$OUT"* 2>/dev/null