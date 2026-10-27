"""Record a build's inputs and checksums without exposing provisioning
material.
"""

import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

build = Path(sys.argv[1])
files = [
  'airscape_dfm.bin',
  'airscape_dfm.elf',
  'airscape_dfm.map',
  'bootloader/bootloader.bin',
  'partition_table/partition-table.bin',
  'flasher_args.json',
]
manifest = {
  'environment': json.loads(Path('tools/build-env.json').read_text()),
  'source_commit': os.environ.get('DFM_SOURCE_COMMIT')
  or subprocess.run(
    ['git', 'rev-parse', 'HEAD'], capture_output=True, text=True
  ).stdout.strip()
  or 'uncommitted',
  'hardware_validated': False,
  'flash_authorized': False,
  'artifacts': {},
}
# Only reviewed tracked inputs belong in distributable metadata. A source
# archive without Git falls back to explicit source roots, never a tree walk.
tracked = None
if shutil.which('git'):
  tracked = subprocess.run(
    ['git', 'ls-files', '-z'], capture_output=True, check=False
  )
if tracked is not None and tracked.returncode == 0:
  inputs = [Path(name) for name in tracked.stdout.decode().split('\0') if name]
else:
  inputs = [
    path
    for root in ('main', 'components', 'tools', 'web', 'tests')
    for path in Path(root).rglob('*')
    if path.suffix in ('.c', '.h', '.py', '.sh', '.js', '.cjs', '.json')
  ]
manifest['private_build_configuration'] = Path(
  'main/deployment.local.h'
).exists()
manifest['source_files'] = {
  path.as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
  for path in sorted(inputs)
  if path.is_file()
  and not path.is_symlink()
  and path.name != 'deployment.local.h'
}
for name in files:
  path = build / name
  manifest['artifacts'][name] = {
    'bytes': path.stat().st_size,
    'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
  }
if (build / 'airscape_dfm.bin').stat().st_size > 0x1C0000:
  raise SystemExit('Application exceeds existing OTA slot')
Path(sys.argv[2]).write_text(json.dumps(manifest, indent=2) + '\n')
