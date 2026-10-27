"""Regression tests for public-repository privacy boundaries."""

import importlib.util
from pathlib import Path
import unittest
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  'check_public', ROOT / 'tools/check_public.py'
)
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class PublicPrivacyTests(unittest.TestCase):
  def test_private_addresses_are_rejected(self):
    for octets in ((10, 1, 2, 3), (172, 20, 1, 3), (192, 168, 2, 3)):
      address = '.'.join(str(value) for value in octets)
      self.assertIn(
        'private installation address', CHECK.findings('example', address)
      )

  def test_reserved_examples_are_allowed(self):
    content = '192.0.2.85 127.0.0.1 dfm-device.example.invalid'
    self.assertEqual(CHECK.findings('example', content), [])

  def test_identifiers_and_artifacts_are_rejected(self):
    self.assertTrue(CHECK.findings('backup.bin', 'backup'))
    key = '-----BEGIN ' + 'PRIVATE KEY-----'
    self.assertTrue(CHECK.findings('input', key))
    mac = ':'.join(['a4', '11', '22', '33', '44', '55'])
    self.assertTrue(CHECK.findings('input', mac))
    self.assertEqual(CHECK.findings('input', '02:00:00:00:00:01'), [])
    self.assertTrue(CHECK.findings('main/deployment.local.h', 'config'))

  def test_defaults_are_generic_and_logging_disabled(self):
    source = (ROOT / 'main/device_identity.h').read_text()
    self.assertIn('"dfm-device.example.invalid"', source)
    self.assertIn('#define DFM_SYSLOG_ADDRESS ""', source)
    console = (ROOT / 'web/console.js').read_text()
    self.assertNotIn('apNames', console)

  def test_local_header_overrides_defaults_without_touching_source(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / 'device_identity.h').write_bytes(
        (ROOT / 'main/device_identity.h').read_bytes()
      )
      (root / 'deployment.local.h').write_text(
        '#define DFM_DEVICE_HOSTNAME "custom.example.invalid"\n'
        '#define DFM_DEVICE_SHORTNAME "custom-device"\n'
        '#define DFM_SYSLOG_ADDRESS "192.0.2.2"\n'
        '#define DFM_SYSLOG_PORT 1514\n'
      )
      result = subprocess.check_output(
        [
          'cc',
          '-E',
          '-dM',
          '-x',
          'c',
          '-include',
          str(root / 'device_identity.h'),
          '/dev/null',
        ],
        text=True,
      )
      self.assertIn('DFM_DEVICE_HOSTNAME "custom.example.invalid"', result)
      self.assertIn('DFM_SYSLOG_ADDRESS "192.0.2.2"', result)
      self.assertIn('DFM_SYSLOG_PORT 1514', result)
      isolated = subprocess.check_output(
        [
          'cc',
          '-E',
          '-dM',
          '-x',
          'c',
          '-DDFM_TESTING',
          '-include',
          str(root / 'device_identity.h'),
          '/dev/null',
        ],
        text=True,
      )
      self.assertIn(
        'DFM_DEVICE_HOSTNAME "dfm-device.example.invalid"', isolated
      )
      self.assertIn('DFM_SYSLOG_ADDRESS ""', isolated)
      self.assertNotIn('custom.example.invalid', isolated)
