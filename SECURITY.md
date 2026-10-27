# Security and privacy boundaries

HTTP diagnostics and controls have no authentication or encryption. Use only
on a trusted, restricted network. Do not expose or reverse-proxy them publicly.
Status responses include runtime network/device identifiers; support exports
and screenshots may reveal installation details even with generic source.

Signed OTA verifies package authenticity; it does not provide confidentiality.
No hardware Secure Boot, flash encryption, anti-downgrade eFuses or physical
readout protection are claimed. NVS contains plaintext Wi-Fi credentials.
UDP syslog is unencrypted, unauthenticated and best effort; disabled by default.
Never log passwords, tokens, private keys or provisioning payloads.

Build/test success is not a vulnerability-free certification. The SDK lock is
reproducible, not an assertion that it is the newest supported security patch.
Review framework and transitive advisories before promoting any release.
No security response SLA is promised. Do not file secrets in public issues.
A private reporting channel must be selected before public release.

Privacy checks must pass against all reachable Git blobs and commit metadata.
Gitleaks supplements the project checks. Neither proves absence of every secret.
Never add deployment headers, private NVS/stock backups, signing keys, runtime
exports, captures or private incident reports to Git. Removing a current file
does not remove it from history.
