# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Generate test-only signing material and synthetic images. Never
provision it.
"""

from pathlib import Path
import importlib.util
import json
import struct
import subprocess
import sys


def prepare(root: Path) -> None:
  root.mkdir(parents=True, exist_ok=True)
  marker = root / 'README.txt'
  if any(root.iterdir()) and not marker.exists():
    raise ValueError('Refusing to overwrite a non-fixture directory')
  marker.write_text('TEST-ONLY KEYS/SYNTHETIC IMAGES. Never provision these.\n')
  for name in ('image.dfmota', 'tiny.dfmota'):
    (root / name).unlink(missing_ok=True)
  spec = importlib.util.spec_from_file_location(
    'firmware', Path(__file__).resolve().parents[1] / 'tools/firmware.py'
  )
  fw = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(fw)
  key = root / 'test-only-private.pem'
  public = root / 'test-only-public.pem'
  subprocess.run(
    [
      'openssl',
      'genpkey',
      '-algorithm',
      'RSA',
      '-pkeyopt',
      'rsa_keygen_bits:3072',
      '-out',
      str(key),
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    check=True,
  )
  public.write_bytes(fw.openssl('pkey', '-in', str(key), '-pubout'))

  def image(length: int) -> bytes:
    b = bytearray(length)
    b[0] = 0xE9
    struct.pack_into('<H', b, 12, 13)
    struct.pack_into('<I', b, 32, 0xABCD5432)
    b[48 : 48 + 9] = b'0.1.0-dev'
    b[80 : 80 + 12] = b'airscape_dfm'
    b[176:208] = bytes(range(32))
    return bytes(b)

  for name, length in [('image', 4096), ('tiny', 288)]:
    p = root / (name + '.bin')
    p.write_bytes(image(length))
    fw.release(p, key, root / (name + '.dfmota'), 'a' * 40)
  package = (root / 'image.dfmota').read_bytes()
  n, s = struct.unpack_from('>II', package, 8)
  manifest = json.loads(package[16 : 16 + n])
  payload = package[16 + n + s :]
  manifest['board'] = 'wrong-board'
  raw = json.dumps(manifest, separators=(',', ':')).encode()
  signature = fw.openssl('dgst', '-sha256', '-sign', str(key), data=raw)
  (root / 'wrong-board.dfmota').write_bytes(
    fw.MAGIC
    + struct.pack('>II', len(raw), len(signature))
    + raw
    + signature
    + payload
  )
  (root / 'README.txt').write_text(
    'TEST-ONLY KEYS/SYNTHETIC IMAGES. '
    'Never use these keys to provision a real device.\n'
  )


if __name__ == '__main__':
  prepare(Path(sys.argv[1]))
