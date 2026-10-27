# Independent DFM firmware

Use two-space Python indentation, single quotes, and 80-column lines.
Run privacy checks, host tests, and two clean builds before release.
Never commit private provisioning, deployment.local.h, signing keys, backups,
packet captures, runtime exports, or installation-specific incident notes.
Use synthetic test identities and reserved documentation addresses only.
Do not flash or deploy without explicit authorization. CI builds are unsigned.
Sampling priority 10 and metrics priority 8 outrank diagnostics and logging.
The SDP810 supplies one differential pressure, not independent port pressures.
Preserve signed OTA verification, NVS isolation and pressure processing tests.
No automatic calibration or dirty-filter inference. Do not copy vendor firmware.
Host tests do not establish hardware, RF, flash timing or power-loss acceptance.

Host tests define DFM_TESTING and must ignore private deployment headers.
Publication checks intentionally require a clean checkout without the header.
Never remove a local header simply to make a privacy check pass.
