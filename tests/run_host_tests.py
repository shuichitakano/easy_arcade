#!/usr/bin/env python3
"""Run shared input tests and arcade_play integration tests."""
import os
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
env = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'}
subprocess.run([sys.executable, str(root / 'external/ea_input/tests/run_host_tests.py')], check=True, env=env)
for script in ['xinput_guide_test.py', 'xinput_report_length_test.py',
               'run_usb_host_tests.py', 'run_pad_config_tests.py',
               'run_sw_rapid_tests.py']:
    subprocess.run([sys.executable, str(root / 'tests' / script)],
                   cwd=root, env=env, check=True)
print('All host tests passed')
