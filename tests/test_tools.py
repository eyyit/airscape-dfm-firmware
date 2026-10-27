"""Unit/fault and local-TLS integration tests. Never contact a real sensor."""

from __future__ import annotations
import contextlib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
import os
from pathlib import Path
import ssl
import struct
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch, MagicMock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
  'firmware', ROOT / 'tools/firmware.py'
)
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)
FIXTURE = ROOT / 'artifacts/ota-fixtures'


class ReleaseTests(unittest.TestCase):
  def setUp(self):
    self.package = FIXTURE / 'image.dfmota'
    self.public = FIXTURE / 'test-only-public.pem'
    self.manifest = fw.verify(self.package.read_bytes(), self.public)

  def test_signature_and_image_identity(self):
    self.assertEqual(self.manifest['board'], 'xiao-esp32c6')
    self.assertEqual(self.manifest['elf_sha256'], bytes(range(32)).hex())
    for offset in (0, 20, -1):
      data = bytearray(self.package.read_bytes())
      data[offset] ^= 1
      with self.assertRaises(ValueError):
        fw.verify(data, self.public)

  def test_bad_lengths_and_image_identity(self):
    for data in (
      b'',
      b'bad',
      fw.MAGIC + struct.pack('>II', 2000, 384),
      fw.MAGIC + struct.pack('>II', 10, 1),
    ):
      with self.assertRaises(ValueError):
        fw.verify(data, self.public)
    image = bytearray((FIXTURE / 'image.bin').read_bytes())
    for offset in (0, 12, 32, 80):
      corrupt = bytearray(image)
      corrupt[offset] ^= 1
      with self.assertRaises(ValueError):
        fw.image_identity(corrupt)
    with self.assertRaises(ValueError):
      fw.image_identity(b'\0' * 288)

  def test_release_refuses_overwrite_bad_commit_and_key(self):
    with tempfile.TemporaryDirectory() as d:
      out = Path(d) / 'update.dfmota'
      fw.release(
        FIXTURE / 'image.bin', FIXTURE / 'test-only-private.pem', out, 'a' * 40
      )
      self.assertEqual(fw.verify(out.read_bytes(), self.public), self.manifest)
      with self.assertRaises(ValueError):
        fw.release(
          FIXTURE / 'image.bin',
          FIXTURE / 'test-only-private.pem',
          out,
          'a' * 40,
        )
      with self.assertRaises(ValueError):
        fw.release(
          FIXTURE / 'image.bin',
          FIXTURE / 'test-only-private.pem',
          Path(d) / 'x',
          'bad',
        )
      with self.assertRaises(ValueError):
        fw.release(FIXTURE / 'image.bin', self.public, Path(d) / 'x', 'a' * 40)

  def test_origin_validation(self):
    self.assertEqual(
      fw.endpoint('https://device.local:8443'), ('device.local', 8443)
    )
    for url in (
      'https://user:pass@device',
      'https://device/path',
      'https://device/?secret=x',
      'https://device/#x',
    ):
      with self.assertRaises(ValueError):
        fw.endpoint(url)

  def test_dry_run_has_no_network_or_credentials(self):
    with patch.object(
      fw, 'request', side_effect=AssertionError('unexpected network')
    ):
      self.assertTrue(
        fw.deploy(self.package, self.public, 'https://device', None, None)[
          'dry_run'
        ]
      )

  def test_execute_requires_private_token_file(self):
    with self.assertRaises(ValueError):
      fw.deploy(self.package, self.public, 'https://device', None, None, True)
    with tempfile.TemporaryDirectory() as d:
      token = Path(d) / 'token'
      token.write_text('a' * 32)
      token.chmod(0o644)
      with self.assertRaises(ValueError):
        fw.deploy(
          self.package, self.public, 'https://device', self.public, token, True
        )
      token.chmod(0o600)
      token.write_text('bad\nvalue')
      with self.assertRaises(ValueError):
        fw.deploy(
          self.package, self.public, 'https://device', self.public, token, True
        )

  def test_stage_failure_never_activates(self):
    with tempfile.TemporaryDirectory() as d:
      token = Path(d) / 'token'
      token.write_text('a' * 32)
      token.chmod(0o600)
      with patch.object(fw, 'request', return_value={'staged': False}) as call:
        with self.assertRaises(ValueError):
          fw.deploy(
            self.package,
            self.public,
            'https://device',
            self.public,
            token,
            True,
          )
        self.assertEqual(call.call_count, 1)

  def test_activation_and_wrong_identity_fail_closed(self):
    with tempfile.TemporaryDirectory() as d:
      token = Path(d) / 'token'
      token.write_text('a' * 32)
      token.chmod(0o600)
      staged = {'staged': True, 'sha256': self.manifest['sha256']}
      with patch.object(
        fw, 'request', side_effect=[staged, {'selected': False}]
      ):
        with self.assertRaises(ValueError):
          fw.deploy(
            self.package,
            self.public,
            'https://device',
            self.public,
            token,
            True,
          )
      responses = [
        staged,
        {'selected': True, 'reboot_queued': True},
        {'ota': {'running_elf_sha256': 'wrong'}},
      ]
      with (
        patch.object(fw, 'request', side_effect=responses),
        patch.object(fw.time, 'sleep'),
        patch.object(fw.time, 'monotonic', side_effect=[0, 0, 2]),
      ):
        with self.assertRaises(ValueError):
          fw.deploy(
            self.package,
            self.public,
            'https://device',
            self.public,
            token,
            True,
            timeout=1,
          )

  def test_https_request_rejects_redirects_and_closes(self):
    connection = MagicMock()
    connection.getresponse.return_value.status = 302
    connection.getresponse.return_value.read.return_value = b'{}'
    with (
      patch.object(fw.ssl, 'create_default_context') as tls,
      patch.object(fw.http.client, 'HTTPSConnection', return_value=connection),
    ):
      with self.assertRaises(ValueError):
        fw.request('https://device', self.public, 'token', '/api')
      tls.assert_called_once_with(cafile=str(self.public))
      connection.close.assert_called_once()
    connection.reset_mock()
    connection.getresponse.return_value.status = 200
    connection.getresponse.return_value.read.return_value = b'{"ok":true}'
    with (
      patch.object(fw.ssl, 'create_default_context'),
      patch.object(fw.http.client, 'HTTPSConnection', return_value=connection),
    ):
      self.assertTrue(
        fw.request('https://device', self.public, 'token', '/api', b'')['ok']
      )

  def test_cli_verify_and_dry_run(self):
    for args in (
      [
        'verify',
        '--package',
        str(self.package),
        '--public-key',
        str(self.public),
      ],
      [
        'deploy',
        '--package',
        str(self.package),
        '--public-key',
        str(self.public),
        '--device',
        'https://device',
      ],
    ):
      with (
        patch('sys.argv', ['firmware.py', *args]),
        contextlib.redirect_stdout(io.StringIO()) as output,
      ):
        fw.main()
        self.assertTrue(json.loads(output.getvalue()))
    with (
      patch(
        'sys.argv',
        [
          'firmware.py',
          'verify',
          '--package',
          'missing',
          '--public-key',
          str(self.public),
        ],
      ),
      contextlib.redirect_stderr(io.StringIO()),
    ):
      with self.assertRaises(SystemExit):
        fw.main()

  def test_local_tls_end_to_end_stage_activate_verify(self):
    manifest = self.manifest
    public = self.public

    class Handler(BaseHTTPRequestHandler):
      def respond(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

      def do_POST(self):
        if self.headers.get('Authorization') != 'Bearer ' + 'a' * 32:
          self.send_error(401)
          return
        data = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        if self.path == '/api/v1/ota':
          fw.verify(data, public)
          self.respond({'staged': True, 'sha256': manifest['sha256']})
        elif self.path == '/api/v1/ota/activate':
          self.respond({'selected': True, 'reboot_queued': True})
        else:
          self.send_error(404)

      def do_GET(self):
        self.respond(
          {
            'valid': True,
            'system': {'metrics_ready': True},
            'ota': {
              'running_elf_sha256': manifest['elf_sha256'],
              'pending_verify': False,
            },
          }
        )

      def log_message(self, *args):
        pass

    with tempfile.TemporaryDirectory() as d:
      cert = Path(d) / 'localhost.pem'
      key = FIXTURE / 'test-only-private.pem'
      subprocess.run(
        [
          'openssl',
          'req',
          '-new',
          '-x509',
          '-key',
          str(key),
          '-out',
          str(cert),
          '-days',
          '1',
          '-subj',
          '/CN=localhost',
          '-addext',
          'subjectAltName=DNS:localhost',
        ],
        check=True,
        capture_output=True,
      )
      server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
      tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
      tls.load_cert_chain(cert, key)
      server.socket = tls.wrap_socket(server.socket, server_side=True)
      thread = threading.Thread(target=server.serve_forever, daemon=True)
      thread.start()
      token = Path(d) / 'token'
      token.write_text('a' * 32)
      token.chmod(0o600)
      try:
        result = fw.deploy(
          self.package,
          self.public,
          f'https://localhost:{server.server_port}',
          cert,
          token,
          True,
        )
        self.assertTrue(result['deployed'])
      finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


class BuildToolTests(unittest.TestCase):
  def test_shell_syntax_and_path_validation(self):
    for script in (ROOT / 'tools').glob('*.sh'):
      subprocess.run(['sh', '-n', str(script)], check=True)
    result = subprocess.run(
      [str(ROOT / 'tools/build.sh'), '../../outside'], capture_output=True
    )
    self.assertEqual(result.returncode, 2)

  def test_manifest_checks_size_and_artifact_hashes(self):
    with tempfile.TemporaryDirectory() as d:
      root = Path(d)
      (root / 'tools').mkdir()
      (root / 'tools/build-env.json').write_text('{"image":"test-image"}')
      (root / 'main').mkdir()
      (root / 'main/deployment.local.h').write_text('private configuration')
      (root / 'private').mkdir()
      (root / 'private/credentials.json').write_text('private input')
      build = root / 'build'
      (build / 'bootloader').mkdir(parents=True)
      (build / 'partition_table').mkdir()
      files = [
        'airscape_dfm.bin',
        'airscape_dfm.elf',
        'airscape_dfm.map',
        'bootloader/bootloader.bin',
        'partition_table/partition-table.bin',
        'flasher_args.json',
      ]
      for name in files:
        (build / name).write_bytes(b'test')
      script = ROOT / 'tools/artifact_manifest.py'
      output = root / 'manifest.json'
      subprocess.run(
        ['python3', str(script), 'build', str(output)],
        cwd=root,
        env={**os.environ, 'DFM_SOURCE_COMMIT': 'a' * 40},
        check=True,
      )
      value = json.loads(output.read_text())
      self.assertEqual(
        value['artifacts']['airscape_dfm.bin']['sha256'],
        hashlib.sha256(b'test').hexdigest(),
      )
      self.assertTrue(value['private_build_configuration'])
      self.assertNotIn('main/deployment.local.h', value['source_files'])
      self.assertNotIn('private/credentials.json', value['source_files'])
      (build / 'airscape_dfm.bin').write_bytes(b'\0' * (fw.SLOT_SIZE + 1))
      result = subprocess.run(
        ['python3', str(script), 'build', str(output)],
        cwd=root,
        capture_output=True,
      )
      self.assertNotEqual(result.returncode, 0)


class AdditionalToolTests(unittest.TestCase):
  @staticmethod
  def module(name):
    spec = importlib.util.spec_from_file_location(
      name, ROOT / 'tools' / (name + '.py')
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

  def test_dependency_checker_is_advisory(self):
    update = self.module('check_updates')
    versions = [
      {'tag_name': 'v5.5.6', 'prerelease': False, 'draft': False},
      {'tag_name': 'v6.0-rc', 'prerelease': True},
      {'tag_name': 'v9', 'draft': True},
    ]
    self.assertEqual(update.stable_versions(versions), ['v5.5.6'])
    response = io.BytesIO(json.dumps(versions).encode())
    with patch.object(update.urllib.request, 'urlopen', return_value=response):
      report = update.check(ROOT / 'tools/build-env.json')
    self.assertFalse(report['mutated'])
    self.assertFalse(report['deployed'])

  def test_usb_export_is_deterministic_and_excludes_nvs(self):
    usb = self.module('export_usb')
    with tempfile.TemporaryDirectory() as d:
      root = Path(d)
      build = root / 'build'
      (build / 'bootloader').mkdir(parents=True)
      (build / 'partition_table').mkdir()
      for relative, _ in usb.FILES.values():
        (build / relative).write_bytes(
          (FIXTURE / 'image.bin').read_bytes()
          if relative == 'airscape_dfm.bin'
          else b'test'
        )
      (build / 'flasher_args.json').write_text(
        json.dumps(
          {
            'flash_files': {
              hex(offset): relative for relative, offset in usb.FILES.values()
            }
          }
        )
      )
      first = root / 'first.zip'
      second = root / 'second.zip'
      manifest = usb.export(build, first)
      usb.export(build, second)
      self.assertEqual(first.read_bytes(), second.read_bytes())
      self.assertFalse(manifest['nvs_included'])
      self.assertFalse(manifest['flash_authorized'])
      with self.assertRaises(ValueError):
        usb.export(build, first)
      (build / 'airscape_dfm.bin').write_bytes(b'bad')
      with self.assertRaises(ValueError):
        usb.export(build, root / 'bad.zip')


class PlainHttpDeployTests(unittest.TestCase):
  def test_http_requests_never_send_legacy_tokens(self):
    connection = MagicMock()
    connection.getresponse.return_value.status = 200
    connection.getresponse.return_value.read.return_value = b'{"valid":true}'
    with patch.object(
      fw.http.client, 'HTTPConnection', return_value=connection
    ):
      self.assertTrue(
        fw.request(
          'http://sensor:8080', None, 'must-not-be-forwarded', '/api/v1/status'
        )['valid']
      )
      self.assertNotIn(
        'Authorization', connection.request.call_args.kwargs['headers']
      )
      self.assertEqual(
        connection.request.call_args.kwargs['headers']['X-DFM-Maintenance'],
        'accept',
      )
    connection.close.assert_called_once()

  def test_token_free_stage_activate_health_verification(self):
    manifest = fw.verify(
      (FIXTURE / 'image.dfmota').read_bytes(), FIXTURE / 'test-only-public.pem'
    )
    responses = [
      {'staged': True, 'sha256': manifest['sha256']},
      {'selected': True, 'reboot_queued': True},
      {
        'valid': True,
        'system': {'metrics_ready': True},
        'ota': {
          'pending_verify': False,
          'running_elf_sha256': manifest['elf_sha256'],
        },
      },
    ]
    with (
      patch.object(fw, 'request', side_effect=responses) as request,
      patch.object(fw.time, 'sleep'),
    ):
      result = fw.deploy(
        FIXTURE / 'image.dfmota',
        FIXTURE / 'test-only-public.pem',
        'http://sensor:8080',
        None,
        None,
        True,
      )
      self.assertTrue(result['deployed'])
      self.assertEqual(request.call_args_list[0].args[1:3], (None, ''))


class LegacyFragmentTests(unittest.TestCase):
  def test_large_https_upload_uses_bounded_record_writes(self):
    connection = MagicMock()
    connection.getresponse.return_value.status = 200
    connection.getresponse.return_value.read.return_value = b'{"staged":true}'
    body = b'x' * 20000
    with (
      patch.object(fw.ssl, 'create_default_context'),
      patch.object(fw.http.client, 'HTTPSConnection', return_value=connection),
    ):
      fw.request(
        'https://device', Path('/test-ca'), 'a' * 32, '/api/v1/ota', body
      )
    self.assertEqual(connection.blocksize, 1024)
    payload = connection.request.call_args.kwargs['body']
    self.assertEqual(payload.read(), body)
    self.assertEqual(
      connection.request.call_args.kwargs['headers']['Content-Length'],
      str(len(body)),
    )


if __name__ == '__main__':
  unittest.main()
