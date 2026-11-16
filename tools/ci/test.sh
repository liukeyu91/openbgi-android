#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
export BGI_TEST_CJK_FONT=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
export BGI_FONT="$BGI_TEST_CJK_FONT"
test -f "$BGI_TEST_CJK_FONT"
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-14
python3 -m unittest discover -s tools/ci -p 'test_*.py' -v
make -j4 CC=clang ${SANITIZERS:+SANITIZERS=$SANITIZERS} test
for script in tools/android/tests/run-*-tests.sh; do sh "$script"; done
