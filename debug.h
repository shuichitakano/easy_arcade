/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 20:42:55
 */

#pragma once

#include <cstdio>

#ifdef NDEBUG
#define DPRINT(x) \
    do            \
    {             \
    } while (0)
#else
#define DPRINT(x) printf x
#endif
