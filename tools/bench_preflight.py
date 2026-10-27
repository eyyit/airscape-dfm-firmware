"""Offline same-device backup and first-flash layout checks. No
serial/network I/O.
"""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile
import struct

OFFSET = 0x3A0000
SIZE = 0x6000


def partition_table(data: bytes) -> None:
  expected = {
    'nvs': (1, 2, 0x9000, 0x6000),
    'otadata': (1, 0, 0xF000, 0x2000),
    'phy_init': (1, 1, 0x11000, 0x1000),
    'ota_0': (0, 16, 0x20000, 0x1C0000),
    'ota_1': (0, 17, 0x1E0000, 0x1C0000),
    'dfm_data': (1, 2, OFFSET, SIZE),
  }
  found = {}
  md5 = False
  for i in range(0, len(data) - 31, 32):
    row = data[i : i + 32]
    if row[:2] == b'\xeb\xeb':
      if (
        row[2:16] != b'\xff' * 14 or row[16:] != hashlib.md5(data[:i]).digest()
      ):
        raise ValueError('Partition-table checksum mismatch')
      md5 = True
      break
    if row[:2] != b'\xaa\x50':
      raise ValueError('Invalid partition table')
    _, kind, subtype, offset, size, label, flags = struct.unpack(
      '<HBBII16sI', row
    )
    label = label.rstrip(b'\0').decode('ascii')
    if flags or label in found:
      raise ValueError('Partition flags/duplicate rejected')
    found[label] = (kind, subtype, offset, size)
  if not md5 or found != expected:
    raise ValueError(
      'Partition layout differs from reviewed first-bench layout'
    )


def check(backup: Path, backup_sha: str, usb: Path, provisioning: Path) -> dict:
  stock = backup.read_bytes()
  if len(stock) != 0x400000 or hashlib.sha256(stock).hexdigest() != backup_sha:
    raise ValueError('Stock backup size/hash mismatch')
  if stock[OFFSET : OFFSET + SIZE] != b'\xff' * SIZE:
    raise ValueError(
      'Custom partition region is not erased in the verified stock backup'
    )
  with zipfile.ZipFile(usb) as archive:
    manifest = json.loads(archive.read('manifest.json'))
    expected = {
      'bootloader.bin': 0,
      'partition-table.bin': 0x8000,
      'ota_data_initial.bin': 0xF000,
      'airscape_dfm.bin': 0x20000,
    }
    if set(archive.namelist()) != set(expected) | {'manifest.json'} or set(
      manifest['files']
    ) != set(expected):
      raise ValueError('Unexpected files in public USB transport')
    partition_table(archive.read('partition-table.bin'))
    writes = []
    for name, offset in expected.items():
      data = archive.read(name)
      info = manifest['files'][name]
      if (
        int(info['offset'], 16) != offset
        or len(data) != info['bytes']
        or hashlib.sha256(data).hexdigest() != info['sha256']
      ):
        raise ValueError('USB payload offset/size/hash mismatch')
      limit = {
        'bootloader.bin': 0x8000,
        'partition-table.bin': 0x1000,
        'ota_data_initial.bin': 0x2000,
        'airscape_dfm.bin': 0x1C0000,
      }[name]
      if name in ('airscape_dfm.bin', 'bootloader.bin') and (
        len(data) < 24 or data[0] != 0xE9 or data[12:14] != b'\x0d\x00'
      ):
        raise ValueError('Not an ESP32-C6 application/bootloader image')
      if not data or len(data) > limit:
        raise ValueError('Payload exceeds assigned flash region')
      writes.append(
        {
          'file': name,
          'offset': hex(offset),
          'bytes': len(data),
          'sha256': info['sha256'],
        }
      )
  pm = json.loads((provisioning / 'provision-manifest.json').read_text())
  data = (provisioning / 'provision.nvs.bin').read_bytes()
  if (
    pm['partition'] != 'dfm_data'
    or int(pm['offset'], 16) != OFFSET
    or pm['size'] != SIZE
    or len(data) != SIZE
    or pm['sha256'] != hashlib.sha256(data).hexdigest()
  ):
    raise ValueError('Private provisioning layout/hash mismatch')
  writes.append(
    {
      'file': 'provision.nvs.bin',
      'offset': hex(OFFSET),
      'bytes': SIZE,
      'sha256': pm['sha256'],
    }
  )
  # Compare sector-rounded writes, not only file extents. Never touch stock
  # NVS/PHY.
  for write in writes:
    start = int(write['offset'], 16) // 4096 * 4096
    end = (int(write['offset'], 16) + write['bytes'] + 4095) // 4096 * 4096
    for low, high in ((0x9000, 0xF000), (0x11000, 0x12000)):
      if start < high and end > low:
        raise ValueError('Write overlaps protected stock NVS/PHY')
  return {
    'offline_checks_passed': True,
    'flash_authorized': False,
    'device_identity_live_verified': False,
    'stock_nvs_preserved': True,
    'stock_phy_preserved': True,
    'writes': writes,
    'next': (
      'Separately authorize flash; verify live chip/MAC/security '
      'and COM port before writes'
    ),
  }


def main() -> None:
  p = argparse.ArgumentParser(description=__doc__)
  p.add_argument('--backup', type=Path, required=True)
  p.add_argument('--backup-sha256', required=True)
  p.add_argument('--usb', type=Path, required=True)
  p.add_argument('--provisioning', type=Path, required=True)
  a = p.parse_args()
  print(
    json.dumps(
      check(a.backup, a.backup_sha256, a.usb, a.provisioning), indent=2
    )
  )


if __name__ == '__main__':
  main()
