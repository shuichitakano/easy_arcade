/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 03:57:41
 */

#include "hid_info.h"
#include <cstdio>
#include <algorithm>
#include "util.h"
#include "debug.h"

namespace
{
    void dumpReports(const std::vector<HIDInfo::Report> &v)
    {
        for (auto &r : v)
        {
            r.dump();
        }

        DPRINT(("Buttons: "));
        for (auto &r : v)
        {
            if (r.isButton())
            {
                DPRINT(("%d ", r.bitOfs_));
            }
        }
        DPRINT(("\n"));

        DPRINT(("Hats: "));
        for (auto &r : v)
        {
            if (r.isHat())
            {
                DPRINT(("%d ", r.bitOfs_));
            }
        }
        DPRINT(("\n"));

        DPRINT(("Analog: "));
        for (auto &r : v)
        {
            auto idx = r.getAnalogIndex();
            if (idx >= 0)
            {
                DPRINT(("(%d, %dbits [%d:%d]) ", r.bitOfs_, r.bits_, r.min_, r.max_));
            }
        }
        DPRINT(("\n"));
    }
} // namespace

void HIDInfo::Report::dump() const
{
    DPRINT(("usage = %08x, ofs = %d, bits = %d, min = %d, max = %d, const = %d, array = %d, nullable = %d\n",
            usage_, bitOfs_, bits_, min_, max_, isConst_, isArray_, isNullable_));
}

void HIDInfo::ReportSet::dump() const
{
    if (!inputs_.empty())
    {
        DPRINT(("inputs:\n"));
        dumpReports(inputs_);
    }
    if (!outputs_.empty())
    {
        DPRINT(("outputs:\n"));
        dumpReports(outputs_);
    }
    if (!features_.empty())
    {
        DPRINT(("features:\n"));
        dumpReports(features_);
    }
}

void HIDInfo::parseDesc(const uint8_t *p, const uint8_t *tail,
                        bool enableUnknowns,
                        bool enableOutput, bool enableFeature)
{
    enum class Type
    {
        MAIN = 0,
        GLOBAL = 1,
        LOCAL = 2,
    };
    enum class MainTag
    {
        INPUT = 8,
        OUTPUT = 9,
        FEATURE = 11,
        COLLECTION = 10,
        END_COLLECTION = 12,
    };
    enum class GlobalTag
    {
        USAGE_PAGE = 0,
        LOGICAL_MINIMUM = 1,
        LOGICAL_MAXIMUM = 2,
        PHYSICAL_MINIMUM = 3,
        PHYSICAL_MAXIMUM = 4,
        UNIT_EXPONENT = 5,
        UNIT = 6,
        REPORT_SIZE = 7,
        REPORT_ID = 8,
        REPORT_COUNT = 9,
        PUSH = 10,
        POP = 11,
    };
    enum class LocalTag
    {
        USAGE = 0,
        USAGE_MINIMUM = 1,
        USAGE_MAXIMUM = 2,
        DESIGNATOR_INDEX = 3,
        DESIGNATOR_MINIMUM = 4,
        DESIGNATOR_MAXIMUM = 5,
        STRING_INDEX = 7,
        STRING_MINIMUM = 8,
        STRING_MAXIMUM = 9,
        DELIMITER = 10,
    };
    enum MainReportBit
    {
        CONSTANT = 1,
        VARIABLE = 2,
        RELATIVE = 4,
        WRAP = 8,
        NONLINEAR = 16,
        NO_PREFERRED = 32,
        NULL_STATE = 64,
        VOLATILE = 128,
        BUFFERED_BYTES = 256,
    };

    struct State
    {
        int reportID = 0;
        int usagePage = 0;
        std::vector<int> usages;
        int usageMin = 0;
        int usageMax = 0;
        int logicalMin = 0;
        int logicalMax = 0;
        int physicalMin = 0;
        int physicalMax = 0;
        int reportSize = 0;
        int reportCount = 0;

        void clearLocal()
        {
            usages.clear();
            usageMin = 0;
            usageMax = 0;
        }
    };

    reportSets_.clear();

    std::vector<State> stateStack;
    stateStack.emplace_back();

    int collectionLv = 0;
    int bitOfs = 0;

    while (p < tail)
    {
        auto prefix = *p++;
        constexpr int sizetbl[] = {0, 1, 2, 4};
        auto size = sizetbl[prefix & 0x3];
        auto type = (prefix >> 2) & 0x3;
        auto tag = (prefix >> 4) & 0xf;
        if (prefix == 0xfe)
        {
            // long item
            if (p + 2 > tail)
            {
                DPRINT(("long item too short\n"));
                break;
            }
            size = p[0];
            tag = p[1];
            // typeはreservedでよい？
            p += 2;
        }
        if (p + size > tail)
        {
            DPRINT(("item too short\n"));
            break;
        }

        int value = 0;
        switch (size)
        {
        case 1:
            value = static_cast<int8_t>(*p);
            break;
        case 2:
            value = static_cast<int16_t>(p[0] | (p[1] << 8));
            break;
        case 4:
            value = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
            break;
        }

        auto &state = stateStack.back();

        auto getBitStepAndAdjustAlign = [&]
        {
            bool isBufferedBytes = value & BUFFERED_BYTES;
            auto bitStep = (isBufferedBytes ? 8 : 1) * state.reportSize;
            if (isBufferedBytes)
            {
                bitOfs = (bitOfs + 7) & ~7;
            }
            return bitStep;
        };

        auto addReport = [&](auto &v, int bitStep)
        {
            auto check = [&](int usage)
            {
                if (enableUnknowns)
                {
                    return true;
                }

                if ((usage >> 16) == 0x09 ||
                    usage == 0x00010039 ||
                    (usage >= 0x00010030 && usage <= 0x00010038))
                {
                    return true;
                }

                return false;
            };

            bool isConst = value & CONSTANT;
            bool isArray = !(value & VARIABLE);
            bool isNullable = value & NULL_STATE;

            int ofs = bitOfs;
            int ct = state.reportCount;

            if (state.usages.empty())
            {
                if (state.usageMin > 0 && state.usageMax > state.usageMin)
                {
                    for (int i = state.usageMin; i <= state.usageMax && ct > 0; ++i, --ct)
                    {
                        auto u = (state.usagePage << 16) | i;
                        if (check(u))
                        {
                            v.emplace_back();
                            auto &r = v.back();
                            r.usage_ = u;
                            r.min_ = state.logicalMin;
                            r.max_ = state.logicalMax;
                            r.bits_ = state.reportSize;
                            r.bitOfs_ = ofs;
                            r.isConst_ = isConst;
                            r.isArray_ = isArray;
                            r.isNullable_ = isNullable;
                        }
                        ofs += bitStep;
                    }
                    if (ct > 0)
                    {
                        DPRINT(("usage count mismatch, left = %d\n", ct));
                    }
                }
            }
            else
            {
                if (state.usages.size() != ct)
                {
                    DPRINT(("usage count mismatch %d != %d\n", state.usages.size(), ct));
                    state.usages.resize(ct);
                }
                for (auto usage : state.usages)
                {
                    auto u = (state.usagePage << 16) | usage;
                    if (check(u))
                    {
                        v.emplace_back();
                        auto &r = v.back();
                        r.usage_ = u;
                        r.min_ = state.logicalMin;
                        r.max_ = state.logicalMax;
                        r.bits_ = state.reportSize;
                        r.bitOfs_ = ofs;
                        r.isConst_ = isConst;
                        r.isArray_ = isArray;
                        r.isNullable_ = isNullable;
                    }
                    ofs += bitStep;
                }
            }
        };

        switch (static_cast<Type>(type))
        {
        case Type::MAIN:
        {
            auto mainTag = static_cast<MainTag>(tag);
            switch (mainTag)
            {
            case MainTag::INPUT:
            {
                auto bitStep = getBitStepAndAdjustAlign();
                {
                    addReport(reportSets_[state.reportID].inputs_, bitStep);
                }
                bitOfs += bitStep * state.reportCount;
            }
            break;

            case MainTag::OUTPUT:
            {
                auto bitStep = getBitStepAndAdjustAlign();
                if (enableOutput)
                {
                    addReport(reportSets_[state.reportID].outputs_, bitStep);
                }
                bitOfs += bitStep * state.reportCount;
            }
            break;

            case MainTag::FEATURE:
            {
                auto bitStep = getBitStepAndAdjustAlign();
                if (enableFeature)
                {
                    addReport(reportSets_[state.reportID].features_, bitStep);
                }
                bitOfs += bitStep * state.reportCount;
            }
            break;

            case MainTag::COLLECTION:
                if (collectionLv == 0 && !state.usages.empty())
                {
                    usageLV0_ = (state.usagePage << 16) | state.usages[0];
                }
                collectionLv++;
                state.usages.clear();
                break;

            case MainTag::END_COLLECTION:
                --collectionLv;
                if (collectionLv < 0)
                {
                    DPRINT(("collection level underflow\n"));
                }
                state.usages.clear();
                break;
            }

            state.clearLocal();
        }
        break;

        case Type::GLOBAL:
            switch (static_cast<GlobalTag>(tag))
            {
            case GlobalTag::USAGE_PAGE:
                state.usagePage = value;
                break;
            case GlobalTag::LOGICAL_MINIMUM:
                state.logicalMin = value;
                break;
            case GlobalTag::LOGICAL_MAXIMUM:
                state.logicalMax = value;
                break;
            case GlobalTag::PHYSICAL_MINIMUM:
                state.physicalMin = value;
                break;
            case GlobalTag::PHYSICAL_MAXIMUM:
                state.physicalMax = value;
                break;
            case GlobalTag::UNIT_EXPONENT:
                break;
            case GlobalTag::UNIT:
                break;
            case GlobalTag::REPORT_SIZE:
                state.reportSize = value;
                break;
            case GlobalTag::REPORT_ID:
                state.reportID = value;
                bitOfs = 0;
                break;
            case GlobalTag::REPORT_COUNT:
                state.reportCount = value;
                break;
            case GlobalTag::PUSH:
                stateStack.emplace_back(state); // stateは参照なので
                // この先で state に触ってはいけない
                break;
            case GlobalTag::POP:
                if (stateStack.size() <= 1)
                {
                    DPRINT(("state stack underflow\n"));
                }
                else
                {
                    // この先で state に触ってはいけない
                    stateStack.pop_back();
                }
                break;
            }
            break;

        case Type::LOCAL:
            switch (static_cast<LocalTag>(tag))
            {
            case LocalTag::USAGE:
                state.usages.push_back(value);
                break;
            case LocalTag::USAGE_MINIMUM:
                state.usageMin = value;
                break;
            case LocalTag::USAGE_MAXIMUM:
                state.usageMax = value;
                break;
            case LocalTag::DESIGNATOR_INDEX:
                break;
            case LocalTag::DESIGNATOR_MINIMUM:
                break;
            case LocalTag::DESIGNATOR_MAXIMUM:
                break;
            case LocalTag::STRING_INDEX:
                break;
            case LocalTag::STRING_MINIMUM:
                break;
            case LocalTag::STRING_MAXIMUM:
                break;
            case LocalTag::DELIMITER:
                break;
            }
            break;
        }

        p += size;
    }

    auto removeRedundant = [](auto &v)
    {
        if (v.size() < 2)
        {
            return;
        }
        auto prev = v.begin();
        for (auto it = prev + 1; it != v.end(); ++it)
        {
            if (prev->usage_ == it->usage_)
            {
                it = v.erase(prev);
                prev = it;
            }
            else
            {
                prev = it;
            }
        }
    };

    for (auto &v : reportSets_)
    {
        auto &rs = v.second;
        std::sort(rs.inputs_.begin(), rs.inputs_.end());
        std::sort(rs.outputs_.begin(), rs.outputs_.end());
        std::sort(rs.features_.begin(), rs.features_.end());
        removeRedundant(rs.inputs_);
        removeRedundant(rs.outputs_);
        removeRedundant(rs.features_);
    }

    dump();
}

void HIDInfo::parseReport(const uint8_t *p, size_t size,
                          uint32_t &buttons,
                          int &hat,
                          std::array<int, N_ANALOGS> &analogs) const
{
    buttons = 0;
    hat = -1;
    analogs = {};

    if (reportSets_.empty())
    {
        return;
    }

    const ReportSet *rs{};
    if (reportSets_.size() == 1 && reportSets_.begin()->first == 0)
    {
        // reportID が指定されていない
        rs = &reportSets_.begin()->second;
    }
    else
    {
        int reportID = *p++;
        auto it = reportSets_.find(reportID);
        if (it == reportSets_.end())
        {
            DPRINT(("unknown reportID %d\n", reportID));
            return;
        }
        rs = &it->second;
    }

    auto getBits = [&](int ofs, int bits)
    {
        int byteOfs = ofs >> 3;
        int bitMod = ofs & 7;
        auto pp = p + byteOfs;
        int v = *pp++ >> bitMod;
        int shift = 8 - bitMod;
        while (shift < bits)
        {
            v |= *pp++ << shift;
            shift += 8;
        }
        v &= (1 << bits) - 1;
        return v;
    };

    for (auto &r : rs->inputs_)
    {
        if (r.isButton())
        {
            int num = (r.usage_ & 0xffff) - 1;
            if (num >= 0 && num < 32)
            {
                bool f = p[r.bitOfs_ >> 3] & (1 << (r.bitOfs_ & 7));
                if (f)
                {
                    buttons |= 1 << num;
                }
            }
        }
        else if (r.isHat())
        {
            auto v = getBits(r.bitOfs_, r.bits_);
            if (v >= 0 && v < 8)
            {
                hat = v;
            }
        }
        else if (int analogID = r.getAnalogIndex(); analogID >= 0)
        {
            int32_t v = getBits(r.bitOfs_, r.bits_);
            if (r.min_ < 0)
            {
                // 符号拡張の条件はこれで良いのか？
                int s = 32 - r.bits_;
                v = (v << s) >> s;
            }

            v = std::clamp<int>((v - r.min_) * 255 / (r.max_ - r.min_), 0, 255);
            analogs[analogID] = v;
        }
    }

#if 0
    if (!analogs.empty())
    {
        DPRINT(("A: "));
        for (int v : analogs)
        {
            DPRINT(("%d ", v));
        }
    }

    DPRINT(("B: "));
    for (int i = 0; i < 32; ++i)
    {
        bool f = buttons & (1u << i);
        DPRINT(("%d", f));
    }
    if (hat >= 0)
    {
        DPRINT((" H: %d", hat));
    }
    DPRINT(("\n"));
#endif
}

void HIDInfo::dump()
{
    DPRINT(("usageLV0 = %08x\n", usageLV0_));
    for (auto &v : reportSets_)
    {
        DPRINT(("reportID = %d\n", v.first));
        v.second.dump();
    }
}
