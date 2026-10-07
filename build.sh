#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p dist
if [[ -z "${WINDOWS_CXX:-}" ]]; then
  if command -v x86_64-w64-mingw32-clang++ >/dev/null 2>&1; then
    WINDOWS_CXX=x86_64-w64-mingw32-clang++
  else
    WINDOWS_CXX=x86_64-w64-mingw32-g++
  fi
fi
WINDOWS_WINDRES="${WINDOWS_WINDRES:-x86_64-w64-mingw32-windres}"
cd src
"$WINDOWS_WINDRES" pet.rc -O coff -o ../dist/pet-res.o
"$WINDOWS_CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -municode -mwindows -static -Wl,--strip-all \
  main.cpp ../dist/pet-res.o -o ../dist/FluffyPet.exe \
  -lgdiplus -lole32 -lshell32 -lcomdlg32 -luser32 -lgdi32
cd ..
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  tests/state_test.cpp -o dist/state_test
./dist/state_test
printf 'Built: dist/FluffyPet.exe\n'
