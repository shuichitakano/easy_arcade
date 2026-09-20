#include "hid_info.h"
#include "fixtures/8bitdo_mac_report_descriptor.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Adapter 2 PSC interface 0: ten buttons and two 2-bit digital axes.
    const uint8_t gamepad[]={
        0x05,0x01,0x09,0x05,0xa1,0x01,0x15,0x00,
        0x25,0x01,0x75,0x01,0x95,0x0a,0x05,0x09,
        0x19,0x01,0x29,0x0a,0x81,0x02,0x05,0x01,
        0x09,0x30,0x09,0x31,0x15,0x00,0x25,0x02,
        0x35,0x00,0x45,0x02,0x75,0x02,0x95,0x02,
        0x81,0x02,0x75,0x01,0x95,0x02,0x81,0x01,0xc0};
    // Interface 1 must not replace gamepad data with neutral controls.
    const uint8_t consumer[]={
        0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x01,
        0x19,0x00,0x2a,0x3c,0x02,0x15,0x00,0x26,
        0x3c,0x02,0x95,0x01,0x75,0x10,0x81,0x00,
        0xc0,0x05,0x01,0x09,0x80,0xa1,0x01,0x85,
        0x02,0x19,0x81,0x29,0x83,0x25,0x01,0x75,
        0x01,0x95,0x03,0x81,0x02,0x95,0x05,0x81,0x01,0xc0};
    HIDInfo pad, media;
    pad.parseDesc(gamepad,gamepad+sizeof gamepad);
    media.parseDesc(consumer,consumer+sizeof consumer);
    uint32_t buttons;
    int hat;
    std::array<int,HIDInfo::N_ANALOGS> axes;
    const uint8_t neutral[]={0,0x14}, right[]={1,0x18}, leftUp[]={2,0};
    assert(pad.parseReport(neutral,2,buttons,hat,axes));
    assert(buttons==0 && hat==-1 && axes[0]==127 && axes[1]==127);
    assert(pad.parseReport(right,2,buttons,hat,axes));
    assert(buttons==1 && axes[0]==255 && axes[1]==127);
    assert(pad.parseReport(leftUp,2,buttons,hat,axes));
    assert(buttons==2 && axes[0]==0 && axes[1]==0);
    assert(pad.parseReport(neutral,2,buttons,hat,axes));
    assert(buttons==0 && axes[0]==127 && axes[1]==127);
    const uint8_t mediaInput[]={1,0,0}, systemInput[]={2,0};
    assert(!media.parseReport(mediaInput,3,buttons,hat,axes));
    assert(!media.parseReport(systemInput,2,buttons,hat,axes));
    assert(!media.parseReport(neutral,2,buttons,hat,axes));
    const uint8_t shortReport[]={0};
    assert(!pad.parseReport(shortReport,1,buttons,hat,axes));
    assert(!pad.parseReport(nullptr,0,buttons,hat,axes));
    const uint8_t numbered[]={0x05,1,0x09,5,0xa1,1,0x85,1,
        0x05,9,0x19,1,0x29,2,0x15,0,0x25,1,0x75,1,0x95,2,0x81,2,0xc0};
    HIDInfo numberedPad;
    numberedPad.parseDesc(numbered,numbered+sizeof numbered);
    const uint8_t idOnly[]={1}, pressed[]={1,3};
    assert(!numberedPad.parseReport(idOnly,1,buttons,hat,axes));
    assert(numberedPad.parseReport(pressed,2,buttons,hat,axes) && buttons==3);
    HIDInfo mac;
    mac.parseDesc(macReportDescriptor,macReportDescriptor+sizeof macReportDescriptor);
    uint8_t macInput[64]={1,127,127,127,127,0x98,0xbc};
    assert(mac.parseReport(macInput,64,buttons,hat,axes));
    assert(buttons==0xbc9 && hat==-1);
    macInput[5]=8; macInput[6]=0; macInput[7]=0;
    assert(mac.parseReport(macInput,64,buttons,hat,axes));
    assert(buttons==0 && hat==-1 && axes[0]==127 && axes[1]==127);
    std::puts("HID report tests passed");
}
