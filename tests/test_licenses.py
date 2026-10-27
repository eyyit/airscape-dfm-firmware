# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Fault tests for offline legal-notice integrity checks."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  'check_licenses', ROOT / 'tools/check_licenses.py'
)
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class LicenseTests(unittest.TestCase):
  def setUp(self):
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)
    self.legal = self.root / 'third_party/esp-idf'
    self.legal.mkdir(parents=True)
    self.content = b'Apache License test fixture'
    (self.root / 'LICENSE').write_bytes(self.content)
    (self.root / 'NOTICE').write_text('Project attribution')
    (self.legal / 'LICENSE').write_bytes(self.content)
    (self.root / 'tools').mkdir()
    lock = {'sdk': 'test-version', 'image': 'test@sha256:fixture'}
    (self.root / 'tools/build-env.json').write_text(json.dumps(lock))
    self.inventory = {
      **lock,
      'files': [
        {
          'path': 'third_party/esp-idf/LICENSE',
          'sdk_path': 'LICENSE',
          'sha256': hashlib.sha256(self.content).hexdigest(),
        }
      ],
    }
    self.write_inventory()

  def write_inventory(self):
    (self.legal / 'inventory.json').write_text(json.dumps(self.inventory))

  def test_intact_notices_pass(self):
    self.assertEqual(CHECK.check(self.root), [])

  def test_sdk_drift_requires_notice_refresh(self):
    self.inventory['sdk'] = 'different-version'
    self.write_inventory()
    self.assertTrue(any('SDK sdk changed' in x for x in CHECK.check(self.root)))

  def test_tampered_or_unrecorded_notices_fail(self):
    (self.legal / 'LICENSE').write_text('tampered')
    (self.legal / 'unexpected.txt').write_text('not inventoried')
    errors = CHECK.check(self.root)
    self.assertTrue(any('Modified preserved' in x for x in errors))
    self.assertTrue(any('does not match' in x for x in errors))

  def test_path_traversal_is_rejected(self):
    self.inventory['files'][0].update(
      path='third_party/esp-idf/../../../LICENSE',
      sdk_path='../../../LICENSE',
    )
    self.write_inventory()
    self.assertTrue(any('escapes' in x for x in CHECK.check(self.root)))
