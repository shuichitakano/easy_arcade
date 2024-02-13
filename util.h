/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 01:17:42
 */

#pragma once
#include <stdio.h>
#include <hardware/structs/systick.h>

namespace util
{
    inline void
    dumpBytes(const void *p, size_t size)
    {
        auto *pp = static_cast<const uint8_t *>(p);
        size_t i = 0;
        while (i < size)
        {
            printf("%02x ", pp[i]);
            ++i;
            if ((i & 15) == 0)
            {
                printf("\n");
            }
        }
        if (i & 15)
        {
            printf("\n");
        }
    }

    inline void initSysTick()
    {
        systick_hw->csr = 0x5;
        systick_hw->rvr = 0x00FFFFFF;
    }

    // tick counterを取得
    // カウンタは減っていくのに注意
    inline uint32_t getSysTickCounter24()
    {
        return systick_hw->cvr;
    }
}