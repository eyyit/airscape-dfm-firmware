# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Verify preserved legal files and their exact SDK provenance offline."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(root: Path) -> list[str]:
  """Return drift/errors without modifying notices or contacting a device."""
  root = root.resolve()
  manifest = root / 'third_party/esp-idf/inventory.json'
  inventory = json.loads(manifest.read_text())
  lock = json.loads((root / 'tools/build-env.json').read_text())
  errors = []
  for field in ('sdk', 'image'):
    if inventory.get(field) != lock.get(field):
      errors.append(f'SDK {field} changed; refresh exact-version legal files')
  seen = set()
  for record in inventory['files']:
    name = record['path']
    path = root / name
    expected = 'third_party/esp-idf/' + record['sdk_path']
    if name != expected or name in seen:
      errors.append('Invalid or duplicate legal-file record')
      continue
    seen.add(name)
    if path.is_symlink() or not path.resolve().is_relative_to(
      root / 'third_party/esp-idf'
    ):
      errors.append('Legal-file path escapes its source directory')
      continue
    if not path.is_file():
      errors.append(f'Missing preserved legal file: {name}')
      continue
    if hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
      errors.append(f'Modified preserved legal file: {name}')
  actual = {
    p.relative_to(root).as_posix()
    for p in (root / 'third_party/esp-idf').rglob('*')
    if p.is_file() and p != manifest
  }
  if actual != seen:
    errors.append('Legal-file inventory does not match files on disk')
  license_path = root / 'LICENSE'
  original = root / 'third_party/esp-idf/LICENSE'
  if (
    not original.is_file() or license_path.read_bytes() != original.read_bytes()
  ):
    errors.append('Project LICENSE differs from reviewed Apache-2.0 text')
  if not (root / 'NOTICE').is_file():
    errors.append('Project NOTICE is missing')
  return errors


def main() -> None:
  """Fail publication checks when legal provenance is stale or incomplete."""
  errors = check(ROOT)
  if errors:
    for error in errors:
      print(error)
    raise SystemExit(1)
  print('PASS: exact SDK provenance and all preserved legal-file hashes')


if __name__ == '__main__':
  main()
