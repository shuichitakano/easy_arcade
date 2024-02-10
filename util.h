/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 01:17:42
 */

#pragma once
#include <stdio.h>

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
}