"""Run the production Xbox One decoder with sequential reports on the host.

Usage: python3 tests/xinput_guide_test.py
Only the USB transport is replaced; decoder code and pad types come from the driver.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/tusb_xinput/xinput_host.c').read_text()
header = (root / 'third_party/tusb_xinput/xinput_host.h').read_text()
constants = '\n'.join(line for line in source.splitlines() if line.startswith('#define GIP_CMD_'))
types = header[header.index('#define XINPUT_GAMEPAD_DPAD_UP'):header.index('extern usbh_class_driver_t')]
start = source.index('        else if (xid_itf->type == XBOXONE)', source.index('bool xinputh_xfer_cb'))
end = source.index('        else if (xid_itf->type == XBOXOG)', start)
decoder = source[start:end].replace('else if', 'if', 1)
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CFG_TUH_XINPUT_EPIN_BUFSIZE 64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64
typedef int xfer_result_t;
#define tu_memclr(p, n) memset(p, 0, n)
'''+constants+'\n'+types+r'''
static void xboxone_init(xinputh_interface_t *itf, uint8_t addr, uint8_t instance)
{ (void)itf; (void)addr; (void)instance; }
static void receive_report(xinputh_interface_t *xid_itf, uint8_t command, uint8_t buttons,
                           uint8_t extra, uint32_t xferred_bytes)
{
    xinput_gamepad_t *pad = &xid_itf->pad;
    uint8_t rdata[64] = {0};
    uint8_t dev_addr = 1, instance = 0;
    rdata[0] = command;
    rdata[4] = buttons;
    rdata[22] = extra;
    (void)xferred_bytes;
    xid_itf->new_pad_data = false;
'''+decoder+r'''
}
static void receive(xinputh_interface_t *itf, uint8_t command, uint8_t buttons)
{ receive_report(itf, command, buttons, 0, 23); }
int main(void)
{
    xinputh_interface_t a = {.type = XBOXONE}, b = {.type = XBOXONE};
    receive(&a, GIP_CMD_VIRTUAL_KEY, 1);
    assert(a.pad.wButtons == XINPUT_GAMEPAD_GUIDE && a.new_pad_data);
    receive(&a, GIP_CMD_INPUT, 0x10); // Guide + A
    assert(a.pad.wButtons == (XINPUT_GAMEPAD_GUIDE | XINPUT_GAMEPAD_A));
    assert(a.new_pad_data);
    receive(&b, GIP_CMD_INPUT, 0x20); // independent controller
    assert(b.pad.wButtons == XINPUT_GAMEPAD_B);
    receive(&a, GIP_CMD_INPUT, 0); // A release must preserve Guide
    assert(a.pad.wButtons == XINPUT_GAMEPAD_GUIDE);
    receive(&a, GIP_CMD_INPUT, 0x20); // second press while Guide held
    assert(a.pad.wButtons == (XINPUT_GAMEPAD_GUIDE | XINPUT_GAMEPAD_B));
    receive(&a, GIP_CMD_VIRTUAL_KEY, 0); // Guide release preserves B
    assert(a.pad.wButtons == XINPUT_GAMEPAD_B && a.new_pad_data);
    receive(&a, GIP_CMD_INPUT, 0);
    assert(a.pad.wButtons == 0);
    receive(&a, GIP_CMD_VIRTUAL_KEY, 1); // can press Guide again
    assert(a.pad.wButtons == XINPUT_GAMEPAD_GUIDE && a.new_pad_data);
    receive(&a, GIP_CMD_VIRTUAL_KEY, 0);
    // HORI Mode 1: A/B are also present in byte 22, but are not Share.
    receive_report(&a, GIP_CMD_INPUT, 0x10, 0x10, 24);
    assert(a.pad.wButtons == XINPUT_GAMEPAD_A);
    receive_report(&a, GIP_CMD_INPUT, 0x20, 0x20, 24);
    assert(a.pad.wButtons == XINPUT_GAMEPAD_B);
    // Only bit 0 represents Share; preserve other buttons when it is set.
    receive_report(&a, GIP_CMD_INPUT, 0x10, 0x11, 24);
    assert(a.pad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_SHARE));
    receive_report(&a, GIP_CMD_INPUT, 0, 1, 23);
    assert(a.pad.wButtons == XINPUT_GAMEPAD_SHARE);
    // Short reports must not consume stale data beyond their received length.
    receive_report(&a, GIP_CMD_INPUT, 0, 1, 22);
    assert(a.pad.wButtons == 0);
    receive_report(&a, GIP_CMD_INPUT, 0x20, 1, 18);
    assert(a.pad.wButtons == XINPUT_GAMEPAD_B);
    receive_report(&a, GIP_CMD_INPUT, 0, 0, 24);
    assert(a.pad.wButtons == 0);
    puts("Xbox Guide and Share report sequence tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix='xinput-guide-') as directory:
    test = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    test.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
