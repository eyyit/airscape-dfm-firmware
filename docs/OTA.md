# Linux signed OTA over plain HTTP

Normal updates require no login token or TLS/CA management. The Linux private
RSA-3072 signing key is the firmware trust mechanism. The MCU stores only its
public key in dfm_data/dfm_provision/ota_pubkey. The signed manifest binds board,
chip, schema, version, source commit, application hash and ELF identity.

```sh
./tools/test-all.sh
./tools/build.sh
python3 tools/firmware.py release --image build/airscape_dfm.bin  --key /private/signing.key --source-commit "$(git rev-parse HEAD)"  --output artifacts/release.dfmota
python3 tools/firmware.py verify --package artifacts/release.dfmota  --public-key /private/signing-public.pem
python3 tools/firmware.py deploy --package artifacts/release.dfmota  --public-key /private/signing-public.pem --device http://DEVICE_FQDN:8080
```

The last command is a network-free dry run. Add --execute only when deployment
is authorized. The tool stages, checks the exact hash, requests activation and
verifies the running ELF identity plus sensor/metrics health. It never follows
redirects or automatically reflashes after failure. An invalid upload never
selects a boot slot. OTA is maintenance: single-core flash writes can pause
acquisition even though management priority is low.

The first OTA boot must get three fresh successful sensor samples and both
local listeners ready within 30 seconds before confirmation, otherwise rollback
is requested. No hardware Secure Boot, flash encryption or anti-downgrade eFuse
policy is enabled. Schema migration beyond 1 is not supported yet.

## One-time legacy transition

Existing 0.2.x firmware used HTTPS with a token. The tool temporarily supports
--ca and --token-file for that source transport, and --confirm-device for the
new post-reboot HTTP origin. The legacy token is never sent to HTTP. After the
transition, omit those flags. Retain old private files for rollback/recovery;
the new runtime ignores their NVS entries. No token or CA is created for new
provisioning profiles.

./tools/check-updates.sh only reports dependency versions. Intentionally update
the pinned SDK digest, rerun tests/two builds, review and sign before deploying.
Deployment is explicit; these tools never schedule unattended flashing.
