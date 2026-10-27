"""Prepare private initial provisioning offline. Never connects to a device."""

from __future__ import annotations
import argparse
import csv
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PARTITION = 'dfm_data'
OFFSET = 0x3A0000
SIZE = 0x6000


def run(args: list[str]) -> bytes:
  result = subprocess.run(args, capture_output=True)
  if result.returncode:
    # Tool errors can contain CSV rows, filenames or secret data.
    raise ValueError('Offline tool failed; no device was contacted')
  return result.stdout


def private_file(path: Path) -> None:
  if path.is_symlink() or not path.is_file() or path.stat().st_mode & 0o077:
    raise ValueError('Input must be a regular private file (mode 600)')


def wifi_input(path: Path) -> dict:
  private_file(path)
  value = json.loads(path.read_text())
  if set(value) != {'ssid', 'password'}:
    raise ValueError('Wi-Fi JSON requires exactly ssid and password')
  ssid, password = value['ssid'], value['password']
  if (
    not isinstance(ssid, str)
    or not 1 <= len(ssid.encode()) <= 32
    or any(c in ssid for c in '\0\r\n')
  ):
    raise ValueError('SSID must be 1-32 UTF-8 bytes without controls')
  if (
    not isinstance(password, str)
    or not password.isascii()
    or any(ord(c) < 32 or ord(c) > 126 for c in password)
  ):
    raise ValueError('Wi-Fi passphrase must be printable ASCII')
  if not (
    8 <= len(password) <= 63
    or len(password) == 64
    and re.fullmatch(r'[0-9a-fA-F]{64}', password)
  ):
    raise ValueError(
      'WPA2 passphrase must be 8-63 characters or 64 hexadecimal digits'
    )
  return value


def identity(hostname: str, address: str) -> str:
  if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9.-]{0,252}', hostname):
    raise ValueError(
      'Use a DNS hostname containing letters, digits, dots or hyphens'
    )
  for label in hostname.split('.'):
    if (
      not 1 <= len(label) <= 63 or label.startswith('-') or label.endswith('-')
    ):
      raise ValueError('Invalid DNS label')
  return str(ipaddress.IPv4Address(address))


def generate_nvs(directory: Path) -> None:
  image = json.loads((ROOT / 'tools/build-env.json').read_text())['image']
  run(
    [
      'docker',
      'run',
      '--rm',
      '--network',
      'none',
      '--user',
      f'{os.getuid()}:{os.getgid()}',
      '-e',
      'HOME=/tmp',
      '-v',
      f'{directory.resolve()}:/private',
      '-w',
      '/private',
      image,
      'python',
      '-m',
      'esp_idf_nvs_partition_gen',
      'generate',
      'provision.csv',
      'provision.nvs.bin',
      hex(SIZE),
    ]
  )


def prepare(wifi_file: Path, output: Path, hostname: str, address: str) -> dict:
  wifi = wifi_input(wifi_file)
  address = identity(hostname, address)
  if output.exists() or output.is_symlink():
    raise ValueError('Refusing to overwrite a provisioning directory')
  # Resolve outside the checkout, even if a symlinked parent points into it.
  output = output.resolve()
  if output.is_relative_to(ROOT):
    raise ValueError(
      'Production provisioning must be outside the source checkout'
    )
  output.mkdir(mode=0o700, parents=False)
  old_umask = os.umask(0o077)
  try:

    def openssl(*args: str) -> bytes:
      return run(['openssl', *args])

    def p(name: str) -> str:
      return str(output / name)

    openssl(
      'genpkey',
      '-algorithm',
      'RSA',
      '-pkeyopt',
      'rsa_keygen_bits:3072',
      '-out',
      p('signing.key'),
    )
    openssl(
      'pkey',
      '-in',
      p('signing.key'),
      '-pubout',
      '-out',
      p('signing-public.pem'),
    )
    values = {
      'wifi_ssid': wifi['ssid'],
      'wifi_password': wifi['password'],
      'ota_pubkey': (output / 'signing-public.pem').read_text(),
    }
    with (output / 'provision.csv').open('w', newline='') as csvfile:
      writer = csv.writer(csvfile)
      writer.writerow(['key', 'type', 'encoding', 'value'])
      writer.writerow(['dfm_provision', 'namespace', '', ''])
      for key, value in values.items():
        writer.writerow([key, 'data', 'string', value])
    generate_nvs(output)
    data = (output / 'provision.nvs.bin').read_bytes()
    if len(data) != SIZE or any(
      value.encode() + b'\0' not in data for value in values.values()
    ):
      raise ValueError(
        'NVS generation did not round-trip every provisioning value'
      )
    manifest = {
      'schema': 1,
      'partition': PARTITION,
      'offset': hex(OFFSET),
      'size': SIZE,
      'sha256': hashlib.sha256(data).hexdigest(),
      'hostname': hostname,
      'ip': address,
      'flash_authorized': False,
      'contains_secrets': True,
      'http': True,
      'management_token_required': False,
    }
    (output / 'provision-manifest.json').write_text(
      json.dumps(manifest, indent=2) + '\n'
    )
    # NVS contains plaintext secrets. Keep only necessary long-lived files.
    for name in ('provision.csv',):
      (output / name).unlink()
    return manifest
  finally:
    os.umask(old_umask)
    for file in output.iterdir():
      if file.is_file():
        file.chmod(0o600)


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--wifi-file', type=Path, required=True)
  parser.add_argument('--output', type=Path, required=True)
  parser.add_argument('--hostname', required=True)
  parser.add_argument('--ip', required=True)
  args = parser.parse_args()
  try:
    print(
      json.dumps(
        prepare(args.wifi_file, args.output, args.hostname, args.ip), indent=2
      )
    )
  except (OSError, ValueError, TypeError) as error:
    parser.exit(
      1,
      f'Preparation failed ({type(error).__name__}); '
      'private inputs were not printed. No flashing performed.\n',
    )


if __name__ == '__main__':
  main()
