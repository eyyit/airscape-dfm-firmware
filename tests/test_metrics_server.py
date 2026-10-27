"""Exercise the actual metrics listener with synthetic snapshots and
slow peers.
"""

from concurrent.futures import ThreadPoolExecutor
import json
import socket
import statistics
import subprocess
import time

PORT = 17894


def request(
  path: str = '/metrics', host: str = 'test', extra: str = ''
) -> tuple[bytes, float]:
  start = time.monotonic()
  with socket.create_connection(('127.0.0.1', PORT), timeout=1) as client:
    client.sendall(
      f'GET {path} HTTP/1.1\r\nHost: {host}\r\n{extra}\r\n'.encode()
    )
    response = bytearray()
    while data := client.recv(16384):
      response.extend(data)
  return bytes(response), (time.monotonic() - start) * 1000


process = subprocess.Popen(['artifacts/host_metrics'])
slow = []
try:
  for attempt in range(100):
    try:
      data, _ = request()
      break
    except OSError:
      time.sleep(0.02)
  else:
    raise AssertionError('Listener did not start')
  assert b'200 OK' in data and b'hvac_filter_temperature_celsius 27\n' in data
  head, body = data.split(b'\r\n\r\n', 1)
  assert int(
    next(
      row.split(b':')[1]
      for row in head.split(b'\r\n')
      if row.startswith(b'Content-Length:')
    )
  ) == len(body)
  assert b'404 Not Found' in request('/unexpected')[0]
  assert b'Location: http://test:8080/\r\n' in request('/')[0]
  assert (
    b'Location: http://test:8080/api/v1/status\r\n'
    in request('/api/v1/status')[0]
  )
  assert b'400 Bad Request' in request('/', 'bad/host')[0]
  assert (
    b'307 Temporary Redirect'
    in request('/', extra='User-Agent: ' + ('browser ' * 100) + '\r\n')[0]
  )
  baseline = [request()[1] for _ in range(100)]
  # Three incomplete headers must not monopolize the fourth slot.
  slow = [
    socket.create_connection(('127.0.0.1', PORT), timeout=1) for _ in range(3)
  ]
  for client in slow:
    client.sendall(b'GET /metrics HTTP/1.1\r\nHost: ')
  contested = []
  for _ in range(50):
    response, latency = request()
    assert b'200 OK' in response
    contested.append(latency)
  # Expired peers release slots; several concurrent valid requests recover.
  time.sleep(0.35)
  with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(lambda _: request(), range(100)))
  assert all(b'200 OK' in response for response, _ in results)
  report = {
    'mode': (
      'POSIX socket harness; synthetic samples, not ESP32 or radio performance'
    ),
    'requests': 252,
    'baseline_p95_ms': statistics.quantiles(baseline, n=100)[94],
    'slow_peer_p95_ms': statistics.quantiles(contested, n=100)[94],
    'max_slow_peer_ms': max(contested),
    'result': 'PASS',
  }
  assert max(contested) < 250
  print(json.dumps(report, indent=2))
finally:
  for client in slow:
    client.close()
  process.terminate()
  process.wait(timeout=5)
