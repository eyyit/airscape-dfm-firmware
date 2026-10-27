# Security policy

## Reporting a vulnerability privately

For the public GitHub repository, use Security > Advisories > Report a
vulnerability. This is GitHub private vulnerability reporting, not a public
issue. Repository administrators must enable it before announcing the project.
No public repository or reporting endpoint has been created by these sources.
If the button is unavailable, do not post exploit details, credentials or
support exports in public issues; wait for a private channel to be enabled.

Include the source commit/version, hardware and SDK versions, affected endpoint,
impact, and minimal steps using synthetic data. Redact SSIDs, addresses, MACs,
passwords, signing keys and device backups. Avoid full raw support exports.
Coordinate disclosure privately while a report is being investigated.

## Maintenance scope

This project is experimental. The current main branch is the maintenance
baseline; no older release branch has a promised backport window. There is no
contractual response, patch or availability SLA. Reproducible builds, advisory
checks and passing tests are evidence, not a vulnerability-free certification.
The pinned SDK is a reproducibility baseline, not a promise it is the latest
security release. Review SDK and transitive advisories before promotion.

## Device trust boundary

HTTP diagnostics and controls have no authentication or encryption. Use only
on a trusted, restricted network. Do not expose or reverse-proxy them publicly.
JSON validation and confirmation prompts are not access control. Status, logs,
support exports and screenshots can reveal runtime network/device identifiers.

Signed OTA verifies package authenticity and integrity, not confidentiality.
It does not authenticate other HTTP controls. No hardware Secure Boot, flash
encryption, anti-downgrade eFuses or physical readout protection are claimed.
NVS stores plaintext Wi-Fi credentials. Keep provisioning and signing material
outside Git; each installation needs its own keys, never host-test fixture keys.
UDP syslog is unencrypted, unauthenticated and best effort; disabled by default.
Never log passwords, tokens, private keys or provisioning payloads.

## Source and release hygiene

Publication checks run in a clean clone with no private deployment header.
They scan tracked files, reachable Git history and commit metadata. Gitleaks
supplements the project checks; neither proves absence of every possible secret.
Ignored files can still be force-added, and deleting a file does not erase it
from history. Customized binaries can embed private settings even when the
configuration file is ignored. Do not publish private-build artifacts.

CI has read-only repository permissions and no deployment/signing credentials.
Pull-request jobs must not receive secrets or run through pull_request_target.
Actions and build/scanner images are pinned. CI uploads validation reports only.
Initial publication is source-only; hardware acceptance, dependency review,
notices and explicit authorization are required before a firmware release.

GitHub private reporting setup:
https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository
