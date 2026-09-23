#!/usr/bin/env python3
"""Run the firmware's host-side regression tests; no board is required."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
env = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'}
with tempfile.TemporaryDirectory(prefix='arcade-host-tests-') as directory:
    for name, source in [('hid_info_test', 'hid_info.cpp'),
                         ('switch_pro_test', 'switch_pro.cpp'),
                         ('switch_baud_compat_test', 'switch_pro.cpp'),
                         ('macro_profile_test', 'macro_profile.cpp')]:
        binary = Path(directory) / name
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++17', '-fsanitize=address,undefined', '-I.',
            'tests/' + name + '.cpp', source, '-o', str(binary),
        ], cwd=root, check=True)
        subprocess.run([str(binary)], cwd=root, env=env, check=True)
    for script in ['xinput_guide_test.py', 'xinput_report_length_test.py',
                   'run_usb_host_tests.py']:
        subprocess.run([sys.executable, str(root / 'tests' / script)],
                       cwd=root, env=env, check=True)
print('All host tests passed')
