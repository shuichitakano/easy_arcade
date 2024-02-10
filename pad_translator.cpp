/*
 * author : Shuichi TAKANO
 * since  : Sat Sep 09 2023 14:41:26
 */

#include "pad_translator.h"

#include <algorithm>
#include <cstdio>
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

void PadConfig::Unit::dump() const
{
    constexpr char typeTable[] = {'-', 'B', 'A', 'H'};
    constexpr char apTable[] = {'H', 'M', 'L'};
    constexpr char hatTable[] = {'L', 'R', 'U', 'D'};

    printf("%c[%d]:A%c,%c:H%c: %d\n",
           typeTable[(int)type],
           number,
           apTable[(int)analogOn],
           apTable[(int)analogOff],
           hatTable[(int)hatPos],
           index);
}

bool PadConfig::convertButton(int i, uint32_t buttons, const int *analogs, int nAnalogs, int hat) const
{
    if (static_cast<size_t>(i) >= buttons_.size())
    {
        return false;
    }
    const auto &cfg = buttons_[i];

    switch (cfg.type)
    {
    case Type::BUTTON:
        return buttons & (1u << cfg.number);

    case Type::ANALOG:
        return cfg.testAnalog(analogs[cfg.number]);

    case Type::HAT:
        return testHat(hat, cfg.hatPos);

    default:
        break;
    }
    return false;
}

int8_t PadConfig::convertAnalog(int i, uint32_t buttons, const int *analogs, int nAnalogs, int hat) const
{
    // todo
    return 0;
}

PadConfig::PadConfig(int vid, int pid, const std::vector<Unit> &buttons,
                     const std::vector<Unit> &analogs)
    : buttons_(buttons), analogs_(analogs), vid_(vid), pid_(pid)
{
}

void PadConfig::dump() const
{
    if (!buttons_.empty())
    {
        printf("buttons\n");
        for (auto &v : buttons_)
        {
            v.dump();
        }
    }
    if (!analogs_.empty())
    {
        printf("analogs\n");
        for (auto &v : analogs_)
        {
            v.dump();
        }
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

PadConfig *PadTranslator::_find(int vid, int pid)
{
    std::pair<int, int> id{vid, pid};
    auto p = std::partition_point(configs_.begin(), configs_.end(),
                                  [&](const PadConfig &v)
                                  { return v.getDeviceID() < id; });
    if (p != configs_.end() && p->getDeviceID() == id)
    {
        return &*p;
    }
    return nullptr;
}

const PadConfig *PadTranslator::find(int vid, int pid) const
{
    auto p = const_cast<PadTranslator *>(this)->_find(vid, pid);
    return p ? p : &defaultConfig_;
}

void PadTranslator::sort()
{
    std::sort(configs_.begin(), configs_.end(), [&](auto &a, auto &b)
              { return a.getDeviceID() < b.getDeviceID(); });
    printf("sort\n");
    for (auto &v : configs_)
    {
        printf("config %04x, %04x\n", v.getVID(), v.getPID());
    }
}

void PadTranslator::append(PadConfig &&cnf)
{
    cnf.dump();

    if (auto *p = _find(cnf.getVID(), cnf.getPID()))
    {
        DPRINT(("replace config %04x, %04x\n", cnf.getVID(), cnf.getPID()));
        *p = std::move(cnf);
    }
    else
    {
        DPRINT(("new config %04x, %04x\n", cnf.getVID(), cnf.getPID()));
        configs_.push_back(std::move(cnf));
        sort();
    }
}
