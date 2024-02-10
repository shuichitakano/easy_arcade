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

inline static constexpr size_t N_PAD_STATE_BUTTONS = static_cast<size_t>(PadStateButton::MAX);

class PadManager
{
public:
    static constexpr int N_ANALOGS = 9;
    static constexpr int N_BUTTONS = 32;

    static constexpr int N_PORTS = 2;

    struct PadInput
    {
        int vid;
        int pid;
        uint32_t buttons;
        int hat;
        std::array<int, N_ANALOGS> analogs;

        PadInput() { reset(); }
        void reset()
        {
            buttons = 0;
            hat = -1;
            analogs = {128, 128, 128, 0, 0, 128, 0, 0, 0};
        }
    };

public:
    PadManager();

    void update(bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong);
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

    uint32_t getButtons(int port) const { return padStates_[port].getButtons(); }
    void setVSyncCount(int count);

protected:
    void updateNormalMode(bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong);
    void updateConfigMode(bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong);

    void setDataNormalMode(int port, const PadInput &input);
    void setDataConfigMode(int port, const PadInput &input);

    void nextButtonConfig();
    void saveConfigAndExit();

private:
    enum class Mode
    {
        NORMAL,
        CONFIG,
    };

    std::array<PadInput, N_PORTS> latestPadData_;
    std::array<PadState, N_PORTS> padStates_;

    struct ButtonSet
    {
        // ボタンは32個まで
        uint32_t buttons;

        // アナログのアサインは1つ
        int8_t analogIndex;
        PadConfig::AnalogPos analogOn;
        PadConfig::AnalogPos analogOff;

        // Hat のアサインは1つ
        int hat = -1;

    public:
        ButtonSet() { clear(); }

        bool hasData() const
        {
            return buttons || analogIndex >= 0 || hat >= 0;
        }

        void clear()
        {
            buttons = 0;
            analogIndex = -1;
            hat = -1;
        }
    };

    struct ConfigMode
    {
        bool waitFirstIdle_ = true; // 最初にボタン入力がない瞬間を待ち中
        int port_ = -1;
        std::array<int, N_ANALOGS> analogNeutral_[N_PORTS]{};
        int vid_ = 0;
        int pid_ = 0;

        PadStateButton curButton_{}; // 現在設定中のボタン
        ButtonSet curButtonSet_;

        std::vector<ButtonSet> buttonSets_;
    };

    Mode mode_{};
    ConfigMode configMode_;

    PadTranslator translator_;
};
