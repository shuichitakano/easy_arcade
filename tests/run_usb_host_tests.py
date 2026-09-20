#!/usr/bin/env python3
"""Run host-side USB tests against the pinned TinyUSB implementation.

The register/host stubs compile selected production functions without the
RP2040 SDK. This tests state/ownership rules, not silicon timing.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
TINYUSB = ROOT / 'third_party/tinyusb/src'


def function_source(path, signature):
    source = path.read_text()
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='arcade-usb-tests-') as directory:
    tmp = Path(directory)
    (tmp / 'tinyusb_recovery_under_test.inc').write_text(function_source(
        TINYUSB / 'host/usbh.c', 'void tuh_enumeration_recovery_task(void)'))
    (tmp / 'tinyusb_abort_under_test.inc').write_text(function_source(
        TINYUSB / 'portable/raspberrypi/rp2040/hcd_rp2040.c',
        'bool hcd_edpt_abort_xfer('))
    cases = [
        ('usb_recovery_test', []),
        ('rp2040_abort_test', []),
        ('usb_enumeration_test', []),
        ('usb_enumeration_lifecycle_test', ['usb_enumeration.cpp']),
    ]
    for name, extra in cases:
        executable = tmp / name
        command = shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-I' + str(tmp),
            '-Itests/usb_enumeration_stubs', '-I.',
            'tests/' + name + '.cpp', *extra, '-o', str(executable),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], check=True,
                       env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
