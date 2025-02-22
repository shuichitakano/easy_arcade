/*
 * author : Shuichi TAKANO
 * since  : Sun Nov 20 2022 03:53:16
 */

#include "pad_state.h"

#include "debug.h"
#include <cassert>
#include <cstdio>
#include <algorithm>

void PadState::reset()
{
    mappedButtons_ = 0;
    mappedButtonsPrev_ = 0;

    std::fill(std::begin(buttonMap_), std::end(buttonMap_), 0);
    std::fill(std::begin(buttonMap0_), std::end(buttonMap0_), 0);

    mappedButtonsRapidA_ = 0;
    mappedButtonsRapidB_ = 0;
    mappedRapidFireMask_ = 0;
}

void PadState::update()
{
    constexpr uint32_t rapidMask = ((1u << static_cast<int>(PadStateButton::UP)) |
                                    (1u << static_cast<int>(PadStateButton::DOWN)) |
                                    (1u << static_cast<int>(PadStateButton::LEFT)) |
                                    (1u << static_cast<int>(PadStateButton::RIGHT)) |
                                    (1u << static_cast<int>(PadStateButton::CMD)));

    bool cmd = mappedButtons_ & (1u << static_cast<int>(PadStateButton::CMD));
    uint32_t mappedTrigger = (mappedButtons_ ^ mappedButtonsPrev_) & mappedButtons_;
    uint32_t unmappedTrigger = (unmappedButtons_ ^ unmappedButtonsPrev_) & unmappedButtons_;

    if (cmd && (mappedTrigger || unmappedTrigger))
    {
        if (unmappedTrigger && !(rapidMask & mappedTrigger))
        {
            rapidFireMask_ ^= unmappedTrigger;
            DPRINT(("rapid fire mask = %08x\n", rapidFireMask_));
        }

        if (mappedTrigger & (1u << static_cast<int>(PadStateButton::UP)))
        {
            rapidFireDiv_ = std::max(1, rapidFireDiv_ - 1);
            DPRINT(("rapid div: %d\n", rapidFireDiv_));
        }
        if (mappedTrigger & (1u << static_cast<int>(PadStateButton::DOWN)))
        {
            rapidFireDiv_ = std::min(4, rapidFireDiv_ + 1);
            DPRINT(("rapid div: %d\n", rapidFireDiv_));
        }
    }

    // それぞれのフェーズの連射ボタン押下状態を更新
    uint32_t mmA = 0, mmB = 0;
    uint32_t mm = 0;
    for (int i = 0; i < nMapButtons_; ++i)
    {
        auto b = buttonMap_[i];
        if (rapidFireMask_ & (1u << i))
        {
            mmA |= b & rapidFirePhase_;
            mmB |= b & ~rapidFirePhase_;
            mm |= buttonMap0_[i];
        }
        else
        {
            mmA |= b;
            mmB |= b;
        }
    }
    mappedButtonsRapidA_ = mmA;
    mappedButtonsRapidB_ = mmB;
    mappedRapidFireMask_ = mm;
}

uint32_t PadState::getButtons() const
{
    bool rapidFire = (vsyncCount_ / std::max(1, rapidFireDiv_)) & 1;
    const uint32_t rapidMask = rapidFire ? 0xffffffff : 0;

    const auto maskA = rapidMask & rapidFirePhase_;
    const auto maskB = (~rapidMask) & (~rapidFirePhase_);
    const auto maskAB = maskA | maskB;

    uint32_t r = 0;
    for (int i = 0; i < nMapButtons_; ++i)
    {
        auto b = buttonMap_[i];
        if (rapidFireMask_ & (1u << i))
        {
            r |= b & maskAB;
        }
        else
        {
            r |= b;
        }
    }

    return r;
}

bool PadState::set(const PadTranslator &translator,
                   int vid, int pid, int portOfs,
                   const uint32_t *buttons, int nButtons,
                   const int *analogs, int nAnalogs, int hat)
{
    mappedButtonsPrev_ = mappedButtons_;
    unmappedButtonsPrev_ = unmappedButtons_;

    if (auto *cfg = translator.find(vid, pid, portOfs))
    {
        {
            uint32_t mapped = 0;
            uint32_t unmapped = 0;
            auto n = std::min<int>(MAX_BUTTONS, cfg->getButtonCount());
            for (auto i = 0u; i < n; ++i)
            {
                bool f = cfg->convertButton(i, buttons, nButtons, analogs, nAnalogs, hat);
                const auto &unit = cfg->getButtonUnit(i);
                uint32_t m0 = 1u << unit.index;
                uint32_t m = f ? m0 : 0;
                buttonMap_[i] = m;
                buttonMap0_[i] = m0;
                mapped |= m;
                unmapped |= f ? 1u << i : 0;
            }
            mappedButtons_ = mapped;
            unmappedButtons_ = unmapped;
            nMapButtons_ = n;
        }
        {
            std::optional<int> values[N_PAD_CONFIG_ANALOGS];
            const auto n = cfg->getAnalogCount();
            for (auto i = 0u; i < n; ++i)
            {
                const auto &unit = cfg->getAnalogUnit(i);
                values[unit.index] = cfg->convertAnalog(i, buttons, nButtons, analogs, nAnalogs, hat);

                // todo: ボタン複数アサインの時の振る舞い
            }

            auto computeValue = [](std::optional<int> h, std::optional<int> l)
                -> std::tuple<int, bool>
            {
                if (h && l)
                {
                    return {(h.value() - l.value() + ANALOG_MAX_VAL) >> 1, true};
                }
                else if (h)
                {
                    return {h.value(), false};
                }
                else if (l)
                {
                    return {ANALOG_MAX_VAL - l.value(), false};
                }
                return {};
            };

            for (int i = 0; i < MAX_ANALOGS; ++i)
            {
                auto [v, hasCenter] = computeValue(values[i * 2 + 0],
                                                   values[i * 2 + 1]);
                analog_.values[i] = v;
                analog_.hasCenter[i] = hasCenter;
            }
        }

        // dump();
        update();
        return true;
    }
    // printf("unknown device [%04x, %04x]\n", vid, pid);
    return false;
}

void PadState::dump() const
{
    for (int i = 31; i >= 0; --i)
    {
        printf("%d", mappedButtons_ & (1 << i) ? 1 : 0);
    }
    printf(" : %d %d %d %d\n",
           analog_.values[0], analog_.values[1], analog_.values[2], analog_.values[3]);
}
