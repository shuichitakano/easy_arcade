#include "config_json.h"

#include "pad_manager.h"
#include "pad_state.h"
#include "usb_storage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include <hardware/watchdog.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/reader.h>

extern "C"
{
#include <ff.h>
}

namespace
{
    constexpr char CONFIG_TEMP_PATH[] = "0:/ea2_config.tmp";
    constexpr char CONFIG_BACKUP_PATH[] = "0:/ea2_config.bak";
    constexpr FSIZE_t MAX_JSON_FILE_SIZE = 256 * 1024;
    constexpr size_t MAX_PAD_CONFIGS = 128;
    constexpr size_t MAX_BUTTON_UNITS = PadState::MAX_BUTTONS;
    constexpr size_t MAX_ANALOG_UNITS = N_PAD_CONFIG_ANALOGS;
    constexpr size_t IO_BUFFER_SIZE = 256;
    // The schema only contains short identifiers and one short format string.
    // Stop RapidJSON before it grows its token stack for a hostile long string.
    constexpr size_t MAX_JSON_STRING_BYTES = 64;
    constexpr unsigned JSON_FORMAT_VERSION = 1;

    class FatFsInputStream
    {
    public:
        using Ch = char;

        explicit FatFsInputStream(FIL &file) : file_(file) {}

        Ch Peek() const
        {
            if (limitExceeded_)
                return '\0';
            fill();
            return position_ < size_ ? buffer_[position_] : '\0';
        }

        Ch Take()
        {
            auto ch = Peek();
            if (position_ < size_)
            {
                ++position_;
                ++offset_;
                trackString(ch);
            }
            return ch;
        }

        size_t Tell() const { return offset_; }

        Ch *PutBegin() { return nullptr; }
        void Put(Ch) {}
        void Flush() {}
        size_t PutEnd(Ch *) { return 0; }

        bool ok() const { return ok_; }
        bool limitExceeded() const { return limitExceeded_; }

    private:
        __attribute__((noinline)) void trackString(Ch ch)
        {
            if (!inString_)
            {
                if (ch == '"')
                {
                    inString_ = true;
                    stringBytes_ = 0;
                }
                return;
            }

            if (escaped_)
            {
                escaped_ = false;
            }
            else if (ch == '\\')
            {
                escaped_ = true;
            }
            else if (ch == '"')
            {
                inString_ = false;
                return;
            }

            if (++stringBytes_ > MAX_JSON_STRING_BYTES)
                limitExceeded_ = true;
        }

        void fill() const
        {
            if (position_ < size_ || eof_ || !ok_)
            {
                return;
            }

            UINT read = 0;
            const auto result = f_read(&file_, buffer_.data(), buffer_.size(), &read);
            ok_ = result == FR_OK;
            position_ = 0;
            size_ = read;
            eof_ = !ok_ || read == 0;
            watchdog_update();
        }

        FIL &file_;
        mutable std::array<char, IO_BUFFER_SIZE> buffer_{};
        mutable size_t position_ = 0;
        mutable size_t size_ = 0;
        mutable size_t offset_ = 0;
        mutable bool eof_ = false;
        mutable bool ok_ = true;
        bool inString_ = false;
        bool escaped_ = false;
        size_t stringBytes_ = 0;
        bool limitExceeded_ = false;
    };

    class FatFsOutputStream
    {
    public:
        using Ch = char;

        explicit FatFsOutputStream(FIL &file) : file_(file) {}
        ~FatFsOutputStream() { Flush(); }

        void Put(Ch ch)
        {
            if (!ok_)
            {
                return;
            }
            buffer_[size_++] = ch;
            if (size_ == buffer_.size())
            {
                Flush();
            }
        }

        void Flush()
        {
            if (!ok_ || size_ == 0)
            {
                return;
            }
            UINT written = 0;
            const auto result = f_write(&file_, buffer_.data(), size_, &written);
            ok_ = result == FR_OK && written == size_;
            size_ = 0;
            watchdog_update();
        }

        bool ok() const { return ok_; }

    private:
        FIL &file_;
        std::array<char, IO_BUFFER_SIZE> buffer_{};
        size_t size_ = 0;
        bool ok_ = true;
    };

    enum class Context
    {
        ROOT,
        APP,
        RAPID_PHASE,
        RAPID_SETTINGS,
        RAPID_SETTING,
        ROT_ENC_LIST,
        ROT_ENC,
        ANALOG_SETTINGS,
        ANALOG_SETTING,
        PADS,
        PAD,
        BUTTONS,
        ANALOGS,
        UNIT,
    };

    enum class Field
    {
        NONE,
        FORMAT,
        VERSION,
        APP,
        PADS,
        INIT_POWER_ON,
        DISP_FPS,
        BUTTON_DISP_MODE,
        BACK_LIGHT,
        RAPID_MODE_SYNCHRO,
        SYNCHRO_FETCH_PHASE,
        SOFTWARE_RAPID_SPEED,
        RAPID_PHASE,
        RAPID_SETTINGS,
        MASK,
        DIV,
        ROT_ENC,
        REVERSE,
        SCALE,
        AXIS,
        TWIN_PORT_MODE,
        ANALOG_MODE,
        ANALOG_SETTINGS,
        SENSITIVITY,
        OFFSET,
        VID,
        PID,
        OUT_PORT_OFFSET,
        BUTTONS,
        ANALOGS,
        TYPE,
        NUMBER,
        ANALOG_ON,
        ANALOG_OFF,
        HAT_POS,
        INDEX,
        SUB_INDEX,
        IN_PORT_OFFSET,
        UNKNOWN,
    };

    template <size_t N>
    bool equals(const char *value, size_t length, const char (&expected)[N])
    {
        return length == N - 1 && std::memcmp(value, expected, N - 1) == 0;
    }

    Field parseKey(const char *value, size_t length)
    {
#define CONFIG_KEY(name, valueText) \
        if (equals(value, length, valueText)) return Field::name
        CONFIG_KEY(FORMAT, "format");
        CONFIG_KEY(VERSION, "version");
        CONFIG_KEY(APP, "app");
        CONFIG_KEY(PADS, "pads");
        CONFIG_KEY(INIT_POWER_ON, "initPowerOn");
        CONFIG_KEY(DISP_FPS, "dispFPS");
        CONFIG_KEY(BUTTON_DISP_MODE, "buttonDispMode");
        CONFIG_KEY(BACK_LIGHT, "backLight");
        CONFIG_KEY(RAPID_MODE_SYNCHRO, "rapidModeSynchro");
        CONFIG_KEY(SYNCHRO_FETCH_PHASE, "synchroFetchPhase");
        CONFIG_KEY(SOFTWARE_RAPID_SPEED, "softwareRapidSpeed");
        CONFIG_KEY(RAPID_PHASE, "rapidPhase");
        CONFIG_KEY(RAPID_SETTINGS, "rapidSettings");
        CONFIG_KEY(MASK, "mask");
        CONFIG_KEY(DIV, "div");
        CONFIG_KEY(ROT_ENC, "rotEnc");
        CONFIG_KEY(REVERSE, "reverse");
        CONFIG_KEY(SCALE, "scale");
        CONFIG_KEY(AXIS, "axis");
        CONFIG_KEY(TWIN_PORT_MODE, "twinPortMode");
        CONFIG_KEY(ANALOG_MODE, "analogMode");
        CONFIG_KEY(ANALOG_SETTINGS, "analogSettings");
        CONFIG_KEY(SENSITIVITY, "sensitivity");
        CONFIG_KEY(OFFSET, "offset");
        CONFIG_KEY(VID, "vid");
        CONFIG_KEY(PID, "pid");
        CONFIG_KEY(OUT_PORT_OFFSET, "outPortOffset");
        CONFIG_KEY(BUTTONS, "buttons");
        CONFIG_KEY(ANALOGS, "analogs");
        CONFIG_KEY(TYPE, "type");
        CONFIG_KEY(NUMBER, "number");
        CONFIG_KEY(ANALOG_ON, "analogOn");
        CONFIG_KEY(ANALOG_OFF, "analogOff");
        CONFIG_KEY(HAT_POS, "hatPos");
        CONFIG_KEY(INDEX, "index");
        CONFIG_KEY(SUB_INDEX, "subIndex");
        CONFIG_KEY(IN_PORT_OFFSET, "inPortOffset");
#undef CONFIG_KEY
        return Field::UNKNOWN;
    }

    class ConfigHandler
        : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ConfigHandler>
    {
    public:
        const AppConfig &appConfig() const { return appConfig_; }
        std::vector<PadConfig> &&takePadConfigs() { return std::move(padConfigs_); }
        ConfigJsonResult result() const { return result_; }
        bool complete() const { return complete_; }

        bool Null() { return fail(ConfigJsonResult::INVALID_VALUE); }
        bool Bool(bool value) { return number(value ? 1 : 0); }
        bool Int(int value) { return number(value); }
        bool Uint(unsigned value) { return number(value); }
        bool Int64(int64_t value) { return number(value); }
        bool Uint64(uint64_t value)
        {
            if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            {
                return fail(ConfigJsonResult::INVALID_VALUE);
            }
            return number(static_cast<int64_t>(value));
        }
        bool Double(double) { return fail(ConfigJsonResult::INVALID_VALUE); }
        bool RawNumber(const char *, rapidjson::SizeType, bool)
        {
            return fail(ConfigJsonResult::INVALID_VALUE);
        }

        bool String(const char *value, rapidjson::SizeType length, bool)
        {
            if (current() == Context::ROOT && key_ == Field::FORMAT &&
                equals(value, length, "easy-arcade-config"))
            {
                key_ = Field::NONE;
                return mark(rootMask_, 1u << 0);
            }
            return fail(ConfigJsonResult::INVALID_FORMAT);
        }

        bool Key(const char *value, rapidjson::SizeType length, bool)
        {
            key_ = parseKey(value, length);
            return key_ != Field::UNKNOWN || fail(ConfigJsonResult::INVALID_FORMAT);
        }

        bool StartObject()
        {
            if (depth_ == 0)
            {
                return push(Context::ROOT);
            }

            switch (current())
            {
            case Context::ROOT:
                if (key_ == Field::APP && mark(rootMask_, 1u << 2))
                {
                    key_ = Field::NONE;
                    return push(Context::APP);
                }
                break;
            case Context::RAPID_SETTINGS:
                rapidSettingMask_ = 0;
                return push(Context::RAPID_SETTING);
            case Context::ROT_ENC_LIST:
                rotEncMask_ = 0;
                return push(Context::ROT_ENC);
            case Context::ANALOG_SETTINGS:
                analogSettingMask_ = 0;
                return push(Context::ANALOG_SETTING);
            case Context::PADS:
                if (padConfigs_.size() >= MAX_PAD_CONFIGS)
                {
                    return fail(ConfigJsonResult::INVALID_VALUE);
                }
                padMask_ = 0;
                padButtons_.clear();
                padAnalogs_.clear();
                padVid_ = padPid_ = padOutPortOffset_ = 0;
                return push(Context::PAD);
            case Context::BUTTONS:
            case Context::ANALOGS:
                unitIsAnalog_ = current() == Context::ANALOGS;
                unitMask_ = 0;
                unit_ = {};
                return push(Context::UNIT);
            default:
                break;
            }
            return fail(ConfigJsonResult::INVALID_FORMAT);
        }

        bool EndObject(rapidjson::SizeType)
        {
            switch (current())
            {
            case Context::ROOT:
                if (rootMask_ != ROOT_REQUIRED)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                complete_ = true;
                break;
            case Context::APP:
                if (appMask_ != APP_REQUIRED)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                break;
            case Context::RAPID_SETTING:
                if (rapidSettingMask_ != RAPID_SETTING_REQUIRED || rapidSettingIndex_ >= 2)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                ++rapidSettingIndex_;
                break;
            case Context::ROT_ENC:
                if (rotEncMask_ != ROT_ENC_REQUIRED || rotEncIndex_ >= appConfig_.rotEnc.size())
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                ++rotEncIndex_;
                break;
            case Context::ANALOG_SETTING:
                if (analogSettingMask_ != ANALOG_SETTING_REQUIRED ||
                    analogSettingIndex_ >= AppConfig::ANALOG_MAX)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                ++analogSettingIndex_;
                break;
            case Context::PAD:
                if (padMask_ != PAD_REQUIRED)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                for (const auto &config : padConfigs_)
                {
                    if (config.getDeviceID() == PadConfig::DeviceID{
                                                    static_cast<uint16_t>(padVid_),
                                                    static_cast<uint16_t>(padPid_),
                                                    static_cast<uint8_t>(padOutPortOffset_)})
                        return fail(ConfigJsonResult::INVALID_VALUE);
                }
                padConfigs_.emplace_back(padVid_, padPid_, padOutPortOffset_,
                                         padButtons_, padAnalogs_);
                break;
            case Context::UNIT:
                if (unitMask_ != UNIT_REQUIRED || !validateUnit(unit_, unitIsAnalog_))
                    return fail(ConfigJsonResult::INVALID_VALUE);
                if (unitIsAnalog_)
                {
                    if (padAnalogs_.size() >= MAX_ANALOG_UNITS)
                        return fail(ConfigJsonResult::INVALID_VALUE);
                    padAnalogs_.push_back(unit_);
                }
                else
                {
                    if (padButtons_.size() >= MAX_BUTTON_UNITS)
                        return fail(ConfigJsonResult::INVALID_VALUE);
                    padButtons_.push_back(unit_);
                }
                break;
            default:
                return fail(ConfigJsonResult::INVALID_FORMAT);
            }
            return pop();
        }

        bool StartArray()
        {
            if (current() == Context::ROOT && key_ == Field::PADS && mark(rootMask_, 1u << 3))
            {
                key_ = Field::NONE;
                return push(Context::PADS);
            }
            if (current() == Context::APP)
            {
                if (key_ == Field::RAPID_PHASE && mark(appMask_, 1u << 8))
                {
                    rapidPhaseIndex_ = 0;
                    key_ = Field::NONE;
                    return push(Context::RAPID_PHASE);
                }
                if (key_ == Field::RAPID_SETTINGS && mark(appMask_, 1u << 9))
                {
                    rapidSettingIndex_ = 0;
                    key_ = Field::NONE;
                    return push(Context::RAPID_SETTINGS);
                }
                if (key_ == Field::ROT_ENC && mark(appMask_, 1u << 10))
                {
                    rotEncIndex_ = 0;
                    key_ = Field::NONE;
                    return push(Context::ROT_ENC_LIST);
                }
                if (key_ == Field::ANALOG_SETTINGS && mark(appMask_, 1u << 13))
                {
                    analogSettingIndex_ = 0;
                    key_ = Field::NONE;
                    return push(Context::ANALOG_SETTINGS);
                }
            }
            if (current() == Context::PAD && key_ == Field::BUTTONS && mark(padMask_, 1u << 3))
            {
                key_ = Field::NONE;
                return push(Context::BUTTONS);
            }
            if (current() == Context::PAD && key_ == Field::ANALOGS && mark(padMask_, 1u << 4))
            {
                key_ = Field::NONE;
                return push(Context::ANALOGS);
            }
            return fail(ConfigJsonResult::INVALID_FORMAT);
        }

        bool EndArray(rapidjson::SizeType)
        {
            switch (current())
            {
            case Context::RAPID_PHASE:
                if (rapidPhaseIndex_ != appConfig_.rapidPhase.size())
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                break;
            case Context::RAPID_SETTINGS:
                if (rapidSettingIndex_ != 2)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                break;
            case Context::ROT_ENC_LIST:
                if (rotEncIndex_ != appConfig_.rotEnc.size())
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                break;
            case Context::ANALOG_SETTINGS:
                if (analogSettingIndex_ != AppConfig::ANALOG_MAX)
                    return fail(ConfigJsonResult::INVALID_FORMAT);
                break;
            case Context::PADS:
            case Context::BUTTONS:
            case Context::ANALOGS:
                break;
            default:
                return fail(ConfigJsonResult::INVALID_FORMAT);
            }
            return pop();
        }

    private:
        static constexpr uint32_t ROOT_REQUIRED = (1u << 4) - 1;
        static constexpr uint32_t APP_REQUIRED = (1u << 14) - 1;
        static constexpr uint32_t RAPID_SETTING_REQUIRED = (1u << 2) - 1;
        static constexpr uint32_t ROT_ENC_REQUIRED = (1u << 3) - 1;
        static constexpr uint32_t ANALOG_SETTING_REQUIRED = (1u << 3) - 1;
        static constexpr uint32_t PAD_REQUIRED = (1u << 5) - 1;
        static constexpr uint32_t UNIT_REQUIRED = (1u << 8) - 1;

        bool fail(ConfigJsonResult result)
        {
            result_ = result;
            return false;
        }

        bool mark(uint32_t &mask, uint32_t bit)
        {
            if (mask & bit)
                return fail(ConfigJsonResult::INVALID_FORMAT);
            mask |= bit;
            return true;
        }

        bool push(Context context)
        {
            if (depth_ >= contexts_.size())
                return fail(ConfigJsonResult::INVALID_FORMAT);
            contexts_[depth_++] = context;
            return true;
        }

        bool pop()
        {
            if (depth_ == 0)
                return fail(ConfigJsonResult::INVALID_FORMAT);
            --depth_;
            key_ = Field::NONE;
            return true;
        }

        Context current() const
        {
            return depth_ ? contexts_[depth_ - 1] : Context::ROOT;
        }

        bool assign(uint32_t &mask, uint32_t bit, int &target,
                    int64_t value, int64_t minimum, int64_t maximum)
        {
            if (value < minimum || value > maximum || !mark(mask, bit))
                return fail(ConfigJsonResult::INVALID_VALUE);
            target = static_cast<int>(value);
            key_ = Field::NONE;
            return true;
        }

        bool number(int64_t value)
        {
            switch (current())
            {
            case Context::ROOT:
                if (key_ == Field::VERSION && value == JSON_FORMAT_VERSION)
                {
                    key_ = Field::NONE;
                    return mark(rootMask_, 1u << 1);
                }
                break;
            case Context::APP:
                switch (key_)
                {
                case Field::VERSION:
                    if (value == AppConfig::VERSION)
                    {
                        key_ = Field::NONE;
                        return mark(appMask_, 1u << 0);
                    }
                    break;
                case Field::INIT_POWER_ON: return assign(appMask_, 1u << 1, appConfig_.initPowerOn, value, 0, 1);
                case Field::DISP_FPS: return assign(appMask_, 1u << 2, appConfig_.dispFPS, value, 0, 1);
                case Field::BUTTON_DISP_MODE: return assign(appMask_, 1u << 3, appConfig_.buttonDispMode, value, 0, 2);
                case Field::BACK_LIGHT: return assign(appMask_, 1u << 4, appConfig_.backLight, value, 0, 1);
                case Field::RAPID_MODE_SYNCHRO: return assign(appMask_, 1u << 5, appConfig_.rapidModeSynchro, value, 0, 1);
                case Field::SYNCHRO_FETCH_PHASE: return assign(appMask_, 1u << 6, appConfig_.synchroFetchPhase, value, 0, 9);
                case Field::SOFTWARE_RAPID_SPEED: return assign(appMask_, 1u << 7, appConfig_.softwareRapidSpeed, value, 1, 30);
                case Field::TWIN_PORT_MODE: return assign(appMask_, 1u << 11, appConfig_.twinPortMode, value, 0, 1);
                case Field::ANALOG_MODE: return assign(appMask_, 1u << 12, appConfig_.analogMode, value, 0, 2);
                default: break;
                }
                break;
            case Context::RAPID_PHASE:
                if (rapidPhaseIndex_ < appConfig_.rapidPhase.size() && value >= 0 && value <= 1)
                {
                    appConfig_.rapidPhase[rapidPhaseIndex_++] = static_cast<int>(value);
                    return true;
                }
                break;
            case Context::RAPID_SETTING:
                if (rapidSettingIndex_ >= 2)
                    break;
                if (key_ == Field::MASK && value >= 0 && value <= UINT32_MAX &&
                    mark(rapidSettingMask_, 1u << 0))
                {
                    appConfig_.rapidSettings[rapidSettingIndex_].mask = static_cast<uint32_t>(value);
                    key_ = Field::NONE;
                    return true;
                }
                if (key_ == Field::DIV)
                    return assign(rapidSettingMask_, 1u << 1,
                                  appConfig_.rapidSettings[rapidSettingIndex_].div,
                                  value, 1, 4);
                break;
            case Context::ROT_ENC:
                if (rotEncIndex_ >= appConfig_.rotEnc.size())
                    break;
                if (key_ == Field::REVERSE)
                    return assign(rotEncMask_, 1u << 0, appConfig_.rotEnc[rotEncIndex_].reverse, value, 0, 1);
                if (key_ == Field::SCALE)
                    return assign(rotEncMask_, 1u << 1, appConfig_.rotEnc[rotEncIndex_].scale, value, 1, 256);
                if (key_ == Field::AXIS)
                    return assign(rotEncMask_, 1u << 2, appConfig_.rotEnc[rotEncIndex_].axis, value, 0, 9);
                break;
            case Context::ANALOG_SETTING:
                if (analogSettingIndex_ >= AppConfig::ANALOG_MAX)
                    break;
                if (key_ == Field::SENSITIVITY)
                    return assign(analogSettingMask_, 1u << 0,
                                  appConfig_.analogSettings[analogSettingIndex_].sensitivity,
                                  value, -16, 16);
                if (key_ == Field::OFFSET)
                    return assign(analogSettingMask_, 1u << 1,
                                  appConfig_.analogSettings[analogSettingIndex_].offset,
                                  value, -99, 99);
                if (key_ == Field::SCALE)
                    return assign(analogSettingMask_, 1u << 2,
                                  appConfig_.analogSettings[analogSettingIndex_].scale,
                                  value, 1, 99);
                break;
            case Context::PAD:
                if (key_ == Field::VID && value >= 0 && value <= UINT16_MAX && mark(padMask_, 1u << 0))
                {
                    padVid_ = static_cast<int>(value); key_ = Field::NONE; return true;
                }
                if (key_ == Field::PID && value >= 0 && value <= UINT16_MAX && mark(padMask_, 1u << 1))
                {
                    padPid_ = static_cast<int>(value); key_ = Field::NONE; return true;
                }
                if (key_ == Field::OUT_PORT_OFFSET && value >= 0 && value < PadManager::N_PORTS && mark(padMask_, 1u << 2))
                {
                    padOutPortOffset_ = static_cast<int>(value); key_ = Field::NONE; return true;
                }
                break;
            case Context::UNIT:
                return assignUnit(value);
            default:
                break;
            }
            return fail(ConfigJsonResult::INVALID_VALUE);
        }

        bool assignUnit(int64_t value)
        {
            auto set = [&](uint32_t bit, uint8_t &target, int maximum)
            {
                if (value < 0 || value > maximum || !mark(unitMask_, bit))
                    return fail(ConfigJsonResult::INVALID_VALUE);
                target = static_cast<uint8_t>(value);
                key_ = Field::NONE;
                return true;
            };

            switch (key_)
            {
            case Field::TYPE:
                if (value < 0 || value > static_cast<int>(PadConfig::Type::HAT) || !mark(unitMask_, 1u << 0)) break;
                unit_.type = static_cast<PadConfig::Type>(value); key_ = Field::NONE; return true;
            case Field::NUMBER: return set(1u << 1, unit_.number, UINT8_MAX);
            case Field::ANALOG_ON:
                if (value < 0 || value > static_cast<int>(PadConfig::AnalogPos::L) || !mark(unitMask_, 1u << 2)) break;
                unit_.analogOn = static_cast<PadConfig::AnalogPos>(value); key_ = Field::NONE; return true;
            case Field::ANALOG_OFF:
                if (value < 0 || value > static_cast<int>(PadConfig::AnalogPos::L) || !mark(unitMask_, 1u << 3)) break;
                unit_.analogOff = static_cast<PadConfig::AnalogPos>(value); key_ = Field::NONE; return true;
            case Field::HAT_POS:
                if (value < 0 || value > static_cast<int>(PadConfig::HatPos::INVALID) || !mark(unitMask_, 1u << 4)) break;
                unit_.hatPos = static_cast<PadConfig::HatPos>(value); key_ = Field::NONE; return true;
            case Field::INDEX: return set(1u << 5, unit_.index, UINT8_MAX);
            case Field::SUB_INDEX: return set(1u << 6, unit_.subIndex, UINT8_MAX);
            case Field::IN_PORT_OFFSET: return set(1u << 7, unit_.inPortOfs, UINT8_MAX);
            default: break;
            }
            return fail(ConfigJsonResult::INVALID_VALUE);
        }

        bool validateUnit(const PadConfig::Unit &unit, bool analogOutput) const
        {
            if (unit.type == PadConfig::Type::BUTTON && unit.number >= PadManager::N_BUTTONS)
                return false;
            if (unit.type == PadConfig::Type::ANALOG && unit.number >= PadManager::N_ANALOGS)
                return false;
            if (unit.type == PadConfig::Type::ANALOG && unit.analogOn == unit.analogOff)
                return false;
            if (unit.type == PadConfig::Type::HAT && unit.hatPos >= PadConfig::HatPos::INVALID)
                return false;
            if (unit.inPortOfs >= PadManager::N_PORTS)
                return false;
            return analogOutput ? unit.index < N_PAD_CONFIG_ANALOGS
                                : unit.index < PadState::MAX_BUTTONS;
        }

        std::array<Context, 8> contexts_{};
        size_t depth_ = 0;
        Field key_ = Field::NONE;
        ConfigJsonResult result_ = ConfigJsonResult::INVALID_JSON;
        bool complete_ = false;

        uint32_t rootMask_ = 0;
        uint32_t appMask_ = 0;
        uint32_t rapidSettingMask_ = 0;
        uint32_t rotEncMask_ = 0;
        uint32_t analogSettingMask_ = 0;
        uint32_t padMask_ = 0;
        uint32_t unitMask_ = 0;

        size_t rapidPhaseIndex_ = 0;
        size_t rapidSettingIndex_ = 0;
        size_t rotEncIndex_ = 0;
        size_t analogSettingIndex_ = 0;

        AppConfig appConfig_{};
        std::vector<PadConfig> padConfigs_;
        int padVid_ = 0;
        int padPid_ = 0;
        int padOutPortOffset_ = 0;
        std::vector<PadConfig::Unit> padButtons_;
        std::vector<PadConfig::Unit> padAnalogs_;
        PadConfig::Unit unit_{};
        bool unitIsAnalog_ = false;
    };

    template <typename Writer>
    void writeUnit(Writer &writer, const PadConfig::Unit &unit)
    {
        writer.StartObject();
        writer.Key("type"); writer.Int(static_cast<int>(unit.type));
        writer.Key("number"); writer.Uint(unit.number);
        writer.Key("analogOn"); writer.Int(static_cast<int>(unit.analogOn));
        writer.Key("analogOff"); writer.Int(static_cast<int>(unit.analogOff));
        writer.Key("hatPos"); writer.Int(static_cast<int>(unit.hatPos));
        writer.Key("index"); writer.Uint(unit.index);
        writer.Key("subIndex"); writer.Uint(unit.subIndex);
        writer.Key("inPortOffset"); writer.Uint(unit.inPortOfs);
        writer.EndObject();
    }

    template <typename Writer>
    void writeConfig(Writer &writer, const AppConfig &app,
                     const std::vector<PadConfig> &pads)
    {
        writer.StartObject();
        writer.Key("format"); writer.String("easy-arcade-config");
        writer.Key("version"); writer.Uint(JSON_FORMAT_VERSION);

        writer.Key("app");
        writer.StartObject();
        writer.Key("version"); writer.Int(AppConfig::VERSION);
        writer.Key("initPowerOn"); writer.Int(app.initPowerOn);
        writer.Key("dispFPS"); writer.Int(app.dispFPS);
        writer.Key("buttonDispMode"); writer.Int(app.buttonDispMode);
        writer.Key("backLight"); writer.Int(app.backLight);
        writer.Key("rapidModeSynchro"); writer.Int(app.rapidModeSynchro);
        writer.Key("synchroFetchPhase"); writer.Int(app.synchroFetchPhase);
        writer.Key("softwareRapidSpeed"); writer.Int(app.softwareRapidSpeed);
        writer.Key("rapidPhase"); writer.StartArray();
        for (auto value : app.rapidPhase) writer.Int(value);
        writer.EndArray();
        writer.Key("rapidSettings"); writer.StartArray();
        for (const auto &setting : app.rapidSettings)
        {
            writer.StartObject();
            writer.Key("mask"); writer.Uint(setting.mask);
            writer.Key("div"); writer.Int(setting.div);
            writer.EndObject();
        }
        writer.EndArray();
        writer.Key("rotEnc"); writer.StartArray();
        for (const auto &encoder : app.rotEnc)
        {
            writer.StartObject();
            writer.Key("reverse"); writer.Int(encoder.reverse);
            writer.Key("scale"); writer.Int(encoder.scale);
            writer.Key("axis"); writer.Int(encoder.axis);
            writer.EndObject();
        }
        writer.EndArray();
        writer.Key("twinPortMode"); writer.Int(app.twinPortMode);
        writer.Key("analogMode"); writer.Int(app.analogMode);
        writer.Key("analogSettings"); writer.StartArray();
        for (const auto &setting : app.analogSettings)
        {
            writer.StartObject();
            writer.Key("sensitivity"); writer.Int(setting.sensitivity);
            writer.Key("offset"); writer.Int(setting.offset);
            writer.Key("scale"); writer.Int(setting.scale);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        writer.Key("pads"); writer.StartArray();
        for (const auto &pad : pads)
        {
            writer.StartObject();
            writer.Key("vid"); writer.Uint(pad.getVID());
            writer.Key("pid"); writer.Uint(pad.getPID());
            writer.Key("outPortOffset"); writer.Uint(pad.getOutPortOfs());
            writer.Key("buttons"); writer.StartArray();
            for (size_t i = 0; i < pad.getButtonCount(); ++i)
                writeUnit(writer, pad.getButtonUnit(i));
            writer.EndArray();
            writer.Key("analogs"); writer.StartArray();
            for (size_t i = 0; i < pad.getAnalogCount(); ++i)
                writeUnit(writer, pad.getAnalogUnit(i));
            writer.EndArray();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }
}

ConfigJsonResult exportConfigJson(const char *path, const AppConfig &appConfig,
                                  const std::vector<PadConfig> &padConfigs)
{
    printf("Config export begin: %s, %lu pad configs\n", path,
           static_cast<unsigned long>(padConfigs.size()));
    auto finish = [path](ConfigJsonResult result)
    {
        printf("Config export end: %s, %s\n", path,
               configJsonResultText(result));
        return result;
    };

    if (!usbStorageMounted())
        return finish(ConfigJsonResult::STORAGE_NOT_READY);

    FIL file{};
    const auto openResult = f_open(&file, CONFIG_TEMP_PATH,
                                   FA_CREATE_ALWAYS | FA_WRITE);
    if (openResult != FR_OK)
    {
        printf("Config export open failed: %s, FatFS %d\n",
               CONFIG_TEMP_PATH, openResult);
        return finish(ConfigJsonResult::IO_ERROR);
    }
    printf("Config export file opened: %s\n", CONFIG_TEMP_PATH);

    FatFsOutputStream output(file);
    rapidjson::PrettyWriter<FatFsOutputStream> writer(output);
    writeConfig(writer, appConfig, padConfigs);
    output.Flush();
    const auto bytesWritten = f_tell(&file);
    auto result = output.ok() && writer.IsComplete() && f_sync(&file) == FR_OK
                      ? ConfigJsonResult::OK
                      : ConfigJsonResult::IO_ERROR;
    if (f_close(&file) != FR_OK)
        result = ConfigJsonResult::IO_ERROR;

    if (result != ConfigJsonResult::OK)
    {
        f_unlink(CONFIG_TEMP_PATH);
        return finish(result);
    }
    printf("Config export file written: %s, %lu bytes\n", CONFIG_TEMP_PATH,
           static_cast<unsigned long>(bytesWritten));

    f_unlink(CONFIG_BACKUP_PATH);
    const auto backupResult = f_rename(path, CONFIG_BACKUP_PATH);
    if (backupResult != FR_OK && backupResult != FR_NO_FILE)
    {
        printf("Config export backup rename failed: FatFS %d\n",
               backupResult);
        f_unlink(CONFIG_TEMP_PATH);
        return finish(ConfigJsonResult::IO_ERROR);
    }
    const auto renameResult = f_rename(CONFIG_TEMP_PATH, path);
    if (renameResult != FR_OK)
    {
        printf("Config export final rename failed: FatFS %d\n",
               renameResult);
        if (backupResult == FR_OK)
            f_rename(CONFIG_BACKUP_PATH, path);
        f_unlink(CONFIG_TEMP_PATH);
        return finish(ConfigJsonResult::IO_ERROR);
    }
    f_unlink(CONFIG_BACKUP_PATH);
    return finish(ConfigJsonResult::OK);
}

ConfigJsonResult importConfigJson(const char *path, AppConfig &appConfig,
                                  std::vector<PadConfig> &padConfigs)
{
    printf("Config import begin: %s\n", path);
    auto finish = [path](ConfigJsonResult result)
    {
        printf("Config import end: %s, %s\n", path,
               configJsonResultText(result));
        return result;
    };

    if (!usbStorageMounted())
        return finish(ConfigJsonResult::STORAGE_NOT_READY);

    FILINFO info{};
    const auto statResult = f_stat(path, &info);
    if (statResult == FR_NO_FILE || statResult == FR_NO_PATH)
        return finish(ConfigJsonResult::FILE_NOT_FOUND);
    if (statResult != FR_OK)
    {
        printf("Config import stat failed: %s, FatFS %d\n", path, statResult);
        return finish(ConfigJsonResult::IO_ERROR);
    }
    printf("Config import file found: %s, %lu bytes\n", path,
           static_cast<unsigned long>(info.fsize));
    if (info.fsize == 0 || info.fsize > MAX_JSON_FILE_SIZE)
        return finish(ConfigJsonResult::FILE_TOO_LARGE);

    FIL file{};
    const auto openResult = f_open(&file, path, FA_READ);
    if (openResult != FR_OK)
    {
        printf("Config import open failed: %s, FatFS %d\n", path, openResult);
        return finish(ConfigJsonResult::IO_ERROR);
    }
    printf("Config import file opened: %s\n", path);

    FatFsInputStream input(file);
    ConfigHandler handler;
    rapidjson::Reader reader;
    const auto parsed = reader.Parse<rapidjson::kParseValidateEncodingFlag>(input, handler);
    const auto closeResult = f_close(&file);

    if (!input.ok() || closeResult != FR_OK)
        return finish(ConfigJsonResult::IO_ERROR);
    if (input.limitExceeded())
        return finish(ConfigJsonResult::INVALID_VALUE);
    if (!parsed)
        return finish(handler.result());
    if (!handler.complete())
        return finish(ConfigJsonResult::INVALID_FORMAT);

    appConfig = handler.appConfig();
    padConfigs = handler.takePadConfigs();
    printf("Config import parsed: %lu pad configs\n",
           static_cast<unsigned long>(padConfigs.size()));
    return finish(ConfigJsonResult::OK);
}

const char *configJsonResultText(ConfigJsonResult result)
{
    switch (result)
    {
    case ConfigJsonResult::OK: return "Done";
    case ConfigJsonResult::STORAGE_NOT_READY: return "No USB";
    case ConfigJsonResult::FILE_NOT_FOUND: return "No File";
    case ConfigJsonResult::FILE_TOO_LARGE: return "TooLarge";
    case ConfigJsonResult::IO_ERROR: return "IO Error";
    case ConfigJsonResult::INVALID_JSON: return "Bad JSON";
    case ConfigJsonResult::INVALID_FORMAT: return "Bad Fmt";
    case ConfigJsonResult::INVALID_VALUE: return "BadValue";
    }
    return "Error";
}
