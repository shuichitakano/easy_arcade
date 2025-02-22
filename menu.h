/*
 * author : Shuichi TAKANO
 * since  : Thu Mar 07 2024 05:00:21
 */

#pragma once

#include "text_screen.h"
#include <string>
#include <functional>
#include <vector>
#include <utility>

class Menu
{
public:
    using ActionFunc = std::function<void(Menu &)>;
    using OpenCloseFunc = std::function<void(bool)>;
    using ValueFormatFunc = std::function<void(char *, size_t, int)>;
    using ConditionFunc = std::function<bool()>;

    struct MenuItem
    {
        const char *name;
        int *value{};
        std::pair<int, int> range{};
        const char **valueTexts{};
        const char *valueText{};
        // const char *valueFormat{};
        ValueFormatFunc valueFormat;
        bool menuButton = false; // onButton でMenuボタンを見る

        ActionFunc onValueChange;
        ActionFunc onButton;
        ConditionFunc conditionFunc;

        MenuItem &setConditionFunc(ConditionFunc f)
        {
            conditionFunc = f;
            return *this;
        }
    };

public:
    Menu(TextScreen *textScreen)
        : textScreen_(textScreen)
    {
    }

    MenuItem &append(const char *name, int *value,
                     const char **valueTexts, size_t nValueTexts,
                     ActionFunc onValueChange = {},
                     ActionFunc onButton = {},
                     bool menuButton = false)
    {
        MenuItem item;
        item.name = name;
        item.value = value;
        item.valueTexts = valueTexts;
        item.range = {0, static_cast<int>(nValueTexts) - 1};
        item.onValueChange = onValueChange;
        item.onButton = onButton;
        item.menuButton = menuButton;
        items_.push_back(item);

        return items_.back();
    }

    MenuItem &append(const char *name, int *value,
                     std::pair<int, int> range,
                     //                const char *valueFormat,
                     ValueFormatFunc valueFormat,
                     ActionFunc onValueChange = {},
                     ActionFunc onButton = {},
                     bool menuButton = false)
    {
        MenuItem item;
        item.name = name;
        item.value = value;
        item.range = range;
        item.valueFormat = valueFormat;
        item.onValueChange = onValueChange;
        item.onButton = onButton;
        item.menuButton = menuButton;
        items_.push_back(item);

        return items_.back();
    }

    MenuItem &append(const char *name, const char *text, ActionFunc onButton = {}, bool menuButton = false)
    {
        //        append(name, {}, {}, text, {}, onButton, menuButton);
        MenuItem item;
        item.name = name;
        item.valueText = text;
        item.onButton = onButton;
        item.menuButton = menuButton;
        items_.push_back(item);

        return items_.back();
    }

    void refresh();
    void update(int dclk,
                uint32_t pad,
                bool menuButtonPress,
                bool menuButtonRelease,
                bool menuButtonLongPress);
    void render() const;

    bool isOpened() const { return open_; }
    void setBlinkInterval(int interval) { blinkInterval_ = interval; }

    void setOpenCloseFunc(OpenCloseFunc f);
    void forceClose()
    {
        open_ = false;
        openLock_ = true;
    }

    bool isChanged() const { return changed_; }
    void clearChanged() { changed_ = false; }

    std::pair<uint32_t, uint32_t> getPad() const { return {pad_, padPrev_}; }

protected:
    void nextItem(bool inc);

private:
    TextScreen *textScreen_;
    TextScreen::Layer layer_ = TextScreen::Layer::MAIN;

    std::vector<MenuItem> items_;

    bool open_ = false;
    int currentItem_ = -1;
    uint32_t pad_ = 0;
    uint32_t padPrev_ = 0;

    int blinkCounter_ = 0;
    int blinkInterval_ = 1;
    bool blinkPhase_ = 0;

    OpenCloseFunc onOpenClose_;
    bool changed_ = false;

    bool openLock_ = false;
};
