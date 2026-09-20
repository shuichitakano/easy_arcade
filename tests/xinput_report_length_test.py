"""Exercise the production XInput transfer callback with truncated/stale reports."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/tusb_xinput/xinput_host.c').read_text()
header = (root / 'third_party/tusb_xinput/xinput_host.h').read_text()
constants = '\n'.join(line for line in source.splitlines() if line.startswith('#define GIP_CMD_'))
types = header[header.index('#define XINPUT_GAMEPAD_DPAD_UP'):header.index('extern usbh_class_driver_t')]
callback = source[source.index('bool xinputh_xfer_cb('):source.index('void xinputh_close(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CFG_TUH_XINPUT_EPIN_BUFSIZE 64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64
typedef int xfer_result_t;
#define XFER_RESULT_SUCCESS 0
#define TUSB_DIR_IN 1
#define tu_memclr(p, n) memset(p, 0, n)
#define TU_LOG2(...) ((void)0)
'''+constants+'\n'+types+r'''
static xinputh_interface_t device;
static unsigned received, rearmed, initialized;
static uint8_t tu_edpt_dir(uint8_t ep) { return ep >> 7; }
static uint8_t get_instance_id_by_epaddr(uint8_t addr, uint8_t ep)
{ (void)addr; (void)ep; return 0; }
static xinputh_interface_t *get_instance(uint8_t addr, uint8_t instance)
{ (void)addr; (void)instance; return &device; }
static void xboxone_init(xinputh_interface_t *itf, uint8_t addr, uint8_t instance)
{ (void)itf; (void)addr; (void)instance; ++initialized; }
static bool tuh_xinput_receive_report(uint8_t addr, uint8_t instance)
{ (void)addr; (void)instance; ++rearmed; return true; }
static void tuh_xinput_report_received_cb(uint8_t addr, uint8_t instance,
                                          const xinputh_interface_t *itf, uint16_t len)
{
    (void)itf; (void)len; ++received;
    tuh_xinput_receive_report(addr, instance);
}
static void (*tuh_xinput_report_sent_cb)(uint8_t, uint8_t, const uint8_t *, uint16_t);
'''+callback+r'''
static void reset(xinput_type_t type)
{
    memset(&device, 0, sizeof(device));
    device.type = type;
    received = rearmed = initialized = 0;
}
static void receive(unsigned len)
{
    assert(xinputh_xfer_cb(1, 0x81, XFER_RESULT_SUCCESS, len));
}
static void input_lengths(xinput_type_t type, unsigned minimum)
{
    reset(type);
    uint8_t *b = device.epin_buf;
    if (type == XBOXONE) { b[0] = GIP_CMD_INPUT; b[4] = 0x10; }
    else if (type == XBOX360_WIRELESS) { b[1] = 1; b[5] = 0x13; b[7] = 0x10; }
    else if (type == XBOX360_WIRED) { b[1] = 0x14; b[3] = 0x10; }
    else { b[1] = 0x14; b[4] = 0xff; }
    receive(minimum);
    assert(received == 1 && device.pad.wButtons == XINPUT_GAMEPAD_A);
    // Keep a valid old report in the buffer. Every truncated prefix must leave
    // the current pad intact and rearm reception without publishing stale data.
    device.pad.wButtons = XINPUT_GAMEPAD_B;
    const xinput_gamepad_t previous = device.pad;
    received = rearmed = 0;
    for (unsigned len = 0; len < minimum; ++len)
    {
        receive(len);
        assert(received == 0 && rearmed == len + 1);
        assert(memcmp(&device.pad, &previous, sizeof(previous)) == 0);
    }
    memset(b, 0, 64);
    if (type == XBOXONE) b[0] = GIP_CMD_INPUT;
    else if (type == XBOX360_WIRELESS) { b[1] = 1; b[5] = 0x13; }
    else b[1] = 0x14;
    receive(minimum);
    assert(received == 1 && device.pad.wButtons == 0);
}
int main(void)
{
    input_lengths(XBOX360_WIRED, 14);
    input_lengths(XBOX360_WIRELESS, 18);
    input_lengths(XBOXONE, 18);
    input_lengths(XBOXOG, 20);
    reset(XBOXONE);
    device.epin_buf[0] = GIP_CMD_VIRTUAL_KEY;
    device.epin_buf[4] = 1;
    for (unsigned len = 0; len < 5; ++len) receive(len);
    assert(received == 0 && device.pad.wButtons == 0 && rearmed == 5);
    receive(5);
    assert(received == 1 && device.pad.wButtons == XINPUT_GAMEPAD_GUIDE);
    device.epin_buf[4] = 0;
    receive(4);
    assert(received == 1 && device.pad.wButtons == XINPUT_GAMEPAD_GUIDE);
    receive(5);
    assert(received == 2 && device.pad.wButtons == 0);
    reset(XBOXONE);
    device.epin_buf[0] = GIP_CMD_ANNOUNCE;
    for (unsigned len = 0; len < 4; ++len) receive(len);
    assert(initialized == 0 && rearmed == 4);
    receive(4);
    assert(initialized == 1);
    reset(XBOX360_WIRELESS);
    device.epin_buf[0] = 8;
    device.epin_buf[1] = 1;
    receive(0); receive(1);
    assert(!device.connected && rearmed == 2);
    receive(2);
    assert(device.connected);
    device.epin_buf[1] = 0;
    receive(1);
    assert(device.connected);
    receive(2);
    assert(!device.connected);
    puts("XInput truncated report tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix='xinput-length-') as directory:
    test = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    test.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
