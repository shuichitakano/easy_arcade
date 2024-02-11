/*
 * author : Shuichi TAKANO
 * since  : Sun Nov 20 2022 03:53:16
 */

#include "pad_state.h"

#include <cassert>
#include <cstdio>

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
            printf("rapid fire mask = %08x\n", rapidFireMask_);
        }

        if (mappedTrigger & (1u << static_cast<int>(PadStateButton::UP)))
        {
            rapidFireDiv_ = std::max(0, rapidFireDiv_ - 1);
            printf("rapid div: %d\n", rapidFireDiv_);
        }
        if (mappedTrigger & (1u << static_cast<int>(PadStateButton::DOWN)))
        {
            rapidFireDiv_ = std::min(3, rapidFireDiv_ + 1);
            printf("rapid div: %d\n", rapidFireDiv_);
        }
    }
}

uint32_t PadState::getButtons() const
{
    bool rapidFire = (vsyncCount_ >> rapidFireDiv_) & 1;
    uint32_t rapidMask = rapidFire ? 0xffffffff : 0;

    auto maskA = rapidMask & rapidFirePhase_;
    auto maskB = (~rapidMask) & (~rapidFirePhase_);

    auto m = rapidFireMask_ & (maskA | maskB); // マップ前のボタンに対する連射マスク

    uint32_t r = 0;
    for (int i = 0; i < nMapButtons_; ++i)
    {
        r |= (m & 1u << i) ? 0 : buttonMap_[i];
    }

    return r;
}

bool PadState::set(const PadTranslator &translator,
                   int vid, int pid, uint32_t buttons, const int *analogs, int nAnalogs, int hat)
{
    mappedButtonsPrev_ = mappedButtons_;
    unmappedButtonsPrev_ = unmappedButtons_;

    if (auto *cfg = translator.find(vid, pid))
    {
        {
            uint32_t mapped = 0;
            uint32_t unmapped = 0;
            uint32_t phase = 0;
            auto n = std::min<int>(MAX_BUTTONS, cfg->getButtonCount());
            for (auto i = 0u; i < n; ++i)
            {
                bool f = cfg->convertButton(i, buttons, analogs, nAnalogs, hat);
                const auto &unit = cfg->getButtonUnit(i);
                uint32_t m = f ? 1u << unit.index : 0;
                buttonMap_[i] = m;
                mapped |= m;
                unmapped |= f ? 1u << i : 0;

                // phase |= unit.subIndex & 1 ? 1u << i : 0;
                phase |= unit.index & 1 ? 1u << unit.index : 0;
            }
            mappedButtons_ = mapped;
            unmappedButtons_ = unmapped;
            rapidFirePhase_ = phase;
            nMapButtons_ = n;
        }
        {
            auto n = std::min<int>(MAX_ANALOGS, cfg->getAnalogCount());
            for (auto i = 0u; i < n; ++i)
            {
                auto v = cfg->convertAnalog(i, buttons, analogs, nAnalogs, hat);
                analog_[i] = v;
            }
        }

        dump();
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
    printf(" : %d %d %d %d\n", analog_[0], analog_[1], analog_[2], analog_[3]);
}
