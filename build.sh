#!/bin/bash
set -e

echo "Building epaper-qpa for aarch64"
docker run --rm -v $(pwd):/build eeems/remarkable-toolchain:latest-rmpp bash -c "
  cd /build &&
  . /opt/codex/*/*/environment-setup-* &&
  mkdir -p build-aarch64 &&
  cd build-aarch64 &&
  cmake .. &&
  make -j\$(nproc) &&
  aarch64-remarkable-linux-strip libepaper.so
"
echo "Built: build-aarch64/libepaper.so"
