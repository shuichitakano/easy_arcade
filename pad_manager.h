/*
 * author : Shuichi TAKANO
 * since  : Sun Jan 14 2024 03:15:01
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <optional>
#include <array>
#include "pad_translator.h"
#include "pad_state.h"
#include "rot_encoder.h"
#include "serializer.h"
#include "app_config.h"
#include <functional>
#include <memory>

class PadManager
{
public:
    static constexpr int N_ANALOGS = 9;
    static constexpr int N_BUTTONS = 128;

    static constexpr int N_PORTS = 5;
    static constexpr int N_OUTPUT_PORTS = 4;
    static constexpr int N_ENCODER_PORTS = 2;

    enum class StateKind
    {
        PORT0,
        PORT1,
        PORT2,
        PORT3,
        MIDI,
    };

    static constexpr int VID_MIDI = 0;
    static constexpr int PID_MIDI = 1;

    struct PadInput
    {
        int vid;
        int pid;
        std::array<uint32_t, N_BUTTONS / 32> buttons;
        int hat;
        std::array<int, N_ANALOGS> analogs;

        bool getButton(int i) const { return (buttons[i >> 5] & (1u << (i & 31))); }

        PadInput() { reset(); }
        void reset()
        {
            buttons = {};
            hat = -1;
            analogs = {128, 128, 128, 0, 0, 128, 0, 0, 0};
        }
    };

    using PrintButtonFunc = std::function<void(PadStateButton)>;
    using PrintCnfAnalogFunc = std::function<void(PadConfigAnalog)>;
    using onExitConfigFunc = std::function<void()>;
    using onSaveFunc = std::function<void()>;

public:
    PadManager();
    void reset();

    void update(int dclk, bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong);
    void setData(int port, const PadInput &input);

    static PadManager &instance()
    {
        static PadManager inst;
        return inst;
    }

    void resetLatestPadData(int port)
    {
        latestPadData_[port].reset();
    }

    uint32_t getButtons(int port) const;
    uint32_t getNonRapidButtons(int port) const;
    std::array<uint32_t, 2> getNonRapidButtonsEachRapidPhase(int port) const;
    uint32_t getRapidFireMask(int port) const;
    uint32_t getNonMappedRapidFireMask(int port) const;
    void setNonMappedRapidFireMask(int port, uint32_t v);
    int getRapidFireDiv(int port) const;
    void setRapidFireDiv(int port, int v);
    void setRapidFirePhaseMask(uint32_t v);

    const PadState::AnalogState &getAnalogState(int port) const;

    void setVSyncCount(int count);

    void serialize(Serializer &s) const;
    void deserialize(Deserializer &s);

    void enterNormalMode();
    void enterConfigMode();
    void enterAnalogConfigMode();

    void setNormalModeLED(bool f)
    {
        normalModeLED_ = f;
    }

    bool isNormalMode() const
    {
        return !modeHandler_;
    }

    void setEnableModelChangeByButton(bool f)
    {
        enableModeChangeByButton_ = f;
    }
    void setEnableLED(bool f)
    {
        enableLED_ = f;
    }

    void setPrintButtonFunc(PrintButtonFunc f)
    {
        printButtonFunc_ = f;
    }
    void setPrintCnfAnalogFunc(PrintCnfAnalogFunc f)
    {
        printCnfAnalogFunc_ = f;
    }
    void setOnExitConfigFunc(onExitConfigFunc f)
    {
        onExitConfigFunc_ = f;
    }
    void setOnSaveFunc(onSaveFunc f)
    {
        onSaveFunc_ = f;
    }

    void resetConfig()
    {
        translator_.reset();
    }
    void setRotEncSetting(int kind, int axis, int scale);

    void setTwinPortMode(bool f)
    {
        twinPortMode_ = f;
    }

    void setAnalogMode(AppConfig::AnalogMode mode)
    {
        analogMode_ = mode;
    }

protected:
    class ModeHandler
    {
    public:
        virtual ~ModeHandler() = default;
        virtual void init(PadManager &mgr) = 0;
        virtual void update(PadManager &mgr, int dclk, bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong) = 0;
        virtual void setData(PadManager &mgr, int port, const PadInput &input) = 0;
    };

    struct ButtonSet
    {
        // ボタンは N_BUTTONS個まで
        std::array<uint32_t, N_BUTTONS / 32> buttons;

        // アナログのアサインは1つ
        int8_t analogIndex = -1;
        PadConfig::AnalogPos analogOn;
        PadConfig::AnalogPos analogOff;

        // Hat のアサインは1つ
        int hat = -1;

    public:
        ButtonSet() { clear(); }

        bool getButton(int i) const { return (buttons[i >> 5] & (1u << (i & 31))); }

        void filterForAnalog()
        {
            if (analogIndex >= 0)
            {
                buttons = {};
                hat = -1;
            }
        }

        bool hasData() const
        {
            for (auto b : buttons)
            {
                if (b)
                {
                    return true;
                }
            }
            return analogIndex >= 0 || hat >= 0;
        }

        void clear()
        {
            buttons = {};
            analogIndex = -1;
            hat = -1;
        }
    };

    class ButtonConfigMode : public ModeHandler
    {
    protected:
        bool waitFirstIdle_ = true; // 最初にボタン入力がない瞬間を待ち中
        int port_ = -1;
        std::array<int, N_ANALOGS> analogNeutral_[N_PORTS]{};
        int vid_ = 0;
        int pid_ = 0;

        int curButton_{}; // 現在設定中のボタン
        ButtonSet curButtonSet_;
        bool anyButtonOn_ = false;
        uint32_t idleCycle_ = 0;

        std::vector<ButtonSet> buttonSets_;

    public:
        void init(PadManager &mgr) override;
        void update(PadManager &mgr, int dclk, bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong) override;
        void setData(PadManager &mgr, int port, const PadInput &input) override;

    protected:
        virtual void next(PadManager &mgr);
        void saveAndExit(PadManager &mgr);

        virtual void printMessage(const PadManager &mgr) const;
        virtual int nButtons(const PadManager &mgr) const;
        virtual void appendUnit(const PadManager &mgr,
                                std::vector<PadConfig::Unit> units[2],
                                PadConfig::Unit &u,
                                int index);
        virtual void registerUnits(PadManager &mgr,
                                   std::vector<PadConfig::Unit> units[2]);
    };

    class AnalogConfigMode : public ButtonConfigMode
    {
    protected:
        void next(PadManager &mgr) override;

        void printMessage(const PadManager &mgr) const override;
        int nButtons(const PadManager &mgr) const override;
        void appendUnit(const PadManager &mgr,
                        std::vector<PadConfig::Unit> units[2],
                        PadConfig::Unit &u,
                        int index) override;
        void registerUnits(PadManager &mgr,
                           std::vector<PadConfig::Unit> units[2]) override;
    };

    uint32_t _getButtons(int port) const;

    void blinkLED(bool reverse, int n) const;
    void setLED(bool on) const;

private:
    std::array<PadInput, N_PORTS> latestPadData_;
    std::array<PadState, N_PORTS> padStates_;
    RotEncoder rotEncoders_[N_OUTPUT_PORTS][2];

    std::unique_ptr<ModeHandler> modeHandler_;

    bool enableModeChangeByButton_ = true;
    bool enableLED_ = true;

    bool normalModeLED_ = true;

    bool twinPortMode_ = false;
    AppConfig::AnalogMode analogMode_{};

    PadTranslator translator_;

    PrintButtonFunc printButtonFunc_;
    PrintCnfAnalogFunc printCnfAnalogFunc_;
    onExitConfigFunc onExitConfigFunc_;
    onSaveFunc onSaveFunc_;
};
