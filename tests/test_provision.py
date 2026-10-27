# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Offline provisioning and preflight fault tests. No serial/network
operations.
"""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
  spec = importlib.util.spec_from_file_location(
    name, ROOT / 'tools' / (name + '.py')
  )
  value = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(value)
  return value


p = module('provision')
b = module('bench_preflight')
usb = module('export_usb')


class ProvisionTests(unittest.TestCase):
  def test_inputs_reject_unsafe_values_and_permissions(self):
    with tempfile.TemporaryDirectory() as d:
      file = Path(d) / 'wifi.json'
      file.write_text(json.dumps({'ssid': 'bench', 'password': 'password123'}))
      file.chmod(0o600)
      self.assertEqual(p.wifi_input(file)['ssid'], 'bench')
      for ssid, password in [
        ('', 'password123'),
        ('a' * 33, 'password123'),
        ('ok', 'short'),
        ('ok', 'g' * 64),
        ('x\n', 'password123'),
      ]:
        file.write_text(json.dumps({'ssid': ssid, 'password': password}))
        with self.assertRaises(ValueError):
          p.wifi_input(file)
      file.chmod(0o644)
      with self.assertRaises(ValueError):
        p.wifi_input(file)
      for host, ip in [
        ('bad/host', '192.0.2.1'),
        ('-bad', '192.0.2.1'),
        ('okay', 'nope'),
      ]:
        with self.assertRaises(ValueError):
          p.identity(host, ip)
      self.assertEqual(
        p.identity('dfm.example.lan', '192.0.2.85'), '192.0.2.85'
      )

  def test_private_prepare_signing_trust_without_login_material(self):
    import csv

    with tempfile.TemporaryDirectory() as d:
      root = Path(d)
      wifi = root / 'wifi.json'
      wifi.write_text(
        json.dumps({'ssid': 'test-only', 'password': 'test-only-password'})
      )
      wifi.chmod(0o600)
      output = root / 'private'

      def nvs(directory):
        with (directory / 'provision.csv').open() as f:
          rows = list(csv.DictReader(f))
        data = b''.join(row['value'].encode() + b'\0' for row in rows[1:])
        (directory / 'provision.nvs.bin').write_bytes(
          data.ljust(p.SIZE, b'\xff')
        )

      with patch.object(p, 'generate_nvs', side_effect=nvs):
        manifest = p.prepare(wifi, output, 'dfm.example.lan', '192.0.2.85')
      self.assertFalse(manifest['flash_authorized'])
      self.assertTrue(manifest['contains_secrets'])
      self.assertEqual(
        manifest['sha256'],
        hashlib.sha256((output / 'provision.nvs.bin').read_bytes()).hexdigest(),
      )
      self.assertFalse((output / 'provision.csv').exists())
      self.assertFalse((output / 'management-token').exists())
      self.assertFalse((output / 'tls.key').exists())
      self.assertEqual(output.stat().st_mode & 0o777, 0o700)
      self.assertTrue(
        all(file.stat().st_mode & 0o777 == 0o600 for file in output.iterdir())
      )
      with self.assertRaises(ValueError):
        p.prepare(wifi, output, 'dfm.example.lan', '192.0.2.85')
      with self.assertRaises(ValueError):
        p.prepare(
          wifi,
          ROOT / 'artifacts/should-not-exist',
          'dfm.example.lan',
          '192.0.2.85',
        )
      with patch.object(p.subprocess, 'run') as run:
        run.return_value.returncode = 1
        with self.assertRaises(ValueError) as error:
          p.run(['mock'])
        self.assertNotIn('password', str(error.exception))

  def test_generator_docker_is_offline_and_has_no_devices(self):
    with patch.object(p, 'run', return_value=b'') as run:
      p.generate_nvs(Path('/tmp/private'))
      args = run.call_args.args[0]
      self.assertIn('--network', args)
      self.assertIn('none', args)
      self.assertNotIn('--device', args)

  def test_preflight_rejects_wrong_backup_layout_and_corruption(self):
    with tempfile.TemporaryDirectory() as d:
      root = Path(d)
      stock = root / 'stock.bin'
      stock.write_bytes(b'\xff' * 0x400000)
      sha = hashlib.sha256(stock.read_bytes()).hexdigest()
      build = root / 'build'
      (build / 'bootloader').mkdir(parents=True)
      (build / 'partition_table').mkdir()
      rows = b''
      for name, kind, sub, offset, size in [
        ('nvs', 1, 2, 0x9000, 0x6000),
        ('otadata', 1, 0, 0xF000, 0x2000),
        ('phy_init', 1, 1, 0x11000, 0x1000),
        ('ota_0', 0, 16, 0x20000, 0x1C0000),
        ('ota_1', 0, 17, 0x1E0000, 0x1C0000),
        ('dfm_data', 1, 2, b.OFFSET, b.SIZE),
      ]:
        rows += struct.pack(
          '<HBBII16sI', 0x50AA, kind, sub, offset, size, name.encode(), 0
        )
      table = (
        rows + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(rows).digest()
      ).ljust(3072, b'\xff')
      b.partition_table(table)
      with self.assertRaises(ValueError):
        b.partition_table(table.replace(b'dfm_data', b'bad_data'))
      image = bytearray(1024)
      image[0] = 0xE9
      image[12] = 13
      for relative, _ in usb.FILES.values():
        (build / relative).write_bytes(bytes(image))
      (build / 'partition_table/partition-table.bin').write_bytes(table)
      (build / 'flasher_args.json').write_text(
        json.dumps(
          {
            'flash_files': {
              hex(offset): relative for relative, offset in usb.FILES.values()
            }
          }
        )
      )
      transport = root / 'usb.zip'
      usb.export(build, transport)
      private = root / 'private'
      private.mkdir()
      data = b'provision'.ljust(b.SIZE, b'\xff')
      (private / 'provision.nvs.bin').write_bytes(data)
      pm = {
        'partition': 'dfm_data',
        'offset': hex(b.OFFSET),
        'size': b.SIZE,
        'sha256': hashlib.sha256(data).hexdigest(),
      }
      (private / 'provision-manifest.json').write_text(json.dumps(pm))
      result = b.check(stock, sha, transport, private)
      self.assertTrue(result['stock_nvs_preserved'])
      self.assertFalse(result['flash_authorized'])
      with self.assertRaises(ValueError):
        b.check(stock, '0' * 64, transport, private)
      data = bytearray(stock.read_bytes())
      data[b.OFFSET] = 0
      stock.write_bytes(data)
      with self.assertRaises(ValueError):
        b.check(stock, hashlib.sha256(data).hexdigest(), transport, private)
      stock.write_bytes(b'\xff' * 0x400000)
      (private / 'provision.nvs.bin').write_bytes(b'corrupt')
      with self.assertRaises(ValueError):
        b.check(stock, sha, transport, private)
      (build / 'flasher_args.json').write_text('{"flash_files":{}}')
      with self.assertRaises(ValueError):
        usb.export(build, root / 'bad.zip')


class StockReaderTests(unittest.TestCase):
  def test_valid_strings_and_crc_rejection(self):
    import zlib

    reader = module('stock_wifi')
    page = bytearray(b'\xff' * 4096)
    page[:4] = (0xFFFFFFFE).to_bytes(4, 'little')
    page[4:8] = bytes(4)
    page[28:32] = zlib.crc32(page[4:28], 0xFFFFFFFF).to_bytes(4, 'little')
    index = 0

    def add(ns, kind, key, value):
      nonlocal index
      raw = value.encode() + b'\0' if kind == 0x21 else b''
      span = 1 + (len(raw) + 31) // 32
      entry = bytearray(b'\xff' * 32)
      entry[:4] = bytes([ns, kind, span, 255])
      entry[8:24] = key.encode().ljust(16, b'\0')
      if kind == 1:
        entry[24] = 1
      else:
        entry[24:26] = len(raw).to_bytes(2, 'little')
        entry[28:32] = zlib.crc32(raw, 0xFFFFFFFF).to_bytes(4, 'little')
      entry[4:8] = zlib.crc32(entry[:4] + entry[8:], 0xFFFFFFFF).to_bytes(
        4, 'little'
      )
      start = 64 + index * 32
      page[start : start + 32] = entry
      if raw:
        page[start + 32 : start + 32 + len(raw)] = raw
      for i in range(index, index + span):
        page[32 + i // 4] &= ~(1 << ((i % 4) * 2))
      index += span

    add(0, 1, 'wifi_cfg', '')
    add(1, 0x21, 'ssid', 'test-only')
    add(1, 0x21, 'password', 'test-password')
    partition = bytes(page) + b'\xff' * (0x6000 - 4096)
    self.assertEqual(
      reader.wifi(partition, 'wifi_cfg', 'ssid', 'password'),
      {'ssid': 'test-only', 'password': 'test-password'},
    )
    for offset in (28, 68, 128):
      corrupt = bytearray(partition)
      corrupt[offset] ^= 1
      with self.assertRaises(ValueError):
        reader.strings(bytes(corrupt))
    with self.assertRaises(ValueError):
      reader.strings(b'bad')
    with self.assertRaises(KeyError):
      reader.wifi(partition, 'missing', 'ssid', 'password')


if __name__ == '__main__':
  unittest.main()
