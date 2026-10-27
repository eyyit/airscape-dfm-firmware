# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Read-only SDK release checker. Never changes a lock, builds or deploys."""

import json
from pathlib import Path
import urllib.request


def stable_versions(releases: list[dict]) -> list[str]:
  return [
    release['tag_name']
    for release in releases
    if not release.get('prerelease') and not release.get('draft')
  ]


def check(lock_path: Path = Path('tools/build-env.json')) -> dict:
  lock = json.loads(lock_path.read_text())
  with urllib.request.urlopen(
    'https://api.github.com/repos/espressif/esp-idf/releases', timeout=20
  ) as response:
    versions = stable_versions(json.load(response))
  series = '.'.join(lock['sdk'].split('.')[:2]) + '.'
  return {
    'pinned': lock['sdk'],
    'same_series': [v for v in versions if v.startswith(series)],
    'recent_stable': versions[:8],
    'mutated': False,
    'deployed': False,
  }


def main() -> None:
  print(json.dumps(check(), indent=2))


if __name__ == '__main__':
  main()
