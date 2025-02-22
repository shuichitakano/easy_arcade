/*
 * author : Shuichi TAKANO
 * since  : Sun Jan 14 2024 03:14:17
 */

#include "pad_manager.h"
#include <pico/stdlib.h>
#include <cstdio>
#include <algorithm>
#include "led.h"
#include "debug.h"

namespace
{
    const char *toString(PadStateButton b)
    {
        constexpr const char *tbl[] = {
            "CMD",
            "COIN",
            "START",
            "UP",
            "DOWN",
            "LEFT",
            "RIGHT",
            "A",
            "B",
            "C",
            "D",
            "E",
            "F",
            "COIN2",
            "START2",
            "UP2",
            "DOWN2",
            "LEFT2",
            "RIGHT2",
            "A2",
            "B2",
            "C2",
            "D2",
            "E2",
            "F2",
        };
        auto i = static_cast<size_t>(b);
        assert(i < std::size(tbl));
        return tbl[i];
    };

    const char *toString(PadConfigAnalog v)
    {
        constexpr const char *tbl[] = {
            "H0",
            "L0",
            "H1",
            "L1",
            "H2",
            "L2",
            "H3",
            "L3",
        };
        auto i = static_cast<size_t>(v);
        assert(i < std::size(tbl));
        return tbl[i];
    };

}

PadManager::PadManager()
{
    using T = PadConfig::Type;
    using AP = PadConfig::AnalogPos;
    using HP = PadConfig::HatPos;
    using B = PadStateButton;
    PadConfig defaultConfig{0, 0, 0, {
                                         {T::ANALOG, 0, AP::L, AP::MID, {}, static_cast<int>(B::LEFT)},
                                         {T::ANALOG, 0, AP::H, AP::MID, {}, static_cast<int>(B::RIGHT)},
                                         {T::ANALOG, 1, AP::L, AP::MID, {}, static_cast<int>(B::UP)},
                                         {T::ANALOG, 1, AP::H, AP::MID, {}, static_cast<int>(B::DOWN)},
                                         {T::HAT, 0, {}, {}, HP::LEFT, static_cast<int>(B::LEFT)},
                                         {T::HAT, 0, {}, {}, HP::RIGHT, static_cast<int>(B::RIGHT)},
                                         {T::HAT, 0, {}, {}, HP::UP, static_cast<int>(B::UP)},
                                         {T::HAT, 0, {}, {}, HP::DOWN, static_cast<int>(B::DOWN)},
                                         {T::BUTTON, 0, {}, {}, {}, static_cast<int>(B::A)},
                                         {T::BUTTON, 1, {}, {}, {}, static_cast<int>(B::B)},
                                         {T::BUTTON, 2, {}, {}, {}, static_cast<int>(B::COIN)},
                                         {T::BUTTON, 3, {}, {}, {}, static_cast<int>(B::START)},
                                         {T::BUTTON, 4, {}, {}, {}, static_cast<int>(B::C)},
                                         {T::BUTTON, 5, {}, {}, {}, static_cast<int>(B::D)},
                                         {T::BUTTON, 6, {}, {}, {}, static_cast<int>(B::E)},
                                         {T::BUTTON, 7, {}, {}, {}, static_cast<int>(B::F)},
                                     },
                            {}};

    translator_.setDefaultConfig(std::move(defaultConfig));
}

void PadManager::reset()
{
    padStates_[0].reset();
    padStates_[1].reset();
}

void PadManager::update(int dclk, bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong)
{
    if (modeHandler_)
    {
        modeHandler_->update(*this, dclk, cnfButton, cnfButtonTrigger, cnfButtonLong);
    }
    else
    {
        if (cnfButtonLong && enableModeChangeByButton_)
        {
            enterConfigMode();
        }
    }

    for (int i = 0; i < N_OUTPUT_PORTS; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            auto &re = rotEncoders_[i][j];
            if (re)
            {
                int axis = re.getAxis();
                auto v = (latestPadData_[i].analogs[axis] >> (ANALOG_BITS - 8)) - 127;
                re.update(dclk, v);
            }
        }
    }
}

void PadManager::enterNormalMode()
{
    modeHandler_.reset();

    printf("to normal mode\n");
    setLED(normalModeLED_);
}

void PadManager::enterConfigMode()
{
    modeHandler_ = std::make_unique<ButtonConfigMode>();
    modeHandler_->init(*this);
    printf("enter config mode\n");
}

void PadManager::enterAnalogConfigMode()
{
    modeHandler_ = std::make_unique<AnalogConfigMode>();
    modeHandler_->init(*this);
    printf("enter ANALOG config mode\n");
}

void PadManager::blinkLED(bool reverse, int n) const
{
    if (enableLED_)
    {
        for (int i = 0; i < n; ++i)
        {
            setLED(true ^ reverse);
            sleep_ms(200);
            setLED(false ^ reverse);
            sleep_ms(200);
        }
    }
}

void PadManager::setData(int port, const PadInput &input)
{
    if (port < 0 || port >= N_PORTS)
    {
        return;
    }

    if (modeHandler_)
    {
        modeHandler_->setData(*this, port, input);
    }
    else
    {
        int n = twinPortMode_ ? 2 : 1;
        for (int i = 0; i < n; ++i)
        {
            int p = port + i;
            if (p < N_OUTPUT_PORTS)
            {
                padStates_[p].set(translator_,
                                  input.vid, input.pid, i,
                                  input.buttons.data(), N_BUTTONS,
                                  input.analogs.data(), N_ANALOGS, input.hat);
            }
        }
    }

    latestPadData_[port] = input;
}

void PadManager::setVSyncCount(int count)
{
    for (auto &s : padStates_)
    {
        s.setVSyncCount(count);
    }
}

uint32_t
PadManager::_getButtons(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return 0;
    }
    if (port == 0)
    {
        return padStates_[static_cast<int>(StateKind::PORT0)].getButtons() |
               padStates_[static_cast<int>(StateKind::MIDI)].getButtons();
    }
    else
    {
        return padStates_[port].getButtons();
    }
}

uint32_t
PadManager::getButtons(int port) const
{
    auto v = _getButtons(port);

    auto &rotEncs = rotEncoders_[port];
    if (rotEncs[0])
    {
        v = rotEncs[0].overrideButton(v,
                                      static_cast<int>(PadStateButton::LEFT),
                                      static_cast<int>(PadStateButton::RIGHT));
    }
    if (rotEncs[1])
    {
        v = rotEncs[1].overrideButton(v,
                                      static_cast<int>(PadStateButton::UP),
                                      static_cast<int>(PadStateButton::DOWN));
    }
    return v;
}

uint32_t
PadManager::getNonRapidButtons(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return {};
    }
    if (port == 0)
    {
        auto s0 = padStates_[static_cast<int>(StateKind::PORT0)].getNonRapidButtons();
        auto s1 = padStates_[static_cast<int>(StateKind::MIDI)].getNonRapidButtons();
        return s0 | s1;
    }
    else
    {
        return padStates_[port].getNonRapidButtons();
    }
}

std::array<uint32_t, 2>
PadManager::getNonRapidButtonsEachRapidPhase(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return {};
    }
    return padStates_[port].getNonRapidButtonsEachRapidPhase();
}

uint32_t
PadManager::getRapidFireMask(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return 0;
    }
    return padStates_[port].getRapidFireMask();
}

uint32_t
PadManager::getNonMappedRapidFireMask(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return 0;
    }
    return padStates_[port].getNonMappedRapidFireMask();
}

void PadManager::setNonMappedRapidFireMask(int port, uint32_t v)
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return;
    }
    padStates_[port].setNonMappedRapidFireMask(v);
}

int PadManager::getRapidFireDiv(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return 0;
    }
    return padStates_[port].getRapidFireDiv();
}

void PadManager::setRapidFireDiv(int port, int v)
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        return;
    }
    padStates_[port].setRapidFireDiv(v);
}

void PadManager::setRapidFirePhaseMask(uint32_t v)
{
    for (auto &s : padStates_)
    {
        s.setRapidFirePhaseMask(v);
    }
}

void PadManager::serialize(Serializer &s) const
{
    translator_.serialize(s);
}

void PadManager::deserialize(Deserializer &s)
{
    translator_.deserialize(s);
}

void PadManager::setLED(bool on) const
{
    if (enableLED_)
    {
        ::setLED(on);
    }
}

void PadManager::setRotEncSetting(int kind, int axis, int scale)
{
    for (int i = 0; i < N_OUTPUT_PORTS; ++i)
    {
        auto &re = rotEncoders_[i][kind];
        re.setAxis(axis);
        re.setScale(scale);
    }
}

const PadState::AnalogState &
PadManager::getAnalogState(int port) const
{
    if (port < 0 || port >= N_OUTPUT_PORTS)
    {
        port = 0;
    }
    return padStates_[port].getAnalogState();
}

////////////
// Button Config Mode
void PadManager::ButtonConfigMode::init(PadManager &mgr)
{
    curButton_ = {};

    for (int i = 0; i < N_PORTS; ++i)
    {
        analogNeutral_[i] = mgr.latestPadData_[i].analogs;
        // ボタン押されないと入力が来ないタイプのコントローラーがあるが、
        // 差し込み直後にコンフィグモードに入った場合はニュートラルを取得できない。
        // それは妥協する。
    }

    mgr.blinkLED(false, 2);

    printMessage(mgr);
    mgr.setLED(true);
}

void PadManager::ButtonConfigMode::printMessage(const PadManager &mgr) const
{
    auto bt = static_cast<PadStateButton>(curButton_);
    printf("Wait for Button '%s'\n", toString(bt));

    if (mgr.printButtonFunc_)
    {
        mgr.printButtonFunc_(bt);
    }

    bool reverse = false;
    int blinkCt = 1;
    switch (bt)
    {
    case PadStateButton::CMD:
        reverse = true;
        blinkCt = 2;
        break;

    case PadStateButton::COIN:
    case PadStateButton::UP:
    case PadStateButton::A:
    case PadStateButton::D:
        blinkCt = 2;
        break;
    }
    mgr.blinkLED(reverse, blinkCt);
}

void PadManager::ButtonConfigMode::update(PadManager &mgr, int dclk, bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong)
{
    if (waitFirstIdle_)
    {
        if (cnfButton || cnfButtonTrigger)
        {
            return;
        }
        waitFirstIdle_ = false;
    }

    if (cnfButtonLong)
    {
        // ここまでの設定を有効にしてexit
        saveAndExit(mgr);
        return;
    }
    else if (cnfButtonTrigger && !curButtonSet_.hasData())
    {
        // このボタン設定をスキップ
        printf("skip.\n");
        next(mgr);
    }

    if (!anyButtonOn_ && curButtonSet_.hasData())
    {
        // 何か入力された後、すべて離された状態でしばらく経過したら次のボタンへ
        auto &ct = idleCycle_;
        ct += dclk;
        if (ct > (uint32_t)(0.1f * CPU_CLOCK))
        {
            ct = 0;
            next(mgr);
            return;
        }
    }
}

void PadManager::ButtonConfigMode::setData(PadManager &mgr, int port, const PadInput &input)
{
    bool anyOn = false;

    ButtonSet cur = curButtonSet_;
    for (auto i = 0u; i < input.buttons.size(); ++i)
    {
        cur.buttons[i] |= input.buttons[i];
        anyOn |= input.buttons[i] != 0;
    }

    if (input.hat >= 0)
    {
        cur.hat = input.hat;
        anyOn = true;
    }

    for (int i = 0; i < N_ANALOGS; ++i)
    {
        auto pos = getAnalogPos(input.analogs[i]);
        auto posN = getAnalogPos(analogNeutral_[port][i]);
        if (pos != posN)
        {
            cur.analogIndex = i;
            cur.analogOn = pos;
            cur.analogOff = posN;
            anyOn = true;
        }
    }

    if (port_ < 0)
    {
        if (anyOn)
        {
            port_ = port;
            vid_ = input.vid;
            pid_ = input.pid;
            DPRINT(("configuring port %d (%04x:%04x)\n", port, input.vid, input.pid));
        }
        else
        {
            return;
        }
    }
    else if (port_ != port)
    {
        // 現在config中のポートじゃなかったら無視
        return;
    }

    curButtonSet_ = cur;
    anyButtonOn_ = anyOn;

    if (anyOn)
    {
        if (port_ < 0)
        {
            // 最初のボタンが入力された
            port_ = port;
            printf("configure port %d...\n", port);
        }
    }
}

int PadManager::ButtonConfigMode::nButtons(const PadManager &mgr) const
{
    auto r = mgr.twinPortMode_ ? PadStateButton::MAX_2P : PadStateButton::MAX;
    return static_cast<int>(r);
}

void PadManager::ButtonConfigMode::next(PadManager &mgr)
{
    buttonSets_.push_back(curButtonSet_);
    curButtonSet_.clear();

    curButton_ = curButton_ + 1;
    if (curButton_ == nButtons(mgr))
    {
        // おわり
        saveAndExit(mgr);
        return;
    }

    printMessage(mgr);
}

void PadManager::ButtonConfigMode::appendUnit(const PadManager &mgr,
                                              std::vector<PadConfig::Unit> units[2],
                                              PadConfig::Unit &u,
                                              int index)
{
    if (index < N_PAD_STATE_BUTTONS)
    {
        u.index = index;
        units[0].push_back(u);
        if (index == static_cast<int>(PadStateButton::CMD) &&
            mgr.twinPortMode_)
        {
            // CMDは共通で入れる
            units[1].push_back(u);
        }
    }
    else if (mgr.twinPortMode_)
    {
        u.index = index - N_PAD_STATE_BUTTONS + 1;
        // CMDないので+1
        units[1].push_back(u);
    }
}

void PadManager::ButtonConfigMode::registerUnits(PadManager &mgr,
                                                 std::vector<PadConfig::Unit> units[2])
{
    for (int i = 0; i < 2; ++i)
    {
        if (!units[i].empty())
        {
            mgr.translator_.append({vid_, pid_, i, units[i], {}}, true, false);
        }
    }
}

void PadManager::ButtonConfigMode::saveAndExit(PadManager &mgr)
{
    auto &sets = buttonSets_;
    if (!sets.empty())
    {
        std::vector<PadConfig::Unit> units[2]; // 1P, 2P同時設定する事がある
        int index = 0;
        for (auto &s : sets)
        {
            int subIndex = 0;
            for (int i = 0; i < N_BUTTONS; ++i)
            {
                if (s.getButton(i))
                {
                    PadConfig::Unit u;
                    u.type = PadConfig::Type::BUTTON;
                    u.number = i;
                    u.subIndex = subIndex++;
                    appendUnit(mgr, units, u, index);
                }
            }
            if (s.analogIndex >= 0)
            {
                PadConfig::Unit u;
                u.type = PadConfig::Type::ANALOG;
                u.number = s.analogIndex;
                u.analogOn = s.analogOn;
                u.analogOff = s.analogOff;
                u.subIndex = subIndex++;
                appendUnit(mgr, units, u, index);
            }
            for (auto hatPos :
                 {PadConfig::HatPos::LEFT, PadConfig::HatPos::RIGHT,
                  PadConfig::HatPos::UP, PadConfig::HatPos::DOWN})
            {
                bool f = testHat(s.hat, hatPos);
                if (f)
                {
                    PadConfig::Unit u;
                    u.type = PadConfig::Type::HAT;
                    u.hatPos = hatPos;
                    u.subIndex = subIndex++;
                    appendUnit(mgr, units, u, index);
                }
            }
            ++index;
        }

        if (units[0].empty() && units[1].empty())
        {
            DPRINT(("no entry\n"));
        }
        else
        {
            DPRINT(("save config\n"));
            registerUnits(mgr, units);
            if (mgr.onSaveFunc_)
            {
                mgr.onSaveFunc_();
            }
        }
    }

    mgr.blinkLED(true, 3);
    if (mgr.onExitConfigFunc_)
    {
        mgr.onExitConfigFunc_();
    }
    mgr.enterNormalMode();
}

/////////

void PadManager::AnalogConfigMode::printMessage(const PadManager &mgr) const
{
    auto bt = static_cast<PadConfigAnalog>(curButton_);
    printf("Wait for Analog '%s'\n", toString(bt));

    if (mgr.printCnfAnalogFunc_)
    {
        mgr.printCnfAnalogFunc_(bt);
    }
}

int PadManager::AnalogConfigMode::nButtons(const PadManager &mgr) const
{
    return mgr.analogMode_ == AppConfig::AnalogMode::_1P_ONLY_4CH ? 8 : 4;
}

void PadManager::AnalogConfigMode::appendUnit(const PadManager &mgr,
                                              std::vector<PadConfig::Unit> units[2],
                                              PadConfig::Unit &u,
                                              int index)
{
    assert(index < N_PAD_CONFIG_ANALOGS);
    u.index = index;
    units[0].push_back(u);
}

void PadManager::AnalogConfigMode::registerUnits(PadManager &mgr,
                                                 std::vector<PadConfig::Unit> units[2])
{
    mgr.translator_.append({vid_, pid_, 0, {}, units[0]}, false, true);
}

void PadManager::AnalogConfigMode::next(PadManager &mgr)
{
    // ハードウェア的にアナログデジタル両方の値を出すデバイス対策として
    // Analog がある時はデジタル系無効にする
    curButtonSet_.filterForAnalog();

    ButtonConfigMode::next(mgr);
}
