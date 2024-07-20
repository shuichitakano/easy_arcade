/*
 * author : Shuichi TAKANO
 * since  : Sun Jun 09 2024 11:49:36
 */

#include "rot_encoder.h"
#include <math.h>
#include "board.h"
#include <stdio.h>

void RotEncoder::update(int dclk, int velocity)
{
    rct_ += dclk;

    int v = velocity * scale_;
    int ct_s = abs(v);
    if (ct_s == 0)
    {
        return;
    }

    int clk_ct = CPU_CLOCK / ct_s;

    if (rct_ >= clk_ct)
    {
        // rct_ -= clk_ct;
        rct_ = 0;

        if (v > 0)
        {
            ++state_;
        }
        else
        {
            --state_;
        }
        state_ &= 3;
    }
}

std::pair<bool, bool> RotEncoder::getEncState() const
{
    static constexpr std::pair<bool, bool> table[] = {
        {false, false},
        {true, false},
        {true, true},
        {false, true},
    };
    return table[state_];
}

uint32_t RotEncoder::overrideButton(uint32_t buttons, int bitA, int bitB) const
{
    auto [a, b] = getEncState();
    buttons &= ~((1u << bitA) | (1u << bitB));
    buttons |= (a << bitA) | (b << bitB);
    return buttons;
}

// コントローラーに紐付けられると良い
// LR, UDがアサインされていない時だけ動作するといいかも

// 次は連射フェッチいちをやろう
