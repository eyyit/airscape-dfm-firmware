#!/bin/sh
# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BUILD=${1:-build}
case "$BUILD" in build|build_a|build_b) ;; *) echo 'Expected build, build_a or build_b' >&2; exit 2;; esac
IMAGE=$(python3 -c 'import json; print(json.load(open("tools/build-env.json"))["image"])')
docker run --rm --platform linux/amd64 --network none --cpus 2 \
  --user "$(id -u):$(id -g)" -e HOME=/tmp -e CCACHE_DIR=/tmp/ccache \
  -e IDF_COMPONENT_MANAGER=0 -e IDF_COMPONENT_CACHE_PATH=/tmp/idf-cache \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/opt/esp/idf \
  -v "$ROOT:/project" -w /project "$IMAGE" \
  /opt/esp/entrypoint.sh idf.py -B "$BUILD" -DIDF_TARGET=esp32c6 reconfigure build
mkdir -p artifacts
python3 tools/artifact_manifest.py "$BUILD" artifacts/build-manifest.json
echo 'Build only. No device access or flashing is performed.'
