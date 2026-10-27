# Private provisioning and recovery

Use `python3 tools/provision.py --help` for offline profile preparation.
Input Wi-Fi JSON must be a private regular file (mode 600) containing exactly
ssid and password. Keep input/output outside the source checkout. The tool
creates a fresh signing key, public key, NVS image and deployment manifest.
Never commit or publish those files. Never reuse test fixture keys on hardware.

Custom provisioning is isolated in dfm_data at 0x3a0000, size 0x6000.
It does not replace manufacturer NVS. Check the partition table and actual
hardware before any authorized first flash. Preserve a complete private stock
flash backup, chip identity, partition layout and security/eFuse observations.
Verify readback hashes and establish a device-specific recovery procedure before
writing. No universal stock image or recovery backup is distributed here.
Do not change eFuses or claim recoverability from build/tests alone.

Linux builds and signs; Windows may transport an explicitly authorized build
over USB. tools/export_usb.py creates a transport bundle in ignored artifacts.
OTA changes only an inactive application slot. See OTA.md for health rollback
and the limits of signed updates over plain HTTP.
