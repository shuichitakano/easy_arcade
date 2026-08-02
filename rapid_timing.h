#pragma once

#include <algorithm>
#include <cstdint>

namespace RapidTiming
{
inline bool globalPhaseOn(uint32_t frame, int divisor, bool back)
{
    const bool backPhase = (frame / std::max(1, divisor)) & 1u;
    return back ? backPhase : !backPhase;
}

inline bool synchronizedOn(uint32_t frame, uint32_t startFrame, int divisor)
{
    return (((frame - startFrame) / std::max(1, divisor)) & 1u) == 0;
}
}
