# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Reject private installation data in tracked files and Git history."""

from __future__ import annotations

import argparse
import ipaddress
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IPV4 = re.compile(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])')
MAC = re.compile(r'(?i)(?<![\w:])(?:[0-9a-f]{2}:){5}[0-9a-f]{2}')
KEY = re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
PATH = re.compile(r'(?i)(?:/hom[e]/[^/\s]+|/root/cre[d]s|[A-Z]:\\Users\\)')
FORBIDDEN = {'.bin', '.elf', '.map', '.pem', '.key', '.pcap', '.pcapng'}


def findings(name: str, content: str) -> list[str]:
  """Return categories only, never print the sensitive matched values."""
  result = []
  if KEY.search(content):
    result.append('private key')
  if PATH.search(content):
    result.append('personal or credential path')
  for match in IPV4.finditer(content):
    try:
      address = ipaddress.IPv4Address(match.group())
    except ipaddress.AddressValueError:
      continue
    networks = ((0x0A000000, 8), (0xAC100000, 12), (0xC0A80000, 16))
    if any(address in ipaddress.ip_network(net) for net in networks):
      result.append('private installation address')
  for match in MAC.finditer(content):
    if not match.group().lower().startswith('02:00:00:'):
      result.append('non-synthetic MAC address')
  if Path(name).suffix in FORBIDDEN:
    result.append('private or generated artifact filename')
  if name.endswith('deployment.local.h') or name.endswith('secrets.yaml'):
    result.append('private configuration filename')
  return sorted(set(result))


def git(*args: str) -> bytes:
  """Read local Git data without contacting any remote."""
  return subprocess.check_output(['git', '-C', str(ROOT), *args])


def main() -> None:
  """Check staged files, reachable historical blobs, and author metadata."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--build', type=Path)
  args = parser.parse_args()
  failures = []
  if (ROOT / 'main/deployment.local.h').exists():
    failures.append(('private configuration', ['local header present']))
  if args.build:
    for name in (
      'airscape_dfm.bin',
      'bootloader/bootloader.bin',
      'partition_table/partition-table.bin',
    ):
      binary = args.build / name
      content = binary.read_bytes().decode(errors='replace')
      failures.extend(
        (name, [issue]) for issue in findings('compiled-output', content)
      )
  for name in git('ls-files', '-z').decode().split('\0'):
    if not name:
      continue
    path = ROOT / name
    if path.is_symlink():
      failures.append((name, ['symlink requires manual review']))
      continue
    raw = path.read_bytes()
    if b'\0' in raw:
      failures.append((name, ['binary requires manual review']))
    failures.extend(
      (name, [issue]) for issue in findings(name, raw.decode(errors='replace'))
    )
  objects = git('rev-list', '--objects', '--all').decode().splitlines()
  for entry in objects:
    fields = entry.split(' ', 1)
    if len(fields) != 2:
      continue
    oid, name = fields
    if git('cat-file', '-t', oid).strip() != b'blob':
      continue
    raw = git('cat-file', 'blob', oid)
    if b'\0' in raw:
      failures.append((name, ['historical binary requires review']))
    failures.extend(
      (name, [issue]) for issue in findings(name, raw.decode(errors='replace'))
    )
  metadata = git('log', '--all', '--format=%an <%ae>%n%cn <%ce>%n%B')
  if findings('metadata', metadata.decode()):
    failures.append(('commit metadata', ['private metadata']))
  if failures:
    for name, issues in failures:
      print(f'{name}: {", ".join(issues)}')
    raise SystemExit(1)
  print('PASS: tracked files, reachable blobs and metadata privacy checks')


if __name__ == '__main__':
  main()
