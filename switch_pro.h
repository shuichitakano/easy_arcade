#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwitchPro
{
constexpr bool matches(uint16_t vid, uint16_t pid)
{
    return vid == 0x057e && pid == 0x2009;
}

struct Input
{
    uint32_t buttons = 0;
    int hat = -1;
    std::array<int, 9> analogs{128, 128, 128, 0, 0, 128, 0, 0, 0};
};

struct AxisCalibration
{
    int center = 2048;
    int below = 2048;
    int above = 2047;
    bool calibrated = false;
    int normalize(int value, bool inverted = false) const;
};

struct Calibration
{
    std::array<AxisCalibration, 4> axes{}; // LX, LY, RX, RY
    bool readStick(const uint8_t *bytes, size_t length, bool right);
};

// Only full input reports are accepted. USB/subcommand replies must never
// replace the current pad state, even though they share an input endpoint.
bool parseInput(const uint8_t *report, size_t length, Input &input,
                const Calibration &calibration = {});

struct Output
{
    std::array<uint8_t, 64> bytes{};
    uint16_t length = 0;
};

// Initializer ACKs alone do not prove that normal input has started.
class InputStartupWatch
{
public:
    void start(uint32_t now) { mountedAt_ = now; received_ = false; }
    void received(bool valid) { received_ |= valid; }
    bool expired(uint32_t now) const
    {
        return !received_ && uint32_t(now - mountedAt_) >= 10000;
    }
private:
    uint32_t mountedAt_ = 0;
    bool received_ = false;
};

// Survives interface unmount/remount so a failed device cannot reset forever.
class RecoveryBudget
{
public:
    bool ready(uint32_t now) const
    {
        return attempts_ < 2 && (!attempts_ || uint32_t(now - lastAttempt_) >= 3000);
    }
    void submitted(uint32_t now) { ++attempts_; lastAttempt_ = now; }
private:
    uint32_t lastAttempt_ = 0;
    uint8_t attempts_ = 0;
};

class Initializer
{
public:
    enum class State : uint8_t
    {
        Handshake, HandshakeAgain, EnableUsb,
        FactoryCalibration, UserCalibration, ReportMode, PlayerLights,
        Ready, Failed
    };

    bool nextOutput(uint32_t now, Output &output);
    void submitted(uint32_t now);
    void completed(uint16_t length);
    void received(const uint8_t *report, size_t length);
    State state() const { return state_; }
    const Calibration &calibration() const { return calibration_; }
    void setPlayerIndex(unsigned index) { playerLights_ = 1u << (index % 4); }

private:
    void advance(State state);
    void timedOut();
    Calibration calibration_;
    State state_ = State::Handshake;
    State sending_ = State::Handshake;
    uint32_t sentAt_ = 0;
    uint8_t attempts_ = 0;
    uint8_t sequence_ = 0;
    bool inFlight_ = false;
    uint8_t playerLights_ = 1;
};
}
