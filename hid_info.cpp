/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 03:57:41
 */

#include "hid_info.h"
#include <cstdio>
#include <algorithm>
#include <cinttypes>
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
                DPRINT(("(%d, %dbits [%" PRId64 ":%" PRId64 "]) ", r.bitOfs_, r.bits_, r.min_, r.max_));
            }
        }
        DPRINT(("\n"));
    }
} // namespace

void HIDInfo::Report::dump() const
{
    DPRINT(("usage = %08x, ofs = %d, bits = %d, min = %" PRId64 ", max = %" PRId64 ", const = %d, array = %d, nullable = %d\n",
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
                        bool enableUnknowns, bool enableOutput, bool enableFeature)
{
    // Bound allocations and arithmetic for descriptors supplied by USB devices.
    constexpr uint32_t maxReportBits = 65535u * 8;
    constexpr size_t maxFields = 1024;
    constexpr size_t maxStackDepth = 32;
    struct Globals
    {
        uint32_t reportID = 0, usagePage = 0, reportSize = 0, reportCount = 0;
        int64_t logicalMin = 0, logicalMax = 0;
    } state;
    struct UsageRange { uint32_t first, last; };
    std::vector<UsageRange> usages;
    bool pendingUsageMin = false;
    uint32_t usageMin = 0;
    std::vector<Globals> stack;
    std::map<int, ReportSet> parsed;
    size_t fields = 0;
    unsigned collectionDepth = 0;
    uint32_t topUsage = 0;
    bool reportIDs = false;

    reportSets_.clear();
    usageLV0_ = 0;
    hasReportIDs_ = false;
    if (!p || !tail || tail < p) return;

    auto supported = [](uint32_t usage) {
        return (usage >> 16) == 9 || usage == 0x10039 ||
               (usage >= 0x10030 && usage <= 0x10038);
    };
    while (p < tail)
    {
        const uint8_t prefix = *p++;
        if (prefix == 0xfe)
        {
            if (tail - p < 2) return;
            const unsigned size = p[0];
            p += 2;
            if (size_t(tail - p) < size) return;
            p += size; // Unknown long items do not change the short-item state.
            continue;
        }
        constexpr unsigned sizes[] = {0, 1, 2, 4};
        const unsigned size = sizes[prefix & 3];
        if (size_t(tail - p) < size) return;
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= uint32_t(p[i]) << (8 * i);
        p += size;
        const auto signedValue = [&]() -> int64_t {
            if (size && (value & (uint32_t(1) << (size * 8 - 1))))
                return int64_t(value) - (int64_t(1) << (size * 8));
            return value;
        };
        const auto usageValue = [&]() {
            return size == 4 ? value : (state.usagePage << 16) | value;
        };
        const unsigned type = (prefix >> 2) & 3;
        const unsigned tag = prefix >> 4;
        if (type == 1) // Global items. PUSH/POP must not copy local usages.
        {
            switch (tag)
            {
            case 0:
                if (value > 0xffff) return;
                state.usagePage = value;
                break;
            case 1: state.logicalMin = signedValue(); break;
            case 2: state.logicalMax = state.logicalMin < 0 ? signedValue() : int64_t(value); break;
            case 7:
                if (value > maxReportBits) return;
                state.reportSize = value;
                break;
            case 8:
                if (!value || value > 255) return;
                state.reportID = value;
                reportIDs = true;
                break;
            case 9:
                if (value > maxReportBits) return;
                state.reportCount = value;
                break;
            case 10:
                if (stack.size() >= maxStackDepth) return;
                stack.push_back(state);
                break;
            case 11:
                if (stack.empty()) return;
                state = stack.back();
                stack.pop_back();
                break;
            default: break; // Physical ranges and units do not affect bit layout.
            }
        }
        else if (type == 2) // Local items
        {
            if (tag == 0)
            {
                if (usages.size() >= maxFields) return;
                usages.push_back({usageValue(), usageValue()});
            }
            else if (tag == 1)
            {
                if (pendingUsageMin) return;
                usageMin = usageValue();
                pendingUsageMin = true;
            }
            else if (tag == 2)
            {
                const uint32_t last = usageValue();
                if (!pendingUsageMin || last < usageMin ||
                    (last >> 16) != (usageMin >> 16) || usages.size() >= maxFields) return;
                usages.push_back({usageMin, last});
                pendingUsageMin = false;
            }
            else if (tag == 10)
                return; // Alternative usage sets need explicit support; do not misparse them.
        }
        else if (type == 0) // Main items consume local state
        {
            if (pendingUsageMin) return;
            if (tag == 8 || tag == 9 || tag == 11)
            {
                const unsigned kind = tag == 8 ? 0 : tag == 9 ? 1 : 2;
                const uint64_t bits = uint64_t(state.reportSize) * state.reportCount;
                auto &rs = parsed[state.reportID];
                auto &offset = rs.bitSizes_[kind];
                if (!state.reportSize || !state.reportCount || bits > maxReportBits - offset) return;
                const bool constant = value & 1;
                const bool array = !(value & 2);
                const bool keep = kind == 0 || (kind == 1 ? enableOutput : enableFeature);
                auto &reports = kind == 0 ? rs.inputs_ : kind == 1 ? rs.outputs_ : rs.features_;
                const bool relevant = enableUnknowns || std::any_of(usages.begin(), usages.end(),
                    [](const UsageRange &range) {
                        return (range.first >> 16) == 9 ||
                               (range.first <= 0x10039 && range.last >= 0x10030);
                    });
                if (keep && !constant && !usages.empty() && relevant)
                {
                    std::vector<uint32_t> expanded;
                    for (const auto &range : usages)
                    {
                        const uint64_t count = uint64_t(range.last) - range.first + 1;
                        if (count > maxFields - expanded.size()) return;
                        for (uint64_t u = range.first; u <= range.last; ++u)
                            expanded.push_back(uint32_t(u));
                    }
                    auto add = [&](uint32_t usage, uint32_t bitOffset) {
                        Report r;
                        r.usage_ = usage;
                        r.bitOfs_ = int(bitOffset);
                        r.bits_ = int(state.reportSize);
                        r.min_ = state.logicalMin;
                        r.max_ = state.logicalMax;
                        r.isArray_ = array;
                        r.isNullable_ = value & 0x40;
                        reports.push_back(std::move(r));
                        ++fields;
                    };
                    if (array)
                    {
                        // An array contains selectors, not one flag for each usage.
                        const auto selected = std::find_if(expanded.begin(), expanded.end(),
                            [&](uint32_t u) { return enableUnknowns || supported(u); });
                        if (selected != expanded.end())
                        {
                            if (fields == maxFields || state.reportSize > 32) return;
                            add(*selected, offset);
                            reports.back().count_ = state.reportCount;
                            reports.back().arrayUsages_ = std::move(expanded);
                        }
                    }
                    else
                    {
                        // HID repeats the final usage when there are more fields than usages.
                        if (state.reportCount > maxFields) return;
                        for (uint32_t i = 0; i < state.reportCount; ++i)
                        {
                            const auto u = expanded[std::min<size_t>(i, expanded.size() - 1)];
                            if (!enableUnknowns && !supported(u)) continue;
                            if (fields == maxFields || state.reportSize > 32) return;
                            add(u, offset + i * state.reportSize);
                        }
                    }
                }
                offset += uint32_t(bits); // Independent for each report ID and type.
            }
            else if (tag == 10)
            {
                if (collectionDepth == 0 && !usages.empty()) topUsage = usages.front().first;
                ++collectionDepth;
            }
            else if (tag == 12)
            {
                if (!collectionDepth) return;
                --collectionDepth;
            }
            usages.clear();
            pendingUsageMin = false;
        }
    }
    if (collectionDepth || !stack.empty() || pendingUsageMin) return;
    // Report ID zero is reserved once numbered reports are used.
    if (reportIDs && parsed.count(0)) return;
    for (auto &entry : parsed)
    {
        auto &rs = entry.second;
        std::stable_sort(rs.inputs_.begin(), rs.inputs_.end());
        std::stable_sort(rs.outputs_.begin(), rs.outputs_.end());
        std::stable_sort(rs.features_.begin(), rs.features_.end());
    }
    reportSets_ = std::move(parsed);
    hasReportIDs_ = reportIDs;
    usageLV0_ = topUsage;
    dump();
}

bool HIDInfo::parseReport(const uint8_t *p, size_t size,
                          uint32_t &buttons, int &hat,
                          std::array<int, N_ANALOGS> &analogs) const
{
    buttons = 0;
    hat = -1;
    analogs = {};
    if (!p || !size || reportSets_.empty()) return false;
    const unsigned reportID = hasReportIDs_ ? *p++ : 0;
    if (hasReportIDs_) --size;
    const auto it = reportSets_.find(reportID);
    if (it == reportSets_.end()) return false;
    const auto &rs = it->second;
    if (size < (rs.bitSizes_[0] + 7) / 8) return false;

    const auto readValue = [&](const Report &r, uint32_t slot = 0) -> int64_t {
        const uint32_t offset = uint32_t(r.bitOfs_) + slot * uint32_t(r.bits_);
        uint32_t raw = 0;
        for (unsigned bit = 0; bit < unsigned(r.bits_); ++bit)
            raw |= uint32_t((p[(offset + bit) / 8] >> ((offset + bit) % 8)) & 1) << bit;
        if (r.min_ < 0 && (raw & (uint32_t(1) << (r.bits_ - 1))))
            return int64_t(raw) - (int64_t(1) << r.bits_);
        return raw;
    };
    bool hasControls = false;
    for (const auto &r : rs.inputs_)
    {
        if (r.isConst_) continue;
        if (r.isArray_)
        {
            bool hasButtons = std::any_of(r.arrayUsages_.begin(), r.arrayUsages_.end(),
                [](uint32_t u) { return (u >> 16) == 9 && (u & 0xffff) >= 1 && (u & 0xffff) <= 32; });
            if (!hasButtons) continue;
            hasControls = true;
            for (uint32_t slot = 0; slot < r.count_; ++slot)
            {
                const auto value = readValue(r, slot);
                if (value < r.min_ || value > r.max_) continue;
                const auto index = uint64_t(value - r.min_);
                if (index >= r.arrayUsages_.size()) continue;
                const auto usage = r.arrayUsages_[size_t(index)];
                const unsigned button = usage & 0xffff;
                if ((usage >> 16) == 9 && button >= 1 && button <= 32)
                    buttons |= uint32_t(1) << (button - 1);
            }
        }
        else if (r.isButton())
        {
            const unsigned button = r.usage_ & 0xffff;
            if (button >= 1 && button <= 32)
            {
                hasControls = true;
                if (readValue(r) != 0) buttons |= uint32_t(1) << (button - 1);
            }
        }
        else if (r.isHat())
        {
            hasControls = true;
            const auto value = readValue(r);
            const int64_t directions = r.max_ - r.min_ + 1;
            if (value >= r.min_ && value <= r.max_ && (directions == 4 || directions == 8))
                hat = int((value - r.min_) * 8 / directions);
        }
        else if (int axis = r.getAnalogIndex(); axis >= 0 && r.max_ > r.min_)
        {
            hasControls = true;
            const auto value = std::clamp(readValue(r), r.min_, r.max_);
            analogs[axis] = int((value - r.min_) * 255 / (r.max_ - r.min_));
        }
    }
    return hasControls;
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
