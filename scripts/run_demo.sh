#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
make
exec ./buildqueue --demo "${1:-fcfs}"
