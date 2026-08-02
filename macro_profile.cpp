#include "macro_profile.h"
#include "rapid_timing.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace Macro
{
namespace
{
constexpr size_t FILE_HEADER_SIZE = 16;
constexpr size_t FILE_MAGIC_SIZE = 4;
constexpr size_t TLV_HEADER_SIZE = 4;
constexpr size_t PROFILE_SETTINGS_SIZE = 2;
constexpr size_t MACRO_SETS_SIZE = 2;
constexpr size_t DIRECT_MAPPING_RECORD_SIZE = 3;
constexpr size_t RAPID_FIRE_RECORD_SIZE = 3;
constexpr size_t BINDING_RECORD_SIZE = 4;
constexpr size_t SEQUENCE_HEADER_SIZE = 4;
constexpr size_t SEQUENCE_STEP_SIZE = 5;
constexpr size_t SELECTOR_HEADER_SIZE = 10;
constexpr size_t METADATA_HEADER_SIZE = 10;
constexpr size_t MAX_SEQUENCES = 64;
constexpr size_t MAX_BINDINGS = 256;
constexpr size_t MAX_TOTAL_STEPS = 1024;
constexpr size_t MAX_SETS = 16;
constexpr size_t MAX_SELECTORS = 8;
constexpr size_t MAX_SELECTOR_STATES = 64;
constexpr uint8_t FORMAT_MAJOR_VERSION = 1;
constexpr uint8_t FORMAT_MINOR_VERSION = 0;
constexpr uint8_t METADATA_VERSION = 1;
constexpr uint8_t MIN_RAPID_DIVISOR = 2;
constexpr uint8_t MAX_RAPID_DIVISOR = 60;

enum class SectionType : uint8_t
{
    INVALID = 0x00,
    DIRECT_MAPPING = 0x01,
    SEQUENCE_BINDING = 0x02,
    SEQUENCE_DEFINITIONS = 0x03,
    STATE_SELECTORS = 0x04,
    RAPID_FIRE = 0x05,
    MACRO_SETS = 0x06,
    PROFILE_SETTINGS = 0x07,
    METADATA = 0x08,
    COUNT
};

enum class RapidType : uint8_t
{
    DISABLED = 0,
    SYNCHRONIZED = 1,
    FRONT = 2,
    BACK = 3,
};

enum BindingFlags : uint8_t
{
    BINDING_LOOP = 1u << 0,
    BINDING_CANCEL_ON_RELEASE = 1u << 1,
    BINDING_FLIP_HORIZONTAL = 1u << 2,
    BINDING_FLIP_VERTICAL = 1u << 3,
    BINDING_FLAGS_MASK = BINDING_LOOP | BINDING_CANCEL_ON_RELEASE |
                         BINDING_FLIP_HORIZONTAL | BINDING_FLIP_VERTICAL,
};

constexpr uint8_t RAPID_OVERRIDE = 1u << 0;
constexpr uint8_t TWO_PLAYER_OUTPUTS = 1u << 0;
constexpr uint8_t SELECTOR_WRAP = 1u << 0;

uint16_t read16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read24(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
}

uint32_t read32(const uint8_t *p)
{
    return read16(p) | (static_cast<uint32_t>(read16(p + 2)) << 16);
}

bool validUtf8(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size;)
    {
        const uint8_t c = data[i++];
        if (c < 0x80)
            continue;
        int continuation = 0;
        uint32_t codepoint = 0;
        uint32_t minimum = 0;
        if ((c & 0xe0) == 0xc0)
        {
            continuation = 1;
            codepoint = c & 0x1f;
            minimum = 0x80;
        }
        else if ((c & 0xf0) == 0xe0)
        {
            continuation = 2;
            codepoint = c & 0x0f;
            minimum = 0x800;
        }
        else if ((c & 0xf8) == 0xf0)
        {
            continuation = 3;
            codepoint = c & 0x07;
            minimum = 0x10000;
        }
        else
            return false;
        if (i + continuation > size)
            return false;
        while (continuation--)
        {
            const uint8_t next = data[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
    }
    return true;
}

struct Section
{
    const uint8_t *data = nullptr;
    size_t size = 0;

    explicit operator bool() const { return data != nullptr; }
};

class ByteReader
{
public:
    ByteReader(const uint8_t *data, size_t size) : cursor_(data), remaining_(size) {}

    bool readU8(uint8_t &value)
    {
        if (remaining_ < 1)
            return false;
        value = *cursor_++;
        --remaining_;
        return true;
    }

    bool readU16(uint16_t &value)
    {
        if (remaining_ < 2)
            return false;
        value = read16(cursor_);
        cursor_ += 2;
        remaining_ -= 2;
        return true;
    }

    bool readU24(uint32_t &value)
    {
        if (remaining_ < 3)
            return false;
        value = read24(cursor_);
        cursor_ += 3;
        remaining_ -= 3;
        return true;
    }

    bool readU32(uint32_t &value)
    {
        if (remaining_ < 4)
            return false;
        value = read32(cursor_);
        cursor_ += 4;
        remaining_ -= 4;
        return true;
    }

    bool readBytes(size_t size, const uint8_t *&data)
    {
        if (remaining_ < size)
            return false;
        data = cursor_;
        cursor_ += size;
        remaining_ -= size;
        return true;
    }

    size_t remaining() const { return remaining_; }

private:
    const uint8_t *cursor_;
    size_t remaining_;
};

struct SequenceHeader
{
    uint8_t id;
    uint8_t stepCount;
    uint8_t loopStart;
    uint8_t reserved;
};

struct SelectorHeader
{
    uint8_t id;
    uint8_t increment;
    uint8_t decrement;
    uint8_t minimum;
    uint8_t maximum;
    uint8_t initial;
    uint8_t flags;
    uint8_t neutralFrames;
    uint8_t stateCount;
    uint8_t reserved;
};

struct MetadataHeader
{
    uint8_t version;
    uint8_t flags;
    uint16_t profileNameLength;
    uint16_t descriptionLength;
    uint8_t sequenceNameCount;
    uint8_t setNameCount;
    uint8_t selectorNameCount;
    uint8_t reserved;
};

bool validOutput(uint32_t output, bool twoPlayer)
{
    return !(output & ~OUTPUT_MASK) && (twoPlayer || !(output & ~PLAYER_OUTPUT_MASK));
}

uint32_t swapBits(uint32_t value, int a, int b)
{
    const uint32_t ba = (value >> a) & 1;
    const uint32_t bb = (value >> b) & 1;
    if (ba != bb)
        value ^= (1u << a) | (1u << b);
    return value;
}
}

const char *resultText(Result result)
{
    switch (result)
    {
    case Result::OK: return "Done";
    case Result::TOO_LARGE: return "TooLarge";
    case Result::BAD_HEADER: return "Bad Head";
    case Result::BAD_CRC: return "Bad CRC";
    case Result::BAD_SECTION: return "Bad Sect";
    case Result::BAD_VALUE: return "BadValue";
    case Result::IO_ERROR: return "IO Error";
    case Result::NO_FILE: return "No File";
    case Result::NO_STORAGE: return "No USB";
    }
    return "Error";
}

uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    while (size--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

const Sequence *Profile::findSequence(uint8_t id) const
{
    for (const auto &sequence : sequences)
        if (sequence.id == id)
            return &sequence;
    return nullptr;
}

namespace
{
using SectionTable = std::array<Section, static_cast<size_t>(SectionType::COUNT)>;

const Section &getSection(const SectionTable &sections, SectionType type)
{
    return sections[static_cast<size_t>(type)];
}

Result parseHeader(const uint8_t *data, size_t size)
{
    if (!data || size < FILE_HEADER_SIZE)
        return Result::BAD_HEADER;
    if (size > MAX_FILE_SIZE)
        return Result::TOO_LARGE;

    ByteReader reader(data, FILE_HEADER_SIZE);
    const uint8_t *magic;
    uint8_t major;
    uint8_t minor;
    uint16_t headerSize;
    uint32_t totalSize;
    uint32_t payloadCrc;
    if (!reader.readBytes(FILE_MAGIC_SIZE, magic) || !reader.readU8(major) ||
        !reader.readU8(minor) ||
        !reader.readU16(headerSize) || !reader.readU32(totalSize) ||
        !reader.readU32(payloadCrc))
        return Result::BAD_HEADER;
    if (std::memcmp(magic, "AMAP", FILE_MAGIC_SIZE) || major != FORMAT_MAJOR_VERSION ||
        minor != FORMAT_MINOR_VERSION ||
        headerSize != FILE_HEADER_SIZE || totalSize != size)
        return Result::BAD_HEADER;
    if (payloadCrc != crc32(data + FILE_HEADER_SIZE, size - FILE_HEADER_SIZE))
        return Result::BAD_CRC;
    return Result::OK;
}

Result collectSections(const uint8_t *data, size_t size, SectionTable &sections)
{
    ByteReader reader(data, size);
    while (reader.remaining())
    {
        uint8_t type;
        uint8_t flags;
        uint16_t length;
        const uint8_t *payload;
        if (reader.remaining() < TLV_HEADER_SIZE || !reader.readU8(type) ||
            !reader.readU8(flags) || !reader.readU16(length) || flags ||
            !reader.readBytes(length, payload))
            return Result::BAD_SECTION;

        if (type < static_cast<uint8_t>(SectionType::COUNT))
        {
            if (type == static_cast<uint8_t>(SectionType::INVALID) || sections[type])
                return Result::BAD_SECTION;
            sections[type] = {payload, length};
        }
    }

    for (const SectionType required : {SectionType::DIRECT_MAPPING,
                                       SectionType::SEQUENCE_BINDING,
                                       SectionType::RAPID_FIRE,
                                       SectionType::MACRO_SETS,
                                       SectionType::PROFILE_SETTINGS})
        if (!getSection(sections, required))
            return Result::BAD_SECTION;
    return Result::OK;
}

Result parseSettings(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    uint8_t frameStep;
    uint8_t flags;
    if (section.size != PROFILE_SETTINGS_SIZE || !reader.readU8(frameStep) ||
        !reader.readU8(flags) ||
        !frameStep || (flags & ~TWO_PLAYER_OUTPUTS))
        return Result::BAD_VALUE;
    profile.frameStep = frameStep;
    profile.twoPlayerOutputs = flags & TWO_PLAYER_OUTPUTS;
    return Result::OK;
}

Result parseMacroSets(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    uint8_t count;
    uint8_t reserved;
    if (section.size != MACRO_SETS_SIZE || !reader.readU8(count) ||
        !reader.readU8(reserved) ||
        count < 1 || count > MAX_SETS || reserved)
        return Result::BAD_VALUE;
    profile.setCount = count;
    profile.setNames.resize(count);
    for (size_t i = 0; i < profile.setNames.size(); ++i)
        profile.setNames[i] = "Set " + std::to_string(i);
    return Result::OK;
}

Result parseDirectMappings(const Section &section, Profile &profile)
{
    if (section.size != LOGICAL_BUTTONS * DIRECT_MAPPING_RECORD_SIZE)
        return Result::BAD_SECTION;
    ByteReader reader(section.data, section.size);
    for (auto &mapping : profile.mappings)
    {
        if (!reader.readU24(mapping))
            return Result::BAD_SECTION;
        if (!validOutput(mapping, profile.twoPlayerOutputs))
            return Result::BAD_VALUE;
    }
    return Result::OK;
}

Result parseRapidFire(const Section &section, Profile &profile)
{
    if (section.size != LOGICAL_BUTTONS * RAPID_FIRE_RECORD_SIZE)
        return Result::BAD_SECTION;
    ByteReader reader(section.data, section.size);
    for (auto &rapid : profile.rapidFire)
    {
        uint8_t flags;
        uint8_t type;
        uint8_t divisor;
        if (!reader.readU8(flags) || !reader.readU8(type) || !reader.readU8(divisor))
            return Result::BAD_SECTION;
        if ((flags & ~RAPID_OVERRIDE) || type > static_cast<uint8_t>(RapidType::BACK) ||
            divisor < MIN_RAPID_DIVISOR || divisor > MAX_RAPID_DIVISOR)
            return Result::BAD_VALUE;
        rapid = {!!(flags & RAPID_OVERRIDE), type, divisor};
    }
    return Result::OK;
}

Result parseSequences(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    uint8_t sequenceCount;
    if (!reader.readU8(sequenceCount) || sequenceCount > MAX_SEQUENCES)
        return Result::BAD_VALUE;

    size_t totalSteps = 0;
    std::set<uint8_t> ids;
    for (size_t n = 0; n < sequenceCount; ++n)
    {
        SequenceHeader header;
        if (reader.remaining() < SEQUENCE_HEADER_SIZE || !reader.readU8(header.id) ||
            !reader.readU8(header.stepCount) || !reader.readU8(header.loopStart) ||
            !reader.readU8(header.reserved))
            return Result::BAD_SECTION;
        if (!header.stepCount || header.loopStart >= header.stepCount || header.reserved ||
            !ids.insert(header.id).second)
            return Result::BAD_VALUE;

        totalSteps += header.stepCount;
        if (totalSteps > MAX_TOTAL_STEPS ||
            reader.remaining() < static_cast<size_t>(header.stepCount) * SEQUENCE_STEP_SIZE)
            return Result::BAD_SECTION;

        Sequence sequence;
        sequence.id = header.id;
        sequence.loopStart = header.loopStart;
        sequence.steps.reserve(header.stepCount);
        for (size_t i = 0; i < header.stepCount; ++i)
        {
            Step step;
            if (!reader.readU24(step.output) || !reader.readU16(step.ticks))
                return Result::BAD_SECTION;
            if (!step.ticks || !validOutput(step.output, profile.twoPlayerOutputs))
                return Result::BAD_VALUE;
            sequence.steps.push_back(step);
        }
        sequence.name = "Macro " + std::to_string(sequence.id + 1);
        profile.sequences.push_back(std::move(sequence));
    }
    return reader.remaining() ? Result::BAD_SECTION : Result::OK;
}

Result parseBindings(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    uint16_t bindingCount;
    if (!reader.readU16(bindingCount) || bindingCount > MAX_BINDINGS ||
        reader.remaining() != static_cast<size_t>(bindingCount) * BINDING_RECORD_SIZE)
        return Result::BAD_SECTION;

    std::set<uint32_t> bindingKeys;
    for (size_t i = 0; i < bindingCount; ++i)
    {
        Binding binding;
        if (!reader.readU8(binding.logicalId) || !reader.readU8(binding.sequenceId) ||
            !reader.readU8(binding.setId) || !reader.readU8(binding.flags))
            return Result::BAD_SECTION;
        const uint32_t key = (static_cast<uint32_t>(binding.setId) << 16) |
                             (static_cast<uint32_t>(binding.logicalId) << 8) |
                             binding.sequenceId;
        if (binding.logicalId >= LOGICAL_BUTTONS || binding.setId >= profile.setCount ||
            (binding.flags & ~BINDING_FLAGS_MASK) ||
            !profile.findSequence(binding.sequenceId) || !bindingKeys.insert(key).second)
            return Result::BAD_VALUE;
        profile.bindings.push_back(binding);
    }
    return Result::OK;
}

Result parseSelectors(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    uint8_t selectorCount;
    if (!reader.readU8(selectorCount) || selectorCount > MAX_SELECTORS)
        return Result::BAD_VALUE;

    std::set<uint8_t> ids;
    for (size_t n = 0; n < selectorCount; ++n)
    {
        SelectorHeader header;
        if (reader.remaining() < SELECTOR_HEADER_SIZE || !reader.readU8(header.id) ||
            !reader.readU8(header.increment) || !reader.readU8(header.decrement) ||
            !reader.readU8(header.minimum) || !reader.readU8(header.maximum) ||
            !reader.readU8(header.initial) || !reader.readU8(header.flags) ||
            !reader.readU8(header.neutralFrames) || !reader.readU8(header.stateCount) ||
            !reader.readU8(header.reserved))
            return Result::BAD_SECTION;
        if (!ids.insert(header.id).second || header.increment >= LOGICAL_BUTTONS ||
            header.decrement >= LOGICAL_BUTTONS || header.increment == header.decrement ||
            header.maximum < header.minimum || header.initial < header.minimum ||
            header.initial > header.maximum || (header.flags & ~SELECTOR_WRAP) ||
            header.reserved || !header.stateCount || header.stateCount > MAX_SELECTOR_STATES ||
            header.stateCount != header.maximum - header.minimum + 1 ||
            reader.remaining() < static_cast<size_t>(header.stateCount) * DIRECT_MAPPING_RECORD_SIZE)
            return Result::BAD_VALUE;

        Selector selector;
        selector.id = header.id;
        selector.increment = header.increment;
        selector.decrement = header.decrement;
        selector.minimum = header.minimum;
        selector.maximum = header.maximum;
        selector.initial = header.initial;
        selector.wrap = header.flags & SELECTOR_WRAP;
        selector.neutralFrames = header.neutralFrames;
        selector.outputs.reserve(header.stateCount);
        for (size_t i = 0; i < header.stateCount; ++i)
        {
            uint32_t output;
            if (!reader.readU24(output))
                return Result::BAD_SECTION;
            if (!validOutput(output, profile.twoPlayerOutputs))
                return Result::BAD_VALUE;
            selector.outputs.push_back(output);
            selector.stateNames.push_back(std::to_string(selector.minimum + i));
        }
        selector.name = "Selector " + std::to_string(selector.id + 1);
        profile.selectors.push_back(std::move(selector));
    }
    return reader.remaining() ? Result::BAD_SECTION : Result::OK;
}

bool readUtf8String(ByteReader &reader, size_t length, std::string &destination)
{
    const uint8_t *data;
    if (!reader.readBytes(length, data) || !validUtf8(data, length))
        return false;
    destination.assign(reinterpret_cast<const char *>(data), length);
    return true;
}

Result parseMetadata(const Section &section, Profile &profile)
{
    ByteReader reader(section.data, section.size);
    MetadataHeader header;
    if (section.size < METADATA_HEADER_SIZE || !reader.readU8(header.version) ||
        !reader.readU8(header.flags) || !reader.readU16(header.profileNameLength) ||
        !reader.readU16(header.descriptionLength) || !reader.readU8(header.sequenceNameCount) ||
        !reader.readU8(header.setNameCount) || !reader.readU8(header.selectorNameCount) ||
        !reader.readU8(header.reserved) || header.version != METADATA_VERSION || header.flags ||
        header.reserved || header.sequenceNameCount != profile.sequences.size() ||
        header.setNameCount != profile.setCount ||
        header.selectorNameCount != profile.selectors.size())
        return Result::BAD_VALUE;

    if (!readUtf8String(reader, header.profileNameLength, profile.name) ||
        !readUtf8String(reader, header.descriptionLength, profile.description))
        return Result::BAD_VALUE;

    std::set<uint8_t> nameIds;
    for (size_t i = 0; i < header.sequenceNameCount; ++i)
    {
        uint8_t id;
        uint16_t length;
        if (reader.remaining() < 3 || !reader.readU8(id) || !reader.readU16(length))
            return Result::BAD_SECTION;
        auto it = std::find_if(profile.sequences.begin(), profile.sequences.end(),
                               [id](const Sequence &value) { return value.id == id; });
        if (it == profile.sequences.end() || !nameIds.insert(id).second ||
            !readUtf8String(reader, length, it->name))
            return Result::BAD_VALUE;
    }

    for (auto &name : profile.setNames)
    {
        uint16_t length;
        if (reader.remaining() < 2 || !reader.readU16(length))
            return Result::BAD_SECTION;
        if (!readUtf8String(reader, length, name))
            return Result::BAD_VALUE;
    }

    nameIds.clear();
    for (size_t i = 0; i < header.selectorNameCount; ++i)
    {
        uint8_t id;
        uint16_t nameLength;
        uint8_t stateCount;
        if (reader.remaining() < 4 || !reader.readU8(id) || !reader.readU16(nameLength) ||
            !reader.readU8(stateCount))
            return Result::BAD_SECTION;
        auto it = std::find_if(profile.selectors.begin(), profile.selectors.end(),
                               [id](const Selector &value) { return value.id == id; });
        if (it == profile.selectors.end() || !nameIds.insert(id).second ||
            stateCount != it->outputs.size() || !readUtf8String(reader, nameLength, it->name))
            return Result::BAD_VALUE;
        for (auto &name : it->stateNames)
        {
            uint16_t length;
            if (reader.remaining() < 2 || !reader.readU16(length))
                return Result::BAD_SECTION;
            if (!readUtf8String(reader, length, name))
                return Result::BAD_VALUE;
        }
    }
    return reader.remaining() ? Result::BAD_SECTION : Result::OK;
}
}

Result parse(const uint8_t *data, size_t size, Profile &out)
{
    Result result = parseHeader(data, size);
    if (result != Result::OK)
        return result;

    SectionTable sections{};
    result = collectSections(data + FILE_HEADER_SIZE, size - FILE_HEADER_SIZE, sections);
    if (result != Result::OK)
        return result;

    Profile profile;
    result = parseSettings(getSection(sections, SectionType::PROFILE_SETTINGS), profile);
    if (result != Result::OK)
        return result;
    result = parseMacroSets(getSection(sections, SectionType::MACRO_SETS), profile);
    if (result != Result::OK)
        return result;
    result = parseDirectMappings(getSection(sections, SectionType::DIRECT_MAPPING), profile);
    if (result != Result::OK)
        return result;
    result = parseRapidFire(getSection(sections, SectionType::RAPID_FIRE), profile);
    if (result != Result::OK)
        return result;

    if (const auto &section = getSection(sections, SectionType::SEQUENCE_DEFINITIONS))
        if ((result = parseSequences(section, profile)) != Result::OK)
            return result;
    if ((result = parseBindings(getSection(sections, SectionType::SEQUENCE_BINDING), profile)) !=
        Result::OK)
        return result;
    if (const auto &section = getSection(sections, SectionType::STATE_SELECTORS))
        if ((result = parseSelectors(section, profile)) != Result::OK)
            return result;
    if (const auto &section = getSection(sections, SectionType::METADATA))
        if ((result = parseMetadata(section, profile)) != Result::OK)
            return result;

    out = std::move(profile);
    return Result::OK;
}

void PlayerRuntime::attach(const Profile *profile)
{
    profile_ = profile;
    currentSet_ = 0;
    reset();
}

void PlayerRuntime::reset()
{
    previousRaw_ = 0;
    syncStartFrames_ = {};
    playbacks_.assign(profile_ ? profile_->bindings.size() : 0, {});
    selectorStates_.assign(profile_ ? profile_->selectors.size() : 0, {});
    if (profile_)
    {
        if (currentSet_ >= profile_->setCount)
            currentSet_ = 0;
        for (size_t i = 0; i < selectorStates_.size(); ++i)
            selectorStates_[i].value = profile_->selectors[i].initial;
    }
}

void PlayerRuntime::setCurrentSet(uint8_t set)
{
    if (profile_ && set < profile_->setCount)
        currentSet_ = set;
}

void PlayerRuntime::changeSet(int delta)
{
    if (!profile_ || !profile_->setCount)
        return;
    const int count = profile_->setCount;
    currentSet_ = static_cast<uint8_t>((currentSet_ + delta % count + count) % count);
}

uint32_t PlayerRuntime::transformOutput(uint32_t output, uint8_t flags) const
{
    for (int base : {0, 12})
    {
        if (flags & BINDING_FLIP_HORIZONTAL)
            output = swapBits(output, base + 4, base + 5);
        if (flags & BINDING_FLIP_VERTICAL)
            output = swapBits(output, base + 2, base + 3);
    }
    return output;
}

void PlayerRuntime::startPlayback(size_t bindingIndex)
{
    auto &playback = playbacks_[bindingIndex];
    if (playback.active)
        return;
    const auto &binding = profile_->bindings[bindingIndex];
    const auto *sequence = profile_->findSequence(binding.sequenceId);
    if (!sequence || sequence->steps.empty())
        return;
    playback.active = true;
    playback.step = 0;
    playback.ticksLeft = sequence->steps[0].ticks;
    playback.framesLeft = profile_->frameStep;
    playback.output = transformOutput(sequence->steps[0].output, binding.flags);
}

void PlayerRuntime::advancePlayback(size_t bindingIndex, uint32_t rawLogical)
{
    auto &playback = playbacks_[bindingIndex];
    if (!playback.active || --playback.framesLeft)
        return;
    playback.framesLeft = profile_->frameStep;
    if (--playback.ticksLeft)
        return;

    const auto &binding = profile_->bindings[bindingIndex];
    const auto *sequence = profile_->findSequence(binding.sequenceId);
    uint8_t next = playback.step + 1;
    if (next >= sequence->steps.size())
    {
        if ((binding.flags & BINDING_LOOP) && (rawLogical & (1u << binding.logicalId)))
            next = sequence->loopStart;
        else
        {
            playback.active = false;
            playback.output = 0;
            return;
        }
    }
    playback.step = next;
    playback.ticksLeft = sequence->steps[next].ticks;
    playback.output = transformOutput(sequence->steps[next].output, binding.flags);
}

uint32_t PlayerRuntime::processFrame(uint32_t raw, uint32_t inherited,
                                     uint32_t frameCounter)
{
    if (!profile_)
        return raw & PLAYER_OUTPUT_MASK;
    const uint32_t rising = raw & ~previousRaw_;

    for (size_t i = 0; i < playbacks_.size(); ++i)
    {
        const auto &binding = profile_->bindings[i];
        if (playbacks_[i].active && (binding.flags & BINDING_CANCEL_ON_RELEASE) &&
            !(raw & (1u << binding.logicalId)))
            playbacks_[i] = {};
    }

    for (size_t i = 0; i < profile_->selectors.size(); ++i)
    {
        const auto &selector = profile_->selectors[i];
        auto &state = selectorStates_[i];
        const bool increment = rising & (1u << selector.increment);
        const bool decrement = rising & (1u << selector.decrement);
        if (increment != decrement)
        {
            if (increment)
            {
                if (state.value < selector.maximum)
                    ++state.value;
                else if (selector.wrap)
                    state.value = selector.minimum;
            }
            else
            {
                if (state.value > selector.minimum)
                    --state.value;
                else if (selector.wrap)
                    state.value = selector.maximum;
            }
            state.neutralLeft = selector.neutralFrames;
        }
    }

    for (size_t i = 0; i < profile_->bindings.size(); ++i)
    {
        const auto &binding = profile_->bindings[i];
        if (binding.setId == currentSet_ && (rising & (1u << binding.logicalId)))
            startPlayback(i);
    }

    uint32_t postRapid = 0;
    for (size_t i = 0; i < LOGICAL_BUTTONS; ++i)
    {
        const auto &rapid = profile_->rapidFire[i];
        if (rapid.override && rapid.type == static_cast<uint8_t>(RapidType::SYNCHRONIZED) &&
            (rising & (1u << i)))
            syncStartFrames_[i] = frameCounter;
        bool on = inherited & (1u << i);
        if (rapid.override)
        {
            on = raw & (1u << i);
            if (on && rapid.type != static_cast<uint8_t>(RapidType::DISABLED))
            {
                if (rapid.type == static_cast<uint8_t>(RapidType::SYNCHRONIZED))
                    on = RapidTiming::synchronizedOn(frameCounter, syncStartFrames_[i], rapid.divisor);
                else
                    on = RapidTiming::globalPhaseOn(frameCounter, rapid.divisor,
                                                    rapid.type == static_cast<uint8_t>(RapidType::BACK));
            }
        }
        if (on)
            postRapid |= 1u << i;
    }

    uint32_t output = 0;
    for (size_t i = 0; i < LOGICAL_BUTTONS; ++i)
        if (postRapid & (1u << i))
            output |= profile_->mappings[i];
    for (const auto &playback : playbacks_)
        if (playback.active)
            output |= playback.output;
    for (size_t i = 0; i < profile_->selectors.size(); ++i)
    {
        const auto &selector = profile_->selectors[i];
        auto &state = selectorStates_[i];
        if (!state.neutralLeft)
            output |= selector.outputs[state.value - selector.minimum];
    }

    for (size_t i = 0; i < playbacks_.size(); ++i)
        advancePlayback(i, raw);
    for (auto &state : selectorStates_)
        if (state.neutralLeft)
            --state.neutralLeft;
    previousRaw_ = raw;
    return output;
}
}
