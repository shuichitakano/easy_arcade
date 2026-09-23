#include "switch_pro.h"

#include <algorithm>

namespace SwitchPro
{
namespace
{
int unpackX(const uint8_t *p) { return p[0] | ((p[1] & 15) << 8); }
int unpackY(const uint8_t *p) { return (p[1] >> 4) | (p[2] << 4); }
}

int AxisCalibration::normalize(int value, bool inverted) const
{
    if (!calibrated)
        return inverted ? std::clamp((4096 - value) >> 4, 0, 255)
                        : std::clamp(value >> 4, 0, 255);
    int delta = value - center;
    // Keep the neutral value exactly 128 on either side of an inverted axis.
    const int range = delta < 0 ? below : above;
    if (inverted)
        delta = -delta;
    return std::clamp(128 + delta * (delta < 0 ? 128 : 127) / range, 0, 255);
}

bool Calibration::readStick(const uint8_t *bytes, size_t length, bool right)
{
    if (!bytes || length < 9)
        return false;
    const uint8_t *center = bytes + (right ? 0 : 3);
    const uint8_t *below = bytes + (right ? 3 : 6);
    const uint8_t *above = bytes + (right ? 6 : 0);
    AxisCalibration x{unpackX(center), unpackX(below), unpackX(above), true};
    AxisCalibration y{unpackY(center), unpackY(below), unpackY(above), true};
    auto valid = [](const AxisCalibration &a) {
        return a.below >= 128 && a.above >= 128 &&
               a.center >= a.below && a.center + a.above <= 4095;
    };
    // Erased/zero-filled or implausible clone responses must not destroy
    // usable factory/default calibration. Commit both axes atomically.
    if (!valid(x) || !valid(y))
        return false;
    axes[right ? 2 : 0] = x;
    axes[right ? 3 : 1] = y;
    return true;
}

// Protocol reference: Linux drivers/hid/hid-nintendo.c and
// dekuNukem/Nintendo_Switch_Reverse_Engineering/USB-HID-Notes.md.
// The HID descriptor describes a different layout from the actual 0x30 data.
bool parseInput(const uint8_t *report, size_t length, Input &input,
                const Calibration &calibration)
{
    if (!report || length < 13 || report[0] != 0x30)
        return false;

    Input result;
    // Logical button order: B, A, Y, X, L, R, ZL, ZR, Minus, Plus,
    // left-stick click, right-stick click, Home, Capture.
    constexpr uint8_t offsets[] = {3, 3, 3, 3, 5, 3, 5, 3, 4, 4, 4, 4, 4, 4};
    constexpr uint8_t bits[] =    {2, 3, 0, 1, 6, 6, 7, 7, 0, 1, 3, 2, 4, 5};
    for (size_t i = 0; i < sizeof(bits); ++i)
        if (report[offsets[i]] & (1u << bits[i]))
            result.buttons |= 1u << i;

    const int x = !!(report[5] & 0x04) - !!(report[5] & 0x08);
    const int y = !!(report[5] & 0x01) - !!(report[5] & 0x02);
    constexpr int hats[3][3] = {{7, 0, 1}, {6, -1, 2}, {5, 4, 3}};
    result.hat = hats[y + 1][x + 1];

    auto stick = [&](int offset, int axis) {
        const int sx = unpackX(report + offset);
        const int sy = unpackY(report + offset);
        result.analogs[axis] = calibration.axes[axis].normalize(sx);
        // Switch Y grows upwards; generic HID axes grow downwards.
        result.analogs[axis == 0 ? 1 : 5] = calibration.axes[axis + 1].normalize(sy, true);
    };
    stick(6, 0);
    stick(9, 2);
    input = result;
    return true;
}

void Initializer::advance(State state)
{
    state_ = state;
    attempts_ = 0;
}

void Initializer::timedOut()
{
    switch (state_)
    {
    case State::HandshakeAgain: advance(State::EnableUsb); break;
    case State::FactoryCalibration: advance(State::UserCalibration); break;
    case State::UserCalibration: advance(State::ReportMode); break;
    case State::PlayerLights: advance(State::Ready); break;
    default: advance(State::Failed); break;
    }
}

bool Initializer::nextOutput(uint32_t now, Output &output)
{
    if (state_ == State::Ready || state_ == State::Failed)
        return false;
    const bool expired = uint32_t(now - sentAt_) >= 500;
    if (inFlight_)
    {
        // Do not overwrite a buffer still owned by the USB controller.
        if (expired)
            state_ = State::Failed;
        return false;
    }
    if (attempts_ && !expired)
        return false;
    if (attempts_ >= 3)
    {
        timedOut();
        return false;
    }
    // USB output reports include the ID and zero padding to 64 bytes.
    // Short writes can disconnect compatible adapters during initialization.
    output = {};
    if (state_ == State::ReportMode || state_ == State::PlayerLights ||
        state_ == State::FactoryCalibration || state_ == State::UserCalibration)
    {
        output.bytes = {0x01, uint8_t(sequence_ & 0x0f),
                        0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40,
                        0x03, 0x30};
        output.length = 64;
        if (state_ == State::PlayerLights)
        {
            output.bytes[10] = 0x30;
            output.bytes[11] = playerLights_;
        }
        else if (state_ == State::FactoryCalibration || state_ == State::UserCalibration)
        {
            const uint32_t address = state_ == State::FactoryCalibration ? 0x603d : 0x8010;
            output.bytes[10] = 0x10; // SPI read only
            for (unsigned i = 0; i < 4; ++i)
                output.bytes[11 + i] = address >> (8 * i);
            output.bytes[15] = state_ == State::FactoryCalibration ? 18 : 22;
            output.length = 64;
        }
    }
    else
    {
        output.bytes[0] = 0x80;
        output.bytes[1] = state_ == State::EnableUsb ? 0x04 : 0x02;
        output.length = 64;
    }
    return true;
}

void Initializer::submitted(uint32_t now)
{
    sending_ = state_;
    sentAt_ = now;
    ++attempts_;
    ++sequence_;
    inFlight_ = true;
}

void Initializer::completed(uint16_t length)
{
    if (!inFlight_)
        return;
    inFlight_ = false;
    // 0x80 0x04 has no input acknowledgement. Advance on OUT completion.
    // An earlier handshake's OUT completion may arrive after its input ACK.
    if (sending_ == State::EnableUsb && state_ == State::EnableUsb && length == 64)
        advance(State::FactoryCalibration);
}

void Initializer::received(const uint8_t *report, size_t length)
{
    if (!report)
        return;
    if (!attempts_ || state_ == State::Failed || state_ == State::Ready)
        return;
    if (length >= 2 && report[0] == 0x81)
    {
        if (state_ == State::Handshake && report[1] == 0x02)
            // 80 03は8BitDo Arcade Stickの無線Switch接続が切れるため省略。手元の互換機は省略で動作確認済み。
            advance(State::HandshakeAgain);
        else if (state_ == State::HandshakeAgain && report[1] == 0x02)
            advance(State::EnableUsb);
        return;
    }
    if (length < 15 || report[0] != 0x21)
        return;
    const bool ack = report[13] & 0x80;
    if (state_ == State::ReportMode && report[14] == 0x03 && ack)
        advance(State::PlayerLights);
    else if (state_ == State::PlayerLights && report[14] == 0x30)
        advance(State::Ready); // LED support is optional.
    else if ((state_ == State::FactoryCalibration || state_ == State::UserCalibration) &&
             report[14] == 0x10)
    {
        if (!ack)
        {
            timedOut();
            return;
        }
        const bool factory = state_ == State::FactoryCalibration;
        const uint32_t expectedAddress = factory ? 0x603d : 0x8010;
        const uint8_t expectedLength = factory ? 18 : 22;
        if (length < size_t(20 + expectedLength))
            return;
        const uint32_t address = uint32_t(report[15]) | (uint32_t(report[16]) << 8) |
                                 (uint32_t(report[17]) << 16) | (uint32_t(report[18]) << 24);
        if (address != expectedAddress || report[19] != expectedLength)
            return; // Ignore stale replies to another SPI read.
        const uint8_t *data = report + 20;
        if (factory)
        {
            calibration_.readStick(data, 9, false);
            calibration_.readStick(data + 9, 9, true);
        }
        else
        {
            if (data[0] == 0xb2 && data[1] == 0xa1)
                calibration_.readStick(data + 2, 9, false);
            if (data[11] == 0xb2 && data[12] == 0xa1)
                calibration_.readStick(data + 13, 9, true);
        }
        advance(factory ? State::UserCalibration : State::ReportMode);
    }
}
}
