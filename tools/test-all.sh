#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts/sdk artifacts/units artifacts/coverage artifacts/generated
IMAGE=$(python3 -c 'import json;print(json.load(open("tools/build-env.json"))["image"])')
printf '#define DFM_VERSION "%s"\n' "$(cat VERSION)" > artifacts/generated/dfm_version.h
find artifacts -name '*.gcda' -delete
# Test doubles are ours; cJSON remains the exact vendor implementation from SDK.
if [ ! -f artifacts/sdk/cJSON.c ]; then
 docker run --rm --network none --entrypoint tar "$IMAGE" -C /opt/esp/idf/components/json/cJSON -cf - cJSON.c cJSON.h | tar -xf - -C artifacts/sdk
fi
python3 tests/prepare_ota_fixtures.py artifacts/ota-fixtures
(cd web && ld -r -b binary -o ../artifacts/units/ui.o index.html console.js)
objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents artifacts/units/ui.o
objcopy --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly artifacts/units/ui.o
for module in sensor runtime network servers app ota syslog; do
 cc -std=c11 -D_DEFAULT_SOURCE -DDFM_TESTING -Wall -Wextra -Werror -g -O0 --coverage -fprofile-abs-path \
  -fsanitize=address,undefined -Iartifacts/generated -Itests/fakes/include -Iartifacts/sdk -Imain -Icomponents/dfm_core/include \
  "tests/unit_$module.c" tests/fakes/sdk.c components/dfm_core/dfm_core.c artifacts/sdk/cJSON.c \
  artifacts/units/ui.o -lcrypto -lm -o "artifacts/units/$module"
 ASAN_OPTIONS=detect_leaks=1 "./artifacts/units/$module" artifacts/ota-fixtures
 done
./tools/test.sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --test tests/test_console.cjs
python3 tools/coverage_report.py
