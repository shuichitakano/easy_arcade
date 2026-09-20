#include "hid_info.h"
#include "fixtures/8bitdo_mac_report_descriptor.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <initializer_list>

namespace
{
struct Input
{
    bool valid;
    uint32_t buttons;
    int hat;
    std::array<int, HIDInfo::N_ANALOGS> axes;
};
Input decode(std::initializer_list<uint8_t> items, std::initializer_list<uint8_t> bytes)
{
    std::vector<uint8_t> descriptor{5, 1, 9, 5, 0xa1, 1}; // Gamepad application
    descriptor.insert(descriptor.end(), items);
    descriptor.push_back(0xc0);
    std::vector<uint8_t> report(bytes);
    HIDInfo info;
    info.parseDesc(descriptor.data(), descriptor.data() + descriptor.size());
    Input result{};
    result.valid = info.parseReport(report.data(), report.size(), result.buttons, result.hat, result.axes);
    return result;
}
void testDescriptorSemantics()
{
    // Unsigned maxima encoded in one and two bytes.
    auto r = decode({5,1,9,0x30,0x15,0,0x25,255,0x75,8,0x95,1,0x81,2}, {255});
    assert(r.valid && r.axes[0] == 255);
    r = decode({5,1,9,0x30,0x15,0,0x26,255,255,0x75,16,0x95,1,0x81,2}, {255,255});
    assert(r.valid && r.axes[0] == 255);
    r = decode({5,1,9,0x30,0x85,128,0x15,0,0x25,255,0x75,8,0x95,1,0x81,2}, {128,255});
    assert(r.valid && r.axes[0] == 255);
    // 128 one-bit fields with the last usage repeated. Must not resize to -128.
    r = decode({5,1,9,0x30,0x15,0,0x25,1,0x75,1,0x95,128,0x81,2},
               {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128});
    assert(r.valid && r.axes[0] == 255);

    // Output and Feature fields must not advance the Input bit cursor.
    r = decode({5,1,0x15,0,0x25,255,0x75,8,0x95,1,
                9,0x30,0x81,2,9,0x32,0x91,2,9,0x33,0xb1,2,9,0x31,0x81,2}, {0,255});
    assert(r.valid && r.axes[0] == 0 && r.axes[1] == 255);
    r = decode({5,1,0x15,0,0x25,255,0x75,8,0x95,1,
                0x85,1,9,0x30,0x81,2,0x85,2,9,0x32,0x81,2,
                0x85,1,9,0x31,0x81,2}, {1,0,255});
    assert(r.valid && r.axes[0] == 0 && r.axes[1] == 255);
    // POP restores globals (including ID), not a report cursor or local usages.
    r = decode({5,1,0x15,0,0x25,255,0x75,8,0x95,1,0x85,1,9,0x30,0x81,2,
                0xa4,0x85,2,9,0x32,0x81,2,9,0x31,0xb4,0x81,2}, {1,0,255});
    assert(r.valid && r.axes[1] == 255);

    // Array selectors: two simultaneous buttons and null/undefined selections.
    r = decode({5,9,0x19,1,0x29,3,0x15,1,0x25,3,0x75,8,0x95,2,0x81,0}, {2,3});
    assert(r.valid && r.buttons == 6);
    r = decode({5,9,0x19,0,0x29,3,0x15,0,0x25,3,0x75,8,0x95,2,0x81,0}, {0,3});
    assert(r.valid && r.buttons == 4);
    r = decode({5,9,9,4,9,8,0x15,1,0x25,2,0x75,8,0x95,2,0x81,0x40}, {2,0});
    assert(r.valid && r.buttons == 128);
    // Multi-bit Variable button: all value bits matter, including button 32.
    r = decode({5,9,9,32,0x15,0,0x25,3,0x75,2,0x95,1,0x81,2}, {2});
    assert(r.valid && r.buttons == 0x80000000u);

    for (uint8_t value = 0; value <= 9; ++value)
    {
        r = decode({5,1,9,0x39,0x15,1,0x25,8,0x75,4,0x95,1,0x81,0x42}, {value});
        assert(r.valid && r.hat == (value >= 1 && value <= 8 ? value - 1 : -1));
    }
    r = decode({5,1,9,0x39,0x15,0,0x25,3,0x75,4,0x95,1,0x81,0x42}, {3});
    assert(r.valid && r.hat == 6);
    r = decode({5,9,0x19,1,0x29,1,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2}, {1});
    assert(r.valid && r.buttons == 1);
    r = decode({5,9,0x0b,0x30,0,1,0,0x15,0,0x25,255,0x75,8,0x95,1,0x81,2}, {255});
    assert(r.valid && r.axes[0] == 255);
    r = decode({5,1,0x1b,1,0,9,0,0x2b,1,0,9,0,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2}, {1});
    assert(r.valid && r.buttons == 1);

    r = decode({5,1,9,0x30,0x15,0,0x27,255,255,255,0,0x75,24,0x95,1,0x81,2}, {255,255,255});
    assert(r.valid && r.axes[0] == 255);
    // 32-bit axis crosses five bytes and exercises the highest extraction bit.
    r = decode({0x75,7,0x95,1,0x81,1,5,1,9,0x30,0x15,0,
                0x27,255,255,255,255,0x75,32,0x81,2}, {128,255,255,255,127});
    assert(r.valid && r.axes[0] == 255);
    r = decode({5,1,9,0x30,0x17,0,0,0,128,0x27,255,255,255,127,0x75,32,0x95,1,0x81,2}, {0,0,0,128});
    assert(r.valid && r.axes[0] == 0);
    r = decode({5,1,9,0x30,0x15,128,0x25,127,0x75,8,0x95,1,0x81,2}, {0});
    assert(r.valid && r.axes[0] == 128);
    // Buffered Bytes does not multiply Report Size by eight.
    r = decode({5,1,9,0x30,0x15,0,0x25,255,0x75,8,0x95,1,0x82,2,1}, {255});
    assert(r.valid && r.axes[0] == 255);
    // Large vendor usage ranges are irrelevant, but still occupy report bits.
    r = decode({6,0,255,0x19,0,0x2a,255,255,0x15,0,0x26,255,255,
                0x75,16,0x95,1,0x81,0,5,9,9,1,0x25,1,0x75,1,0x81,2}, {0,0,1});
    assert(r.valid && r.buttons == 1);
}
void testMalformedDescriptors()
{
    const uint8_t descriptor[]={5,1,9,5,0xa1,1,9,0x30,0x15,0,0x25,255,0x75,8,0x95,1,0x81,2,0xc0};
    HIDInfo info;
    uint8_t byte=255;
    uint32_t buttons;
    int hat;
    std::array<int,HIDInfo::N_ANALOGS> axes;
    for (size_t length=0; length<sizeof descriptor; ++length)
    {
        info.parseDesc(descriptor,descriptor+sizeof descriptor);
        info.parseDesc(descriptor,descriptor+length);
        assert(!info.parseReport(&byte,1,buttons,hat,axes));
    }
    assert(!decode({5,1,9,0x30,0x75,8,0x97,255,255,255,255,0x81,2},{0}).valid);
    assert(!decode({0xb4},{0}).valid); // POP without PUSH
    assert(!decode({0x85,0,5,9,9,1,0x75,1,0x95,1,0x81,2},{1}).valid);
    // The whole report length includes trailing constant bits.
    assert(!decode({5,9,9,1,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2,
                    0x75,8,0x81,1},{1}).valid);
    assert(!decode({5,1,9,0x30,0x15,0,0x25,255,0x75,8,0x95,1,0x81,2,0x26},{255}).valid);
}
}

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
    testDescriptorSemantics();
    testMalformedDescriptors();
    std::puts("HID report tests passed");
}
