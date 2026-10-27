#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Linux release packaging and explicit signed OTA deployment. No USB
support.
"""

from __future__ import annotations
import argparse
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import ssl
import struct
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

MAGIC = b'DFMOTA1\n'
SLOT_SIZE = 0x1C0000
ALGORITHM = 'RSA3072-PKCS1v15-SHA256'


def image_identity(image: bytes) -> dict[str, str]:
  """Read the ESP32-C6 application identity without executing vendor tooling."""
  if not 288 <= len(image) <= SLOT_SIZE or image[0] != 0xE9:
    raise ValueError('Invalid application image size/header')
  if (
    struct.unpack_from('<H', image, 12)[0] != 13
    or struct.unpack_from('<I', image, 32)[0] != 0xABCD5432
  ):
    raise ValueError('Expected ESP32-C6 application descriptor')

  def text(offset: int) -> str:
    return image[offset : offset + 32].split(b'\0', 1)[0].decode('ascii')

  version, project = text(48), text(80)
  if not version or len(version) > 31 or project != 'airscape_dfm':
    raise ValueError('Wrong application project or invalid version')
  return {'version': version, 'elf_sha256': image[176:208].hex()}


def openssl(*args: str, data: bytes | None = None) -> bytes:
  result = subprocess.run(
    ['openssl', *args], input=data, capture_output=True, check=False
  )
  if result.returncode:
    raise ValueError('OpenSSL operation failed')
  return result.stdout


def release(
  image_path: Path, key: Path, output: Path, source_commit: str
) -> dict:
  image = image_path.read_bytes()
  identity = image_identity(image)
  if len(source_commit) != 40 or any(
    c not in '0123456789abcdef' for c in source_commit
  ):
    raise ValueError('Exact Git commit required')
  if output.exists():
    raise ValueError('Refusing to overwrite an existing release')
  with tempfile.TemporaryDirectory(prefix='dfm-sign-') as directory:
    public = Path(directory) / 'public.pem'
    public.write_bytes(openssl('pkey', '-in', str(key), '-pubout'))
    # A 3072-bit RSA key produces exactly 384 signature bytes; also inspect
    # type.
    key_text = openssl(
      'pkey', '-pubin', '-in', str(public), '-text', '-noout'
    ).decode()
    if '3072 bit' not in key_text or 'Modulus:' not in key_text:
      raise ValueError('Expected RSA-3072 signing key')
    manifest = {
      'schema': 1,
      'config_schema': 1,
      'board': 'xiao-esp32c6',
      'chip': 'esp32c6',
      'algorithm': ALGORITHM,
      'size': len(image),
      'sha256': hashlib.sha256(image).hexdigest(),
      'source_commit': source_commit,
      **identity,
    }
    raw = json.dumps(
      manifest, sort_keys=True, separators=(',', ':'), allow_nan=False
    ).encode()
    signature = openssl('dgst', '-sha256', '-sign', str(key), data=raw)
    if len(signature) != 384 or len(raw) > 1024:
      raise ValueError('Unsupported key/manifest length')
    package = (
      MAGIC
      + struct.pack('>II', len(raw), len(signature))
      + raw
      + signature
      + image
    )
    verify(package, public)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects a previously signed release from replacement.
    with output.open('xb') as stream:
      stream.write(package)
  return manifest


def verify(package: bytes, public_key: Path) -> dict:
  if (
    len(package) < 16 or len(package) > SLOT_SIZE + 1552 or package[:8] != MAGIC
  ):
    raise ValueError('Invalid package envelope')
  manifest_size, signature_size = struct.unpack_from('>II', package, 8)
  if not 1 <= manifest_size <= 1024 or signature_size != 384:
    raise ValueError('Invalid manifest/signature length')
  start = 16 + manifest_size
  raw, signature, image = (
    package[16:start],
    package[start : start + signature_size],
    package[start + signature_size :],
  )
  with tempfile.TemporaryDirectory(prefix='dfm-verify-') as directory:
    sig = Path(directory) / 'signature'
    sig.write_bytes(signature)
    openssl(
      'dgst',
      '-sha256',
      '-verify',
      str(public_key),
      '-signature',
      str(sig),
      data=raw,
    )

  def unique(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for name, value in pairs:
      if name in result:
        raise ValueError('Duplicate manifest field')
      result[name] = value
    return result

  manifest = json.loads(raw, object_pairs_hook=unique)
  if (
    not isinstance(manifest, dict)
    or type(manifest.get('schema')) is not int
    or manifest['schema'] != 1
    or type(manifest.get('config_schema')) is not int
    or manifest['config_schema'] != 1
    or manifest.get('board') != 'xiao-esp32c6'
    or manifest.get('chip') != 'esp32c6'
    or manifest.get('algorithm') != ALGORITHM
    or type(manifest.get('size')) is not int
    or manifest['size'] != len(image)
    or manifest.get('sha256') != hashlib.sha256(image).hexdigest()
  ):
    raise ValueError('Manifest identity, schema or hash mismatch')
  if any(
    manifest.get(name) != value for name, value in image_identity(image).items()
  ):
    raise ValueError('Signed manifest does not match application identity')
  return manifest


def endpoint(url: str) -> tuple[str, int]:
  parts = urlsplit(url)
  if (
    parts.scheme not in ('http', 'https')
    or not parts.hostname
    or parts.username
    or parts.password
    or parts.path not in ('', '/')
    or parts.query
    or parts.fragment
  ):
    raise ValueError(
      'Use an HTTP(S) device origin without credentials, path or query'
    )
  return parts.hostname, parts.port or (443 if parts.scheme == 'https' else 80)


def request(
  origin: str, ca: Path | None, token: str, path: str, body: bytes | None = None
) -> dict:
  host, port = endpoint(origin)
  legacy = urlsplit(origin).scheme == 'https'
  if legacy:
    if ca is None or not token:
      raise ValueError('Legacy HTTPS migration requires CA and token')
    context = ssl.create_default_context(cafile=str(ca))
    connection = http.client.HTTPSConnection(
      host, port, context=context, timeout=60
    )
  else:
    connection = http.client.HTTPConnection(host, port, timeout=60)
  headers = {'X-DFM-Maintenance': 'accept'}
  if legacy:
    headers['Authorization'] = 'Bearer ' + token
  if body is not None:
    headers.update(
      {
        'Content-Type': 'application/octet-stream',
        'Content-Length': str(len(body)),
      }
    )
  try:
    # Legacy firmware has a 4096-byte TLS receive buffer. Do not let the
    # Python SSL socket emit default 16KiB application-data records.
    if legacy:
      connection.blocksize = 1024
    payload = io.BytesIO(body) if legacy and body is not None else body
    connection.request(
      'GET' if body is None else 'POST', path, body=payload, headers=headers
    )
    response = connection.getresponse()
    data = response.read(65537)
    # No redirects, alternate origins, proxy environment or certificate
    # bypasses.
    if response.status != 200 or len(data) > 65536:
      raise ValueError(
        f'Device returned HTTP {response.status}; no redirect/retry performed'
      )
    return json.loads(data)
  finally:
    connection.close()


def deploy(
  package_path: Path,
  public_key: Path,
  origin: str,
  ca: Path | None,
  token_file: Path | None,
  execute: bool = False,
  timeout: float = 90,
  confirm_origin: str | None = None,
) -> dict:
  manifest = verify(package_path.read_bytes(), public_key)
  endpoint(origin)
  if confirm_origin:
    endpoint(confirm_origin)
  if not execute:
    return {
      'dry_run': True,
      'manifest': manifest,
      'device': origin,
      'actions': [
        'stage signed application',
        'select inactive slot and reboot',
        'verify health/identity',
      ],
    }
  token = ''
  if urlsplit(origin).scheme == 'https':
    if ca is None or token_file is None:
      raise ValueError('Legacy migration requires CA and token file')
    if os.name == 'posix' and token_file.stat().st_mode & 0o077:
      raise ValueError('Token file must be private')
    token = token_file.read_text().strip()
    if (
      not 32 <= len(token) <= 64
      or not token.isascii()
      or any(ord(c) <= 32 or ord(c) >= 127 for c in token)
    ):
      raise ValueError('Invalid management token')
  staged = request(origin, ca, token, '/api/v1/ota', package_path.read_bytes())
  if (
    staged.get('staged') is not True
    or staged.get('sha256') != manifest['sha256']
  ):
    raise ValueError(
      'Device did not confirm the exact staged image; not activating'
    )
  activated = request(origin, ca, token, '/api/v1/ota/activate', b'')
  if (
    activated.get('selected') is not True
    or activated.get('reboot_queued') is not True
  ):
    raise ValueError(
      'Activation/reboot not confirmed; inspect device before retrying'
    )
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    time.sleep(1)
    try:
      status = request(confirm_origin or origin, ca, token, '/api/v1/status')
    except (OSError, ValueError, http.client.HTTPException):
      continue
    ota = status.get('ota', {})
    if (
      ota.get('running_elf_sha256') == manifest['elf_sha256']
      and ota.get('pending_verify') is False
      and status.get('valid') is True
      and status.get('system', {}).get('metrics_ready') is True
    ):
      return {
        'deployed': True,
        'version': manifest['version'],
        'elf_sha256': manifest['elf_sha256'],
      }
  raise ValueError(
    'New firmware identity/health not confirmed; '
    'rollback or connectivity failure possible. No automatic reflash.'
  )


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__)
  commands = parser.add_subparsers(dest='command', required=True)
  sign = commands.add_parser('release')
  sign.add_argument('--image', required=True, type=Path)
  sign.add_argument('--key', required=True, type=Path)
  sign.add_argument('--output', required=True, type=Path)
  sign.add_argument('--source-commit', required=True)
  check = commands.add_parser('verify')
  check.add_argument('--package', required=True, type=Path)
  check.add_argument('--public-key', required=True, type=Path)
  push = commands.add_parser('deploy')
  push.add_argument('--package', required=True, type=Path)
  push.add_argument('--public-key', required=True, type=Path)
  push.add_argument('--device', required=True)
  push.add_argument('--ca', type=Path)
  push.add_argument('--token-file', type=Path)
  push.add_argument(
    '--confirm-device',
    help='Post-reboot HTTP origin for one-time migration from legacy HTTPS',
  )
  push.add_argument(
    '--execute',
    action='store_true',
    help='Explicitly authorize stage, activation and reboot',
  )
  args = parser.parse_args()
  try:
    if args.command == 'release':
      result = release(args.image, args.key, args.output, args.source_commit)
    elif args.command == 'verify':
      result = verify(args.package.read_bytes(), args.public_key)
    else:
      result = deploy(
        args.package,
        args.public_key,
        args.device,
        args.ca,
        args.token_file,
        args.execute,
        confirm_origin=args.confirm_device,
      )
    print(json.dumps(result, indent=2))
  except (
    OSError,
    ValueError,
    KeyError,
    struct.error,
    http.client.HTTPException,
  ) as error:
    parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
  main()
