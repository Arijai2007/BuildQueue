#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
# TSan needs its ASLR personality operation. Only this disposable, network-disabled
# container relaxes Docker's default seccomp policy; the source mount is read-only.
docker run --rm --network none --security-opt seccomp=unconfined -v "$PWD:/source:ro" buildqueue-lab sh -eu -c '
mkdir /tmp/bq
cp -a /source/include /source/src /source/tests /source/demos /source/third_party /source/Makefile /source/CMakeLists.txt /tmp/bq/
cd /tmp/bq
make -j4 tsan
'
