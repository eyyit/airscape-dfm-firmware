#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
for DIR in build_a build_b; do
  if [ -e "$DIR" ]; then echo "Refusing to reuse $DIR; use a clean checkout." >&2; exit 1; fi
done
./tools/build.sh build_a
./tools/build.sh build_b
for FILE in airscape_dfm.bin bootloader/bootloader.bin partition_table/partition-table.bin; do
  cmp "build_a/$FILE" "build_b/$FILE"
  sha256sum "build_a/$FILE"
done
echo 'PASS: application, bootloader and partition table match across clean build directories.'
