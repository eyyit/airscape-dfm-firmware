# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Offline stock NVS string reader. Does not print values or contact
hardware.
"""

import zlib


def strings(partition: bytes) -> dict[tuple[str, str], str]:
  if len(partition) != 0x6000:
    raise ValueError('Expected the backed-up 24KiB stock NVS partition')
  pages = []
  for start in range(0, len(partition), 4096):
    page = partition[start : start + 4096]
    if int.from_bytes(page[:4], 'little') not in (0xFFFFFFFE, 0xFFFFFFFC):
      continue
    if zlib.crc32(page[4:28], 0xFFFFFFFF) != int.from_bytes(
      page[28:32], 'little'
    ):
      raise ValueError('NVS page CRC mismatch')
    pages.append((int.from_bytes(page[4:8], 'little'), page))
  namespaces = {}
  values = {}
  for _, page in sorted(pages):
    i = 0
    while i < 126:
      entry = page[64 + i * 32 : 96 + i * 32]
      state = (page[32 + i // 4] >> ((i % 4) * 2)) & 3
      if state != 2:
        i += 1
        continue
      span = entry[2]
      if not span or i + span > 126:
        raise ValueError('Invalid NVS entry span')
      if zlib.crc32(entry[:4] + entry[8:], 0xFFFFFFFF) != int.from_bytes(
        entry[4:8], 'little'
      ):
        raise ValueError('NVS entry CRC mismatch')
      key = entry[8:24].split(b'\0', 1)[0].decode('ascii')
      if entry[0] == 0 and entry[1] == 1:
        namespaces[entry[24]] = key
      if entry[1] == 0x21:
        size = int.from_bytes(entry[24:26], 'little')
        raw = page[96 + i * 32 : 64 + (i + span) * 32][:size]
        if (
          not raw.endswith(b'\0')
          or len(raw) != size
          or zlib.crc32(raw, 0xFFFFFFFF)
          != int.from_bytes(entry[28:32], 'little')
        ):
          raise ValueError('NVS string CRC mismatch')
        values[(entry[0], key)] = raw[:-1].decode('utf-8')
      i += span
  return {
    (namespaces[ns], key): value
    for (ns, key), value in values.items()
    if ns in namespaces
  }


def wifi(
  partition: bytes, namespace: str, ssid_key: str, password_key: str
) -> dict:
  values = strings(partition)
  return {
    'ssid': values[(namespace, ssid_key)],
    'password': values[(namespace, password_key)],
  }
