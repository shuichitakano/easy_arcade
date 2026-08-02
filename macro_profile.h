#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Macro
{
inline constexpr size_t MAX_FILE_SIZE = 8192;
inline constexpr size_t LOGICAL_BUTTONS = 32;
inline constexpr uint32_t OUTPUT_MASK = 0x00ffffff;
inline constexpr uint32_t PLAYER_OUTPUT_MASK = 0x00000fff;

enum class Result
{
    OK,
    TOO_LARGE,
    BAD_HEADER,
    BAD_CRC,
    BAD_SECTION,
    BAD_VALUE,
    IO_ERROR,
    NO_FILE,
    NO_STORAGE,
};

const char *resultText(Result result);

struct RapidFire
{
    bool override = false;
    uint8_t type = 0;
    uint8_t divisor = 2;
};

struct Step
{
    uint32_t output = 0;
    uint16_t ticks = 0;
};

struct Sequence
{
    uint8_t id = 0;
    uint8_t loopStart = 0;
    std::vector<Step> steps;
    std::string name;
};

struct Binding
{
    uint8_t logicalId = 0;
    uint8_t sequenceId = 0;
    uint8_t setId = 0;
    uint8_t flags = 0;
};

struct Selector
{
    uint8_t id = 0;
    uint8_t increment = 0;
    uint8_t decrement = 0;
    uint8_t minimum = 0;
    uint8_t maximum = 0;
    uint8_t initial = 0;
    bool wrap = false;
    uint8_t neutralFrames = 0;
    std::vector<uint32_t> outputs;
    std::string name;
    std::vector<std::string> stateNames;
};

struct Profile
{
    std::array<uint32_t, LOGICAL_BUTTONS> mappings{};
    std::array<RapidFire, LOGICAL_BUTTONS> rapidFire{};
    std::vector<Binding> bindings;
    std::vector<Sequence> sequences;
    std::vector<Selector> selectors;
    uint8_t setCount = 1;
    uint8_t frameStep = 1;
    bool twoPlayerOutputs = false;
    std::string name;
    std::string description;
    std::vector<std::string> setNames;

    const Sequence *findSequence(uint8_t id) const;
};

Result parse(const uint8_t *data, size_t size, Profile &profile);
uint32_t crc32(const uint8_t *data, size_t size);

// Runtime state is deliberately separate for each player. Profile definitions
// are shared, while active set, sequences and selectors are not.
class PlayerRuntime
{
public:
    void attach(const Profile *profile);
    void reset();
    uint8_t currentSet() const { return currentSet_; }
    void setCurrentSet(uint8_t set);
    void changeSet(int delta);

    // rawLogical and inheritedLogical use .eamacro logical bit numbering.
    // This method must be called once for each game VSync frame.
    uint32_t processFrame(uint32_t rawLogical, uint32_t inheritedLogical,
                          uint32_t frameCounter);

private:
    struct Playback
    {
        bool active = false;
        uint8_t step = 0;
        uint16_t ticksLeft = 0;
        uint8_t framesLeft = 0;
        uint32_t output = 0;
    };
    struct SelectorState
    {
        uint8_t value = 0;
        uint8_t neutralLeft = 0;
    };

    uint32_t transformOutput(uint32_t output, uint8_t flags) const;
    void startPlayback(size_t bindingIndex);
    void advancePlayback(size_t bindingIndex, uint32_t rawLogical);

    const Profile *profile_ = nullptr;
    std::vector<Playback> playbacks_;
    std::vector<SelectorState> selectorStates_;
    std::array<uint32_t, LOGICAL_BUTTONS> syncStartFrames_{};
    uint32_t previousRaw_ = 0;
    uint8_t currentSet_ = 0;
};
}
