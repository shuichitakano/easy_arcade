#include "switch_pro.h"

#include <cassert>
#include <iostream>

using namespace SwitchPro;

namespace
{
// Captured from RB-SGA-026, including timer, battery and grip-status bytes.
constexpr std::array<uint8_t, 16> neutral{
    0x30, 0x03, 0x91, 0x00, 0x80, 0x00, 0x00, 0x08,
    0x80, 0x00, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00};

void testInput()
{
    Input input;
    assert(parseInput(neutral.data(), neutral.size(), input));
    assert(input.buttons == 0 && input.hat == -1);
    assert(input.analogs[0] == 128 && input.analogs[1] == 128);
    assert(input.analogs[2] == 128 && input.analogs[5] == 128);

    auto report = neutral;
    report[1] = 0xff; // Timer/battery changes are not button presses.
    report[2] = 0xff;
    assert(parseInput(report.data(), report.size(), input));
    assert(input.buttons == 0);

    report[3] = 0x84; // B + ZR
    report[4] = 0x92; // Plus + Home (and non-button grip bit)
    report[5] = 0x42; // L + Up
    assert(parseInput(report.data(), report.size(), input));
    assert(input.buttons == ((1u << 0) | (1u << 7) | (1u << 9) |
                             (1u << 12) | (1u << 4)));
    assert(input.hat == 0);

    // Every button has a distinct mapping; no timer/battery/reserved bits leak.
    report = neutral;
    report[3] = 0xcf;
    report[4] = 0x3f;
    report[5] = 0xc0;
    assert(parseInput(report.data(), report.size(), input));
    assert(input.buttons == 0x3fff);
    constexpr uint8_t dpad[] = {2, 6, 4, 5, 1, 9, 8, 10};
    for (int i = 0; i < 8; ++i)
    {
        report = neutral;
        report[5] = dpad[i];
        assert(parseInput(report.data(), report.size(), input));
        assert(input.hat == i && input.buttons == 0);
    }
    report[5] = 0x0f; // Opposing directions cancel.
    assert(parseInput(report.data(), report.size(), input) && input.hat == -1);

    report = neutral;
    report[6] = 0xff;
    report[7] = 0xff;
    report[8] = 0xff; // Left stick right/up
    report[9] = report[10] = report[11] = 0; // Right stick left/down
    assert(parseInput(report.data(), report.size(), input));
    assert(input.analogs[0] == 255 && input.analogs[1] == 0);
    assert(input.analogs[2] == 0 && input.analogs[5] == 255);

    const auto previous = input;
    for (size_t len = 0; len < 13; ++len)
        assert(!parseInput(report.data(), len, input));
    for (uint8_t id : {0x81, 0x21, 0x3f, 0x00})
    {
        report[0] = id;
        assert(!parseInput(report.data(), report.size(), input));
        assert(input.buttons == previous.buttons && input.hat == previous.hat &&
               input.analogs == previous.analogs);
    }
    assert(!parseInput(nullptr, 64, input));
    assert(matches(0x057e, 0x2009));
    assert(!matches(0x057e, 0x2006) && !matches(0x045e, 0x2009));
}

void testInitialization(bool ackFirst)
{
    Initializer init;
    Output output;
    const uint8_t handshake[] = {0x81, 0x02};
    assert(init.nextOutput(0, output));
    assert(output.length == 64 && output.bytes[0] == 0x80 && output.bytes[1] == 2);
    for (size_t i = 2; i < output.bytes.size(); ++i) assert(output.bytes[i] == 0);
    init.submitted(0);
    assert(!init.nextOutput(1, output));
    if (!ackFirst)
        init.completed(64);
    init.received(handshake, sizeof(handshake));
    assert(init.state() == Initializer::State::BaudRate);
    if (ackFirst)
    {
        assert(!init.nextOutput(2, output));
        init.completed(64); // Late handshake completion must not skip EnableUsb.
        assert(init.state() == Initializer::State::BaudRate);
    }
    assert(init.nextOutput(3, output));
    assert(output.length == 64 && output.bytes[1] == 3);
    init.submitted(3);
    init.completed(64);
    const uint8_t baud[] = {0x81, 0x03};
    init.received(baud, sizeof(baud));
    assert(init.nextOutput(4, output) && output.bytes[1] == 2);
    init.submitted(4);
    init.completed(64);
    init.received(handshake, sizeof(handshake));
    assert(init.nextOutput(5, output));
    assert(output.length == 64 && output.bytes[1] == 4);
    init.submitted(5);
    init.completed(64);
    // A clone may explicitly reject calibration reads; this is non-fatal.
    std::array<uint8_t, 15> nak{};
    nak[0] = 0x21;
    nak[14] = 0x10;
    for (int i = 0; i < 2; ++i)
    {
        assert(init.nextOutput(6 + i, output));
        assert(output.length == 64 && output.bytes[10] == 0x10);
        for (size_t n = 16; n < output.bytes.size(); ++n) assert(output.bytes[n] == 0);
        init.submitted(6 + i);
        init.completed(64);
        init.received(nak.data(), nak.size());
    }
    assert(init.nextOutput(8, output));
    assert(output.length == 64 && output.bytes[0] == 1 &&
           output.bytes[10] == 3 && output.bytes[11] == 0x30);
    for (size_t n = 12; n < output.bytes.size(); ++n) assert(output.bytes[n] == 0);
    init.submitted(8);
    init.completed(64);

    auto reply = neutral;
    reply[0] = 0x21;
    reply[13] = 0x80;
    reply[14] = 3;
    init.received(reply.data(), 14); // Truncated ACK
    assert(init.state() == Initializer::State::ReportMode);
    reply[14] = 0x30; // Unrelated subcommand
    init.received(reply.data(), reply.size());
    assert(init.state() == Initializer::State::ReportMode);
    reply[14] = 3;
    reply[13] = 0; // Negative ACK
    init.received(reply.data(), reply.size());
    assert(init.state() == Initializer::State::ReportMode);
    reply[13] = 0x80;
    init.received(reply.data(), reply.size());
    assert(init.state() == Initializer::State::PlayerLights);
    init.setPlayerIndex(2);
    assert(init.nextOutput(9, output));
    assert(output.bytes[10] == 0x30 && output.bytes[11] == 4);
    init.submitted(9);
    init.completed(64);
    reply[14] = 0x30;
    init.received(reply.data(), reply.size());
    assert(init.state() == Initializer::State::Ready);
    assert(!init.nextOutput(1000, output));
}

void testTimeoutsAndReconnect()
{
    Initializer init;
    Output output;
    // Time arithmetic must work across the millisecond counter wraparound.
    const uint32_t start = 0xffffff00;
    for (uint32_t retry = 0; retry < 3; ++retry)
    {
        const uint32_t now = start + retry * 500;
        assert(init.nextOutput(now, output));
        init.submitted(now);
        init.completed(64);
        assert(!init.nextOutput(now + 499, output));
    }
    assert(!init.nextOutput(start + 1500, output));
    assert(init.state() == Initializer::State::Failed);
    init = {}; // Unplug/replug or power cycle starts a new handshake.
    assert(init.nextOutput(2000, output) && output.bytes[1] == 2);
    init.submitted(2000);
    assert(!init.nextOutput(2500, output)); // OUT never completed
    assert(init.state() == Initializer::State::Failed);
    init.completed(64); // Late completion must not resurrect a failed session.
    assert(init.state() == Initializer::State::Failed);

    Initializer other;
    assert(other.nextOutput(2500, output)); // Separate device state is independent.
}

void pack(uint8_t *p, int x, int y)
{
    p[0] = x;
    p[1] = (x >> 8) | ((y & 15) << 4);
    p[2] = y >> 4;
}

void testCalibration()
{
    Calibration cal;
    std::array<uint8_t, 9> bytes{};
    // Asymmetric ranges, off-center sticks, different left/right layouts.
    pack(bytes.data(), 1200, 1000);
    pack(bytes.data() + 3, 1900, 2100);
    pack(bytes.data() + 6, 900, 1100);
    assert(cal.readStick(bytes.data(), bytes.size(), false));
    assert(cal.axes[0].normalize(1900) == 128);
    assert(cal.axes[0].normalize(1000) == 0);
    assert(cal.axes[0].normalize(3100) == 255);
    assert(cal.axes[1].normalize(2100, true) == 128);
    assert(cal.axes[1].normalize(1000, true) == 255);
    assert(cal.axes[1].normalize(3100, true) == 0);
    pack(bytes.data(), 2000, 2000);
    pack(bytes.data() + 3, 1000, 1000);
    pack(bytes.data() + 6, 1300, 1300);
    assert(cal.readStick(bytes.data(), bytes.size(), true));
    auto report = neutral;
    pack(report.data() + 6, 1900, 2100);
    pack(report.data() + 9, 2000, 2000);
    Input input;
    assert(parseInput(report.data(), report.size(), input, cal));
    assert(input.analogs[0] == 128 && input.analogs[1] == 128 &&
           input.analogs[2] == 128 && input.analogs[5] == 128);
    for (uint8_t fill : {0, 255})
    {
        bytes.fill(fill);
        assert(!cal.readStick(bytes.data(), bytes.size(), false));
        assert(cal.axes[0].center == 1900);
    }
    assert(!cal.readStick(bytes.data(), 8, true));
    assert(!cal.readStick(nullptr, 9, true));
}

void testSpiRepliesAndOptionalTimeouts()
{
    Initializer init;
    Output output;
    uint32_t now = 0;
    const uint8_t handshake[] = {0x81, 2};
    assert(init.nextOutput(now, output));
    init.submitted(now);
    init.completed(64);
    init.received(handshake, 2);
    // A clone with no baud/re-handshake support must still proceed.
    auto timeoutStage = [&] {
        for (int i = 0; i < 3; ++i)
        {
            assert(init.nextOutput(now, output));
            init.submitted(now);
            init.completed(output.length);
            now += 500;
        }
        assert(!init.nextOutput(now, output));
    };
    timeoutStage();
    assert(init.state() == Initializer::State::HandshakeAgain);
    timeoutStage();
    assert(init.state() == Initializer::State::EnableUsb);
    assert(init.nextOutput(now, output));
    init.submitted(now);
    init.completed(64);
    assert(init.nextOutput(now, output));
    assert(output.bytes[11] == 0x3d && output.bytes[12] == 0x60 && output.bytes[15] == 18);
    init.submitted(now);
    init.completed(64);
    std::array<uint8_t, 64> response{};
    response[0] = 0x21;
    response[13] = 0x90;
    response[14] = 0x10;
    response[15] = 0x3d;
    response[16] = 0x60;
    response[19] = 18;
    pack(response.data() + 20, 1200, 1100);
    pack(response.data() + 23, 1900, 2000);
    pack(response.data() + 26, 1000, 900);
    pack(response.data() + 29, 2100, 2200);
    pack(response.data() + 32, 900, 1000);
    pack(response.data() + 35, 1100, 1200);
    init.received(response.data(), 37); // Truncated payload
    assert(init.state() == Initializer::State::FactoryCalibration);
    response[15] = 0x3e;
    init.received(response.data(), response.size()); // Wrong address
    assert(init.state() == Initializer::State::FactoryCalibration);
    response[15] = 0x3d;
    init.received(response.data(), response.size());
    assert(init.calibration().axes[0].center == 1900);
    assert(init.calibration().axes[2].center == 2100);
    assert(init.nextOutput(now, output));
    assert(output.bytes[11] == 0x10 && output.bytes[12] == 0x80 && output.bytes[15] == 22);
    init.submitted(now);
    init.completed(64);
    response.fill(0);
    response[0] = 0x21;
    response[13] = 0x90;
    response[14] = 0x10;
    response[15] = 0x10;
    response[16] = 0x80;
    response[19] = 22;
    response[20] = 0xb2;
    response[21] = 0xa1;
    pack(response.data() + 22, 1000, 1000);
    pack(response.data() + 25, 2000, 2000);
    pack(response.data() + 28, 1000, 1000);
    // User left overrides factory left. Missing right magic preserves factory.
    init.received(response.data(), response.size());
    assert(init.calibration().axes[0].center == 2000);
    assert(init.calibration().axes[2].center == 2100);
    assert(init.state() == Initializer::State::ReportMode);
    assert(init.nextOutput(now, output));
    init.submitted(now);
    init.completed(64);
    response[14] = 3;
    init.received(response.data(), response.size());
    assert(init.state() == Initializer::State::PlayerLights);
    timeoutStage();
    assert(init.state() == Initializer::State::Ready);
    init = {};
    assert(!init.calibration().axes[0].calibrated);
}
}

int main()
{
    testInput();
    testInitialization(false);
    testInitialization(true);
    testTimeoutsAndReconnect();
    testCalibration();
    testSpiRepliesAndOptionalTimeouts();
    std::cout << "Switch Pro tests passed\n";
}
