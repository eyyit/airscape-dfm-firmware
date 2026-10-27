# SPDX-FileCopyrightText: 2026 the DFM firmware contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge gcov runs across real-module unit tests and the socket
integration test.
"""

import gzip
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / 'artifacts/coverage'
output.mkdir(parents=True, exist_ok=True)
for path in (root / 'artifacts').rglob('*.gcda'):
  subprocess.run(
    [
      'gcov',
      '--json-format',
      '--branch-counts',
      '--branch-probabilities',
      str(path),
    ],
    cwd=output,
    check=True,
    stdout=subprocess.DEVNULL,
  )
files = {}
expected = {p.relative_to(root).as_posix() for p in (root / 'main').glob('*.c')}
expected.add('components/dfm_core/dfm_core.c')
for report in output.glob('*.gcov.json.gz'):
  data = json.loads(gzip.decompress(report.read_bytes()))
  for entry in data['files']:
    try:
      name = Path(entry['file']).resolve().relative_to(root).as_posix()
    except ValueError:
      continue
    if name not in expected:
      continue
    result = files.setdefault(name, {'lines': {}, 'functions': {}})
    for line in entry['lines']:
      n = line['line_number']
      result['lines'][n] = result['lines'].get(n, 0) + line['count']
    for function in entry['functions']:
      name_key = function['name']
      result['functions'][name_key] = (
        result['functions'].get(name_key, 0) + function['execution_count']
      )
summary = {}
for name in sorted(expected):
  value = files.get(name, {'lines': {}, 'functions': {}})
  lines = value['lines']
  functions = value['functions']
  hit = sum(count > 0 for count in lines.values())
  function_hit = sum(count > 0 for count in functions.values())
  summary[name] = {
    'lines_covered': hit,
    'lines': len(lines),
    'line_percent': round(100 * hit / len(lines), 2) if lines else 0,
    'functions_covered': function_hit,
    'functions': len(functions),
    'uncovered_functions': [
      key for key, count in functions.items() if not count
    ],
  }
(output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
missing = [
  name for name, value in summary.items() if not value['lines_covered']
]
if missing:
  raise SystemExit('Untested production C modules: ' + ', '.join(missing))

insufficient = [
  name
  for name, value in summary.items()
  if value['line_percent'] < 90
  or value['functions_covered'] != value['functions']
]
if insufficient:
  raise SystemExit(
    'Coverage gate requires >=90% lines and every C function: '
    + ', '.join(insufficient)
  )
