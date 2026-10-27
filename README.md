# Independent DFM firmware

Linux-first ESP-IDF firmware for Seeed XIAO ESP32-C6 with a Sensirion
SDP810-500Pa differential-pressure sensor. This is an independent project,
not manufacturer firmware or an official manufacturer release.

## Features

- Native Prometheus `/metrics` on HTTP port 80; no JSON exporter required.
- Light local diagnostics UI on HTTP port 8080, linked from port 80.
- Cached raw and processed differential pressure and internal temperature.
- Wi-Fi power-save control, conservative weak-signal roaming, error counters.
- Optional bounded best-effort UDP syslog, disabled by default.
- RSA-3072 signed OTA packages and rollback health checks.

The sensor exposes one differential pressure. It does not report independent
pressure at each port. Internal temperature is not room temperature. Pressure
varies with blower/zone conditions; no filter-change alert is inferred.
See docs/PRESSURE.md and docs/ROAMING.md.

## Build and test on Linux

Requires Docker, Python 3.10+, Node 22+, GCC/gcov, binutils, OpenSSL and its
development headers. The ESP-IDF image is pinned by digest.

```sh
docker pull "$(python3 -c 'import json;print(json.load(open("tools/build-env.json"))["image"])')"
python3 tools/check_public.py
python3 tools/check_licenses.py
./tools/test-all.sh
./tools/check-reproducible.sh
```

No command above connects to hardware. Build containers have no network or
USB access. CI uses synthetic credentials and ephemeral test keys only.
Fresh build_a/build_b directories are required for reproducibility checks.
Generated artifacts and private installation files must never be published.

## Private installation settings

Default identity is `dfm-device.example.invalid`; syslog is disabled.
For a private build, copy examples/deployment.local.h.example to the ignored
main/deployment.local.h and customize identity/syslog there. The header is
compiled into the binary, so customized binaries also disclose those settings.
Never distribute such binaries as generic releases. Host tests automatically
set DFM_TESTING so they use synthetic identities and ignore this header.
Run publication privacy checks in a clean tracked-source checkout; they
intentionally refuse a working copy with a private deployment header.

Wi-Fi credentials and the OTA public verification key are provisioned in a
separate NVS partition; private signing keys remain outside this checkout.
Run `python3 tools/provision.py --help` for offline preparation. Keep outputs
outside source control. Hostname/address provisioning arguments describe the
deployment manifest; build-time identity is set by the local header.
See docs/PROVISIONING.md, docs/OTA.md and SECURITY.md before use.

## Hardware and validation

I2C SDA GPIO22, SCL GPIO23, address 0x25, 100 kHz. Antenna enable GPIO3 is
active-low; GPIO14 low selects onboard antenna, high selects external.
LED GPIO15 is active-low. Independently confirm your board revision/wiring.
Sensor acquisition priority 10 and metrics priority 8 outrank management.
Host coverage and reproducibility do not replace hardware acceptance tests.

## License and contributions

Original project code, tests, configuration and documentation are licensed
under Apache-2.0. See LICENSE, NOTICE and CONTRIBUTING.md. Upstream notices under
third_party/esp-idf retain their own terms; THIRD_PARTY.md records exact SDK
provenance. Manufacturer firmware and decompiler output are not distributed.

Initial publication is source-only. CI validates builds and retains validation
reports, without uploading firmware binaries. Read SECURITY.md for private
vulnerability reporting and device trust boundaries, and docs/PUBLICATION.md
for the remaining GitHub settings and separate firmware release process.
