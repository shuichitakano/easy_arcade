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
        };
        return tbl[static_cast<int>(b)];
    };

    void printButtonMessage(PadStateButton b)
    {
        printf("Wait for Button '%s'\n", toString(b));
    }
}

PadManager::PadManager()
{
    using T = PadConfig::Type;
    using AP = PadConfig::AnalogPos;
    using HP = PadConfig::HatPos;
    using B = PadStateButton;
    PadConfig defaultConfig{0, 0, {
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
    switch (mode_)
    {
    case Mode::NORMAL:
        updateNormalMode(cnfButton, cnfButtonTrigger, cnfButtonLong);
        break;

    case Mode::CONFIG:
        updateConfigMode(cnfButton, cnfButtonTrigger, cnfButtonLong);
        break;
    }

    for (int i = 0; i < N_OUTPUT_PORTS; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            auto &re = rotEncoders_[i][j];
            if (int axis = re.getAxis())
            {
                auto v = latestPadData_[i].analogs[axis] - 127;
                re.update(dclk, v);
            }
        }
    }
}

void PadManager::enterNormalMode()
{
    mode_ = Mode::NORMAL;
    printf("to normal mode\n");
    setLED(normalModeLED_);
}

void PadManager::enterConfigMode()
{
    // enter config mode
    configMode_ = {};
    configMode_.curButtonSet_.clear();

    for (int i = 0; i < N_PORTS; ++i)
    {
        configMode_.analogNeutral_[i] = latestPadData_[i].analogs;
        // ボタン押されないと入力が来ないタイプのコントローラーがあるが、
        // 差し込み直後にコンフィグモードに入った場合はニュートラルを取得できない。
        // それは妥協する。
    }

    mode_ = Mode::CONFIG;
    printf("enter config mode\n");

    blinkLED(false, 2);
    setLED(true);

    printButtonMessage(configMode_.curButton_);
    if (printButtonFunc_)
    {
        printButtonFunc_(configMode_.curButton_);
    }
}

void PadManager::blinkLED(bool reverse, int n)
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

void PadManager::updateNormalMode(bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong)
{
    if (cnfButtonLong && enableModeChangeByButton_)
    {
        enterConfigMode();
    }
}

void PadManager::updateConfigMode(bool cnfButton, bool cnfButtonTrigger, bool cnfButtonLong)
{
    if (configMode_.waitFirstIdle_)
    {
        if (cnfButton || cnfButtonTrigger)
        {
            return;
        }
        configMode_.waitFirstIdle_ = false;
    }

    if (cnfButtonLong)
    {
        // ここまでの設定を有効にしてexit
        saveConfigAndExit();
        return;
    }
    else if (cnfButtonTrigger && !configMode_.curButtonSet_.hasData())
    {
        // このボタン設定をスキップ
        printf("skip.\n");
        nextButtonConfig();
    }
}

void PadManager::setData(int port, const PadInput &input)
{
    if (port < 0 || port >= N_PORTS)
    {
        return;
    }

    switch (mode_)
    {
    case Mode::NORMAL:
        setDataNormalMode(port, input);
        break;

    case Mode::CONFIG:
        setDataConfigMode(port, input);
        break;
    }

    latestPadData_[port] = input;
}

void PadManager::setDataNormalMode(int port, const PadInput &input)
{
    padStates_[port].set(translator_,
                         input.vid, input.pid,
                         input.buttons.data(), N_BUTTONS,
                         input.analogs.data(), N_ANALOGS, input.hat);
}

void PadManager::setDataConfigMode(int port, const PadInput &input)
{
    bool anyOn = false;

    ButtonSet cur = configMode_.curButtonSet_;
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
        auto posN = getAnalogPos(configMode_.analogNeutral_[port][i]);
        if (pos != posN)
        {
            cur.analogIndex = i;
            cur.analogOn = pos;
            cur.analogOff = posN;
            anyOn = true;
        }
    }

    if (configMode_.port_ < 0)
    {
        if (anyOn)
        {
            configMode_.port_ = port;
            configMode_.vid_ = input.vid;
            configMode_.pid_ = input.pid;
            DPRINT(("configuring port %d (%04x:%04x)\n", port, input.vid, input.pid));
        }
        else
        {
            return;
        }
    }
    else if (configMode_.port_ != port)
    {
        // 現在config中のポートじゃなかったら無視
        return;
    }

    configMode_.curButtonSet_ = cur;

    if (anyOn)
    {
        if (configMode_.port_ < 0)
        {
            // 最初のボタンが入力された
            configMode_.port_ = port;
            printf("configure port %d...\n", port);
        }
    }
    else
    {
        // 何か入力された後すべて離された
        if (cur.hasData())
        {
            // 次のボタンへ
            nextButtonConfig();
            return;
        }
    }
}

void PadManager::nextButtonConfig()
{
    configMode_.buttonSets_.push_back(configMode_.curButtonSet_);
    configMode_.curButtonSet_.clear();

    configMode_.curButton_ = static_cast<PadStateButton>(static_cast<int>(configMode_.curButton_) + 1);
    if (configMode_.curButton_ == PadStateButton::MAX)
    {
        // おわり
        saveConfigAndExit();
        return;
    }

    int blinkCt = 1;
    switch (configMode_.curButton_)
    {
    case PadStateButton::COIN:
    case PadStateButton::UP:
    case PadStateButton::A:
    case PadStateButton::D:
        blinkCt = 2;
        break;
    }
    blinkLED(true, blinkCt);

    printButtonMessage(configMode_.curButton_);
    if (printButtonFunc_)
    {
        printButtonFunc_(configMode_.curButton_);
    }
}

void PadManager::saveConfigAndExit()
{
    auto &sets = configMode_.buttonSets_;
    if (!sets.empty())
    {
        std::vector<PadConfig::Unit> units;
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
                    u.index = index;
                    u.subIndex = subIndex++;
                    units.push_back(u);
                }
            }
            if (s.analogIndex >= 0)
            {
                PadConfig::Unit u;
                u.type = PadConfig::Type::ANALOG;
                u.number = s.analogIndex;
                u.analogOn = s.analogOn;
                u.analogOff = s.analogOff;
                u.index = index;
                u.subIndex = subIndex++;
                units.push_back(u);
            }
            for (auto hatPos : {PadConfig::HatPos::LEFT, PadConfig::HatPos::RIGHT, PadConfig::HatPos::UP, PadConfig::HatPos::DOWN})
            {
                bool f = testHat(s.hat, hatPos);
                if (f)
                {
                    PadConfig::Unit u;
                    u.type = PadConfig::Type::HAT;
                    u.hatPos = hatPos;
                    u.index = index;
                    u.subIndex = subIndex++;
                    units.push_back(u);
                }
            }
            ++index;
        }

        if (units.empty())
        {
            printf("no entry\n");
        }
        else
        {
            printf("save config\n");
            translator_.append({configMode_.vid_, configMode_.pid_, units, {}});
            if (onSaveFunc_)
            {
                onSaveFunc_();
            }
        }
    }

    blinkLED(true, 3);
    enterNormalMode();
    if (onExitConfigFunc_)
    {
        onExitConfigFunc_();
    }
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
    padStates_[0].setRapidFirePhaseMask(v);
    padStates_[0].setRapidFirePhaseMask(v);
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
