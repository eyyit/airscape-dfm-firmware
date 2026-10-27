#!/bin/sh
# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts/generated
printf '#define DFM_VERSION "%s"\n' "$(cat VERSION)" > artifacts/generated/dfm_version.h
cc --coverage -fprofile-abs-path -O0 -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g \
  -Iartifacts/generated -Icomponents/dfm_core/include components/dfm_core/dfm_core.c tests/test_core.c \
  -lm -o artifacts/test_core
ASAN_OPTIONS=detect_leaks=1 ./artifacts/test_core artifacts/sample-metrics.prom
cc --coverage -fprofile-abs-path -O0 -std=c11 -D_DEFAULT_SOURCE -DDFM_HOST -DDFM_METRICS_PORT=17894 \
  -Wall -Wextra -Werror -pedantic -pthread -Iartifacts/generated -Icomponents/dfm_core/include \
  components/dfm_core/dfm_core.c main/metrics.c tests/host_metrics.c -lm -o artifacts/host_metrics
python3 tests/test_metrics_server.py > artifacts/metrics-listener-test.json
cat artifacts/metrics-listener-test.json
