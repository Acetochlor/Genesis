#!/usr/bin/env bash
# Build and run Genesis. Uses the bundled Qt 6.11.1 MinGW toolchain.
set -e

QT_DIR="C:/Qt/6.11.1/mingw_64"
TOOLS="C:/Qt/Tools"
export PATH="$TOOLS/CMake_64/bin:$TOOLS/Ninja:$TOOLS/mingw1310_64/bin:$QT_DIR/bin:$PATH"

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_CXX_COMPILER="$TOOLS/mingw1310_64/bin/g++.exe"

cmake --build build

echo "Launching Genesis..."
./build/bin/Genesis.exe
