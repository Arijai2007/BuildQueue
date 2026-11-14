#!/usr/bin/env sh
set -u
cd "$(dirname "$0")/.."
make asan
asan_status=$?
make tsan
tsan_status=$?
printf 'ASan/UBSan exit: %s\nThreadSanitizer exit: %s\n' "$asan_status" "$tsan_status"
test "$asan_status" -eq 0 && test "$tsan_status" -eq 0
