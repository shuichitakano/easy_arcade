/*
 * author : Shuichi TAKANO
 * since  : Sat Sep 09 2023 14:41:26
 */

#include "pad_translator.h"

#include <algorithm>
#include <cstdio>
#include "serializer.h"
#include "debug.h"

int getLevel(PadConfig::AnalogPos p)
{
    switch (p)
    {
    default:
    case PadConfig::AnalogPos::H:
        return 255;

    case PadConfig::AnalogPos::MID:
        return 128;

    case PadConfig::AnalogPos::L:
        return 0;
    }
}

bool testHat(int hat, PadConfig::HatPos hatPos)
{
    static constexpr int L = 1 << static_cast<int>(PadConfig::HatPos::LEFT);
    static constexpr int R = 1 << static_cast<int>(PadConfig::HatPos::RIGHT);
    static constexpr int U = 1 << static_cast<int>(PadConfig::HatPos::UP);
    static constexpr int D = 1 << static_cast<int>(PadConfig::HatPos::DOWN);

    static constexpr int table[] = {
        U,
        U | R,
        R,
        D | R,
        D,
        L | D,
        L,
        L | U,
    };

    if (hat >= 0 && hat < 8)
    {
        return table[hat] & (1 << static_cast<int>(hatPos));
    }
    return false;
}

PadConfig::AnalogPos getAnalogPos(int v)
{
    if (v < 85)
    {
        return PadConfig::AnalogPos::L;
    }
    if (v > 170)
    {
        return PadConfig::AnalogPos::H;
    }
    return PadConfig::AnalogPos::MID;
}

bool PadConfig::Unit::testAnalog(uint8_t v) const
{
    int lv0 = getLevel(analogOff);
    int lv1 = getLevel(analogOn);
    int d0 = lv0 - v;
    int d1 = lv1 - v;
    return d0 * d0 > d1 * d1;
}

int PadConfig::Unit::getAnalog(int v) const
{
    int lv0 = getLevel(analogOff);
    int lv1 = getLevel(analogOn);

    return std::clamp((v - lv0) * ANALOG_MAX_VAL / (lv1 - lv0),
                      0, ANALOG_MAX_VAL);
}

void PadConfig::Unit::dump() const
{
    constexpr char typeTable[] = {'-', 'B', 'A', 'H'};
    constexpr char apTable[] = {'H', 'M', 'L'};
    constexpr char hatTable[] = {'L', 'R', 'U', 'D'};

    DPRINT(("%c[%d]:A%c,%c:H%c: %d\n",
            typeTable[(int)type],
            number,
            apTable[(int)analogOn],
            apTable[(int)analogOff],
            hatTable[(int)hatPos],
            index));
}

bool PadConfig::convertButton(int i, const uint32_t *buttons, int nButtons,
                              const int *analogs, int nAnalogs, int hat) const
{
    if (static_cast<size_t>(i) >= buttons_.size())
    {
        return false;
    }
    const auto &cfg = buttons_[i];

    switch (cfg.type)
    {
    case Type::BUTTON:
        return buttons[cfg.number >> 5] & (1u << (cfg.number & 31));

    case Type::ANALOG:
        return cfg.testAnalog(analogs[cfg.number]);

    case Type::HAT:
        return testHat(hat, cfg.hatPos);

    default:
        break;
    }
    return false;
}

std::optional<int>
PadConfig::convertAnalog(int i, const uint32_t *buttons, int nButtons,
                         const int *analogs, int nAnalogs, int hat) const
{
    if (static_cast<size_t>(i) >= analogs_.size())
    {
        return {};
    }
    const auto &cfg = analogs_[i];

    switch (cfg.type)
    {
    case Type::BUTTON:
        return buttons[cfg.number >> 5] & (1u << (cfg.number & 31)) ? ANALOG_MAX_VAL : 0;

    case Type::ANALOG:
        return cfg.getAnalog(analogs[cfg.number]);

    case Type::HAT:
        return testHat(hat, cfg.hatPos) ? ANALOG_MAX_VAL : 0;

    default:
        break;
    }
    return {};
}

PadConfig::PadConfig(int vid, int pid, int outPortOfs,
                     const std::vector<Unit> &buttons,
                     const std::vector<Unit> &analogs)
    : buttons_(buttons), analogs_(analogs), vid_(vid), pid_(pid), outPortOfs_(outPortOfs)
{
}

void PadConfig::dump() const
{
    if (!buttons_.empty())
    {
        DPRINT(("buttons\n"));
        for (auto &v : buttons_)
        {
            v.dump();
        }
    }
    if (!analogs_.empty())
    {
        DPRINT(("analogs\n"));
        for (auto &v : analogs_)
        {
            v.dump();
        }
    }
}

void PadConfig::serialize(Serializer &s) const
{
    s.append16u(vid_);
    s.append16u(pid_);
    s.append8u(outPortOfs_);

    auto writeUnit = [&](const Unit &v)
    {
        s.append8u(static_cast<uint8_t>(v.type));
        s.append8u(v.number);
        s.append8u(static_cast<uint8_t>(v.analogOn));
        s.append8u(static_cast<uint8_t>(v.analogOff));
        s.append8u(static_cast<uint8_t>(v.hatPos));
        s.append8u(v.index);
        s.append8u(v.subIndex);
        s.append8u(v.inPortOfs);
    };

    s.append16u(buttons_.size());
    for (auto &v : buttons_)
    {
        writeUnit(v);
    }

    s.append8u(analogs_.size());
    for (auto &v : analogs_)
    {
        writeUnit(v);
    }
}

PadConfig::PadConfig(Deserializer &s)
{
    vid_ = s.peek16u();
    pid_ = s.peek16u();
    outPortOfs_ = s.peek8u();

    auto readUnit = [&]()
    {
        Unit v;
        v.type = static_cast<Type>(s.peek8u());
        v.number = s.peek8u();
        v.analogOn = static_cast<AnalogPos>(s.peek8u());
        v.analogOff = static_cast<AnalogPos>(s.peek8u());
        v.hatPos = static_cast<HatPos>(s.peek8u());
        v.index = s.peek8u();
        v.subIndex = s.peek8u();
        v.inPortOfs = s.peek8u();
        return v;
    };

    auto n = s.peek16u();
    buttons_.reserve(n);
    for (auto i = 0u; i < n; ++i)
    {
        buttons_.push_back(readUnit());
    }

    n = s.peek8u();
    analogs_.reserve(n);
    for (auto i = 0u; i < n; ++i)
    {
        analogs_.push_back(readUnit());
    }
}

/////////
/////////

PadTranslator::PadTranslator()
{
    /*
        // default pad configs
        using T = PadConfig::Type;
        using AP = PadConfig::AnalogPos;
        using HP = PadConfig::HatPos;

        // CMD LRUD ABCDEF Coin Start
        std::vector<PadConfig> defaultConfigs = {
            // BUFFALO BGC-FC801
            {0x411, 0xc6, {
                              {T::BUTTON, 4, {}, {}, {}, 0},         // CMD
                              {T::ANALOG, 0, AP::L, AP::MID, {}, 1}, // LEFT
                              {T::ANALOG, 0, AP::H, AP::MID, {}, 2}, // RIGHT
                              {T::ANALOG, 1, AP::L, AP::MID, {}, 3}, // UP
                              {T::ANALOG, 1, AP::H, AP::MID, {}, 4}, // DOWN
                              {T::BUTTON, 0, {}, {}, {}, 5},         // A
                              {T::BUTTON, 1, {}, {}, {}, 6},         // B
                              {T::BUTTON, 2, {}, {}, {}, 7},         // C
                              {T::BUTTON, 3, {}, {}, {}, 8},         // D
                              {T::BUTTON, 5, {}, {}, {}, 9},         // E
                              {T::NA, 0, {}, {}, {}, 10},            // F
                              {T::BUTTON, 6, {}, {}, {}, 11},        // COIN
                              {T::BUTTON, 7, {}, {}, {}, 12},        // START
                          },
             {{}}},
            // DualShock4
            {0x54c, 0x9cc, {
                               {T::BUTTON, 2, {}, {}, {}, 0},     // CMD
                               {T::HAT, 0, {}, {}, HP::LEFT, 1},  // LEFT
                               {T::HAT, 0, {}, {}, HP::RIGHT, 2}, // RIGHT
                               {T::HAT, 0, {}, {}, HP::UP, 3},    // UP
                               {T::HAT, 0, {}, {}, HP::DOWN, 4},  // DOWN
                               {T::BUTTON, 0, {}, {}, {}, 5},     // A
                               {T::BUTTON, 1, {}, {}, {}, 6},     // B
                               {T::BUTTON, 2, {}, {}, {}, 7},     // C
                               {T::BUTTON, 3, {}, {}, {}, 8},     // D
                               {T::BUTTON, 4, {}, {}, {}, 9},     // E
                               {T::BUTTON, 5, {}, {}, {}, 10},    // F
                               {T::BUTTON, 8, {}, {}, {}, 11},    // COIN
                               {T::BUTTON, 9, {}, {}, {}, 12},    // START
                           },
             {{}}},
        };

        configs_ = std::move(defaultConfigs);
        sort();
    */
}

void PadTranslator::reset()
{
    configs_.clear();
}

PadConfig *PadTranslator::_find(int vid, int pid, int portOfs)
{
    PadConfig::DeviceID id{vid, pid, portOfs};
    auto p = std::partition_point(configs_.begin(), configs_.end(),
                                  [&](const PadConfig &v)
                                  { return v.getDeviceID() < id; });
    if (p != configs_.end() && p->getDeviceID() == id)
    {
        return &*p;
    }
    return nullptr;
}

const PadConfig *PadTranslator::find(int vid, int pid, int portOfs) const
{
    auto p = const_cast<PadTranslator *>(this)->_find(vid, pid, portOfs);
    return p ? p : &defaultConfig_;
}

void PadTranslator::sort()
{
    std::sort(configs_.begin(), configs_.end(), [&](auto &a, auto &b)
              { return a.getDeviceID() < b.getDeviceID(); });
}

void PadTranslator::append(PadConfig &&cnf, bool buttons, bool analogs)
{
    cnf.dump();

    if (auto *p = _find(cnf.getVID(), cnf.getPID(), cnf.getOutPortOfs()))
    {
        DPRINT(("replace config %04x, %04x, %d\n", cnf.getVID(), cnf.getPID(), cnf.getOutPortOfs()));
        if (buttons && analogs)
        {
            *p = std::move(cnf);
        }
        else if (buttons)
        {
            p->setButtonUnits(std::move(cnf.getButtonUnits()));
        }
        else if (analogs)
        {
            p->setAnalogUnits(std::move(cnf.getAnalogUnits()));
        }
    }
    else
    {
        DPRINT(("new config %04x, %04x, %d\n", cnf.getVID(), cnf.getPID(), cnf.getOutPortOfs()));
        configs_.push_back(std::move(cnf));
        sort();
    }
}

void PadTranslator::serialize(Serializer &s) const
{
    s.append(configs_.size());

    int n = 0;
    for (auto &v : configs_)
    {
        if (s.exceedLimit())
        {
            s.append8u(0);
        }
        s.append8u(1); // enabled
        v.serialize(s);
        ++n;
    }
    DPRINT(("store %d/%d configs.\n", n, configs_.size()));
}

void PadTranslator::deserialize(Deserializer &s)
{
    configs_.clear();
    auto nn = s.peek();

    DPRINT(("%d configs...\n", nn));

    int n = 0;
    while (n < nn && s.peek8u())
    {
        PadConfig cnf(s);
        configs_.push_back(std::move(cnf));
        ++n;
    }

    DPRINT(("load %d/%d configs.\n", n, nn));
    sort();
}
