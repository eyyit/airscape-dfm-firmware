# Source publication checklist

## Local checks before any authorized push

- Keep the directory/repository name airscape-dfm-firmware.
- Use a clean Git clone, with no private deployment header or ignored notes.
- Run tools/check_public.py, tools/check_licenses.py and full-history Gitleaks.
- Run Ruff format/lint, tools/test-all.sh and tools/check-reproducible.sh.
- Check both compiled outputs for private identifiers. Do not distribute them.
- Export source using git archive; confirm partitions.csv and legal files exist.
- Verify neutral commit metadata and review every intended ref; no private
  legacy branch/tag, signing key or stock image belongs in the public remote.
- Keep LICENSE, NOTICE, THIRD_PARTY.md and the exact SDK legal inventory together.

No push or repository creation is performed by the checks. The private working
copy keeps its local configuration; never delete it to bypass a publication
check. Source-only archives omit ignored settings and build/test artifacts.

## GitHub settings after repository creation is separately authorized

- Enable private vulnerability reporting before announcing the project.
- Watch security alerts and verify maintainer reporting notifications.
- Enable available secret scanning and push protection.
- Require reviewed changes and successful Firmware checks before merging main.
- Keep workflow permissions read-only; never give PR jobs production secrets.
- Add dependency update review for GitHub Actions and pinned build tools.
- Confirm the actual GitHub-hosted workflow passes. Local equivalents do not
  prove that an unrun hosted workflow succeeded.

## Firmware releases are separate

Do not attach binaries to the initial source publication. Before a later
firmware release, update/review dependencies, map exact linked components and
runtime licenses, package their required notices, complete hardware acceptance,
verify signing/identity and obtain explicit release/deployment authorization.
