# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Export Linux build outputs for a separately authorized Windows USB
session.
"""

from pathlib import Path
import argparse
import hashlib
import json
import zipfile

FILES = {
  'bootloader.bin': ('bootloader/bootloader.bin', 0),
  'partition-table.bin': ('partition_table/partition-table.bin', 0x8000),
  'ota_data_initial.bin': ('ota_data_initial.bin', 0xF000),
  'airscape_dfm.bin': ('airscape_dfm.bin', 0x20000),
}


def export(build: Path, output: Path) -> dict:
  if output.exists():
    raise ValueError('Refusing to overwrite an existing USB bundle')
  sdk = json.loads((build / 'flasher_args.json').read_text())['flash_files']
  if {int(offset, 16): path for offset, path in sdk.items()} != {
    offset: relative for relative, offset in FILES.values()
  }:
    raise ValueError('SDK USB offsets differ from reviewed layout')
  payload = {
    name: (build / relative).read_bytes()
    for name, (relative, _) in FILES.items()
  }
  if not 288 <= len(payload['airscape_dfm.bin']) <= 0x1C0000:
    raise ValueError('Application does not fit expected OTA slot')
  manifest = {
    'purpose': 'Windows USB transport only; no automatic flashing',
    'flash_authorized': False,
    'nvs_included': False,
    'files': {
      name: {
        'offset': hex(FILES[name][1]),
        'bytes': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
      }
      for name, data in payload.items()
    },
  }
  payload['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
  with zipfile.ZipFile(
    output, 'x', compression=zipfile.ZIP_DEFLATED
  ) as archive:
    for name, data in sorted(payload.items()):
      info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
      info.compress_type = zipfile.ZIP_DEFLATED
      archive.writestr(info, data)
  return manifest


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--build', type=Path, default=Path('build'))
  parser.add_argument('--output', type=Path, required=True)
  args = parser.parse_args()
  print(json.dumps(export(args.build, args.output), indent=2))


if __name__ == '__main__':
  main()
