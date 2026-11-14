#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
docker run --rm --network none -v "$PWD:/source:ro" buildqueue-lab sh -eu -c '
mkdir /tmp/bq
cp -a /source/include /source/src /source/tests /source/demos /source/third_party /source/web /source/scripts /source/Makefile /source/CMakeLists.txt /tmp/bq/
cd /tmp/bq
make -j4
make test
python3 scripts/smoke_test.py
python3 scripts/audit_test.py
cmake -S . -B /tmp/bq-cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/bq-cmake -j4
ctest --test-dir /tmp/bq-cmake --output-on-failure
make -j4 asan
'
