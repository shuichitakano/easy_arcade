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
        ('xinput_startup_test', []),
        ('xinput_descriptors_test', ['usb_enumeration.cpp']),
        ('usb_hub_reset_status_test', []),
        ('ext_hub_cache_test', []),
        ('usb_hub_enable_test', []),
        ('usb_reenumerate_test', []),
        ('usb_recovery_test', []),
        ('rp2040_abort_test', []),
        ('usb_enumeration_test', []),
        ('usb_enumeration_lifecycle_test', ['usb_enumeration.cpp']),
    ]
    (tmp / 'ext_hub_cache_under_test.inc').write_text(
        function_source(TINYUSB / 'host/hub.c', 'uint8_t hub_port_count(') +
        function_source(ROOT / 'hid_app.cpp', 'void checkExtHub()'))
    startup = 'std::array<bool, CFG_TUH_DEVICE_MAX> xinputInputPending_{};\n'
    startup += function_source(ROOT / 'hid_app.cpp', 'void xinputStartupTask()')
    (tmp / 'xinput_startup_under_test.inc').write_text(startup)
    (tmp / 'hub_enable_under_test.inc').write_text(function_source(
        TINYUSB / 'host/hub.c', 'static void hub_port_get_status_complete (tuh_xfer_t* xfer)\n{'))
    (tmp / 'tinyusb_reenumerate_under_test.inc').write_text(function_source(
        TINYUSB / 'host/usbh.c', 'bool tuh_device_reenumerate('))
    reset_branch = (TINYUSB / 'host/usbh.c').read_text().split(
        'case ENUM_RESET_1:', 1)[1].split('//case ENUM_HUB_GET_STATUS_1:', 1)[0]
    (tmp / 'tinyusb_reset_completion_under_test.inc').write_text(
        'static void reset_completion_under_test() { do {\n' + reset_branch +
        '\n} while (false); }\n')
    source = (TINYUSB / 'host/usbh.c').read_text()
    branches = function_source(TINYUSB / 'host/usbh.c', 'static void enum_hub_reset_wait(')
    for number, end in [(1, 'case ENUM_HUB_GET_STATUS_2:'), (2, '#endif')]:
        branch = source.split(f'case ENUM_HUB_CLEAR_RESET_{number}:', 1)[1].split(end, 1)[0]
        branches += f'static void reset_status_{number}(tuh_xfer_t* xfer) {{ do {{\n' + branch + '\n} while(false); }\n'
    (tmp / 'hub_reset_status_under_test.inc').write_text(branches)
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
