# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Local UI fixture only. Never contacts or configures a physical device."""

from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]
START = time.monotonic()
CONFIG = {
  'schema': 1,
  'interval_ms': 1000,
  'timeout_ms': 100,
  'recovery_failures': 3,
  'backoff_ms': 5000,
  'stale_ms': 3000,
  'alpha': 0.2,
  'offset_pa': 0,
  'orientation': 1,
  'setpoint_pa': 125,
  'hysteresis_pa': 10,
  'power_save': False,
}


class Handler(BaseHTTPRequestHandler):
  def do_GET(self) -> None:
    if self.path in ('/', '/console.js'):
      html = (
        ROOT / ('web/index.html' if self.path == '/' else 'web/console.js')
      ).read_text(encoding='utf-8')
      html = html.replace(
        'Acquisition first', 'SIMULATED UI TEST · NO DEVICE CONNECTION'
      )
      self.send_response(200)
      self.send_header(
        'Content-Type',
        'text/html; charset=utf-8'
        if self.path == '/'
        else 'application/javascript',
      )
      self.end_headers()
      self.wfile.write(html.encode())
      return
    if self.path == '/api/v1/status':
      value = {
        'firmware': '0.4.0-dev / SIMULATED',
        'sdk': 'fixture',
        'boot_id': 42,
        'revision': 1,
        'uptime_seconds': time.monotonic() - START,
        'reset_reason': 1,
        'valid': True,
        'sample_age_seconds': 0.2,
        'error_code': 0,
        'consecutive_errors': 0,
        'crc_valid_mask': 7,
        'last_frame_hex': '00 00 81 15 18 4f 00 3c 11',
        'config': CONFIG,
        'syslog': {
          'destination': '192.0.2.2:514/UDP',
          'sent': 12,
          'errors': 0,
          'dropped': 0,
          'delivery': 'best effort; sent is not delivery confirmation',
        },
        'sensor_dp_pa': 0.03,
        'cor_dp_pa': 0,
        'processing_difference_pa': -0.03,
        'temp_f': 80.6,
        'high_static': False,
        'successful_samples': 100,
        'last_good': {
          'counts': 0,
          'scale': 60,
          'sensor_pa': 0.03,
          'filter_pa': 0.01,
          'raw_dp_pa': 0,
          'canonical_dp_pa': 0,
          'cor_dp_pa': 0,
          'temp_c': 27,
        },
        'i2c_errors': 2,
        'crc_errors': 1,
        'scale_errors': 0,
        'recoveries': 1,
        'missed_deadlines': 0,
        'sample_duration_us': 1700,
        'sample_max_us': 2100,
        'network': {
          'connected': True,
          'ip': '192.0.2.85',
          'gateway': '192.0.2.1',
          'netmask': '255.255.255.0',
          'bssid': '02:00:00:00:00:01',
          'channel': 6,
          'rssi': -55,
          'power_save_actual': False,
          'disconnect_reason': 0,
          'reconnects': 1,
          'connected_seconds': 123,
        },
        'system': {
          'free_heap': 120000,
          'metrics_requests': 84,
          'metrics_rejected': 0,
          'management_rejected': 0,
          'ota_upload_enabled': False,
        },
      }
    elif self.path == '/api/v1/logs':
      value = {
        'overwritten': 0,
        'entries': [
          {'seconds': 1, 'message': 'SIMULATED: sensor initialized'},
          {'seconds': 2, 'message': 'SIMULATED: Wi-Fi connected'},
        ],
      }
    elif self.path == '/api/v1/history':
      value = [
        {
          'seconds': i,
          'valid': i not in (20, 21),
          'pressure_pa': (i % 9 - 4) * 0.1,
          'temp_c': 27,
        }
        for i in range(100)
      ]
    else:
      self.send_error(404)
      return
    self.send_response(200)
    self.send_header('Content-Type', 'application/json')
    self.end_headers()
    self.wfile.write(json.dumps(value).encode())

  def log_message(self, format: str, *args: object) -> None:
    pass


if __name__ == '__main__':
  import os

  HTTPServer(
    (os.environ.get('DFM_PREVIEW_BIND', '127.0.0.1'), 8768), Handler
  ).serve_forever()
