/*
 * author : Shuichi TAKANO
 * since  : Sun Nov 20 2022 03:50:18
 */
#pragma once

#include <cstdint>
#include <cstdlib>
#include "pad_translator.h"
#include <array>

enum class PadStateButton
{
    CMD,
    COIN,
    START,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    A,
    B,
    C,
    D,
    E,
    F,
    MAX,

    COIN_2P = MAX,
    START_2P,
    UP_2P,
    DOWN_2P,
    LEFT_2P,
    RIGHT_2P,
    A_2P,
    B_2P,
    C_2P,
    D_2P,
    E_2P,
    F_2P,
    MAX_2P,
};
inline static constexpr int N_PAD_STATE_BUTTONS = static_cast<int>(PadStateButton::MAX);

class PadState
{
public:
    inline static constexpr size_t MAX_BUTTONS = 32;
    inline static constexpr size_t MAX_ANALOGS = 4;

    struct AnalogState
    {
        std::array<int, MAX_ANALOGS> values;
        std::array<bool, MAX_ANALOGS> hasCenter;
    };

public:
    bool set(const PadTranslator &,
             int vid, int pid, int portOfs,
             const uint32_t *buttons, int nButtons,
             const int *analogs, int nAnalogs, int hat);

    // uint8_t getAnalog(int ch) const { return analog_[ch]; }
    const AnalogState &getAnalogState() const { return analog_; }
    uint32_t getButtons() const; // 連射処理されたボタン
    uint32_t getNonRapidButtons() const { return mappedButtons_; }

    // 各フェーズの連射ボタン押下状態を取得
    std::array<uint32_t, 2> getNonRapidButtonsEachRapidPhase() const
    {
        return {mappedButtonsRapidA_, mappedButtonsRapidB_};
    }

    uint32_t getRapidFireMask() const { return mappedRapidFireMask_; }

    uint32_t getNonMappedRapidFireMask() const { return rapidFireMask_; }
    void setNonMappedRapidFireMask(uint32_t v) { rapidFireMask_ = v; }

    void setVSyncCount(uint32_t v) { vsyncCount_ = v; }

    int getRapidFireDiv() const { return rapidFireDiv_; }
    void setRapidFireDiv(int v) { rapidFireDiv_ = v; }

    void setRapidFirePhaseMask(uint32_t v) { rapidFirePhase_ = v; }

    void reset();
    void dump() const;

protected:
    void update();

private:
    int rapidFireDiv_ = 1;
    uint32_t rapidFireMask_ = 0;
    uint32_t rapidFirePhase_ = 0xaaaaaaaa;
    uint32_t vsyncCount_ = 0;

    uint32_t mappedRapidFireMask_ = 0;

    uint32_t buttonMap_[MAX_BUTTONS]{};  // 入力ボタン番号に対する出力ボタンビットマスク
    uint32_t buttonMap0_[MAX_BUTTONS]{}; // 入力状態に関わらないマップ

    // マップ済みのボタン(連射処理はされていない)
    uint32_t mappedButtons_{};
    uint32_t mappedButtonsPrev_{};

    uint32_t mappedButtonsRapidA_{};
    uint32_t mappedButtonsRapidB_{};

    // マップ前のボタン
    uint32_t unmappedButtons_{};
    uint32_t unmappedButtonsPrev_{};

    int nMapButtons_ = 0;

    AnalogState analog_{};
};

// PadState *getPadState(size_t port = 0);