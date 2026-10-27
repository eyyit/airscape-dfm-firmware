# Third-party attribution

## Original project material

Original firmware, host doubles/tests, web UI, Python and shell tooling,
configuration examples and documentation are licensed under Apache-2.0.
See LICENSE and NOTICE. SPDX identifiers mark original source files.
The copyright credit uses the collective name "the DFM firmware contributors";
no personal email, home domain or installation identity is required.

This is an independent implementation for compatible hardware. Manufacturer
firmware, stock backups, extracted binaries, decompiler output and manufacturer
manuals are not distributed. Product names do not imply endorsement or grant
trademark rights. Hardware documentation is referenced rather than copied.

## Exact external SDK provenance

The build lock is tools/build-env.json: ESP-IDF v5.5.5, source commit
b774170ff46c393eeb5e495ea37936038d3f4f4f, immutable container digest
sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2.

third_party/esp-idf contains 46 unmodified legal files copied from that image,
including its root LICENSE and docs/en/COPYRIGHT.rst. inventory.json records
original SDK paths and SHA-256 hashes. These notices retain their upstream
licenses and are explicitly excluded from our Apache-2.0 grant.

The collection is deliberately broader than the linked firmware: presence of
a notice does not claim that component is linked, used, or relicensed. It is
not a firmware SBOM. Per-file upstream notices take precedence over summaries.
The root SDK copyright overview includes additional component/ROM attributions.

Relevant dependencies include:

- Espressif framework, Wi-Fi, PHY and coexistence libraries: upstream Apache-2.0
  notices are preserved. Hardware-specific code remains hardware-specific.
- FreeRTOS: MIT; see components/freertos/FreeRTOS-Kernel/LICENSE.md.
- lwIP: BSD terms; see components/lwip/lwip/COPYING.
- cJSON: MIT; see components/json/cJSON/LICENSE. Host tests extract its exact
  implementation from the pinned SDK into ignored artifacts; source is not
  vendored into this repository.
- Mbed TLS: Apache-2.0; see components/mbedtls/mbedtls/LICENSE and included
  nested legal files for their own terms.
- wpa_supplicant: BSD terms; see components/wpa_supplicant/README.
- Newlib and optional Picolibc: multiple notices, preserved in their complete
  COPYING files; the collection does not imply optional Picolibc is enabled.

All paths above are relative to third_party/esp-idf. Other SDK legal files are
included with their original terms, including optional components not enabled
by this application. No SDK implementation or radio binary is vendored here.

## Build and test tools

Docker images, GCC/binutils, Python, Node, OpenSSL, Ruff, Gitleaks and GitHub
Actions remain external tools under their own licenses. They are not relicensed
or distributed by this source repository. Host tests link the system OpenSSL
library; test executables and ephemeral test keys are not release artifacts.

## Source publication versus binary releases

The initial GitHub publication is source-only. CI builds unsigned development
images for validation but uploads validation reports only, not firmware, ELF,
map, USB bundles, signing material or provisioning images.

Before distributing any firmware/toolchain-containing bundle, review the exact
linked components and toolchain runtime (including applicable GCC runtime
exceptions), regenerate notices for the exact SDK, and ship required license,
copyright and NOTICE material beside the binaries. Do not infer binary-release
clearance solely from this source inventory or from successful compilation.

When changing the SDK lock, refresh these files from the new exact image and
update inventory.json. Never silently relabel old notices as a newer SDK.
Run python3 tools/check_licenses.py to detect lock drift or modified notices.
