/*
 * author : Shuichi TAKANO
 * since  : Sat Mar 09 2024 05:30:23
 */

#include "menu.h"
#include "pad_state.h"
#include "font.h"

void Menu::setOpenCloseFunc(OpenCloseFunc f)
{
    onOpenClose_ = f;
    if (f)
    {
        f(open_);
    }
}

void Menu::refresh()
{
    textScreen_->clearLayer(layer_);
    if (onOpenClose_)
    {
        onOpenClose_(open_);
    }

    if (open_)
    {
        textScreen_->setFont(0, getLeftRightFont());
        textScreen_->setFont(1, getUpDownFont());
    }
}

void Menu::update(int dclk,
                  uint32_t pad,
                  bool menuButtonPress,
                  bool menuButtonRelease,
                  bool menuButtonLongPress)
{
    blinkCounter_ += dclk;
    if (blinkCounter_ >= blinkInterval_)
    {
        blinkCounter_ -= blinkInterval_;
        blinkPhase_ ^= true;
    }

    pad_ = pad;

    auto testButton = [&](PadStateButton b)
    {
        return ((pad ^ padPrev_) & pad) & (1u << static_cast<int>(b));
    };

    if (!openLock_ && menuButtonRelease)
    {
        open_ = !open_;
        currentItem_ = 0;

        refresh();
    }
    else if (open_)
    {
        auto &item = items_[currentItem_];

        if (testButton(PadStateButton::UP))
        {
            nextItem(false);
        }
        else if (testButton(PadStateButton::DOWN))
        {
            nextItem(true);
        }
        else if (testButton(PadStateButton::LEFT))
        {
            if (item.value)
            {
                *item.value = std::max(item.range.first, *item.value - 1);
                if (item.onValueChange)
                {
                    item.onValueChange(*this);
                }
                changed_ = true;
            }
        }
        else if (testButton(PadStateButton::RIGHT))
        {
            if (item.value)
            {
                *item.value = std::min(item.range.second, *item.value + 1);
                if (item.onValueChange)
                {
                    item.onValueChange(*this);
                }
                changed_ = true;
            }
        }
        else
        {
            if (item.onButton &&
                ((item.menuButton && menuButtonLongPress) ||
                 (!item.menuButton && testButton(PadStateButton::A))))
            {
                item.onButton(*this);
                changed_ = true;
            }
        }
    }

    // 長押し操作でのメニュークローズ後、離した時にメニューが開くのを防ぐ
    openLock_ &= menuButtonPress;

    padPrev_ = pad;
}

void Menu::render() const
{
    if (!open_)
    {
        return;
    }
    textScreen_->clearLayer(layer_);

    auto &item = items_[currentItem_];
    textScreen_->print(0, 0, layer_, item.name);

    if (item.valueTexts)
    {
        textScreen_->print(0, 1, layer_, item.valueTexts[*item.value]);
    }
    else if (item.valueFormat)
    {
        char buf[16];
        // snprintf(buf, sizeof(buf), item.valueFormat, *item.value);
        item.valueFormat(buf, sizeof(buf), *item.value);
        textScreen_->print(0, 1, layer_, buf);
    }
    else if (item.valueText)
    {
        textScreen_->print(0, 1, layer_, item.valueText);
    }

    if (blinkPhase_)
    {
        if (item.value)
        {
            textScreen_->print(7, 1, layer_, "\240");
        }
        textScreen_->print(7, 0, layer_, "\1");
    }
}

void Menu::nextItem(bool inc)
{
    int dir = inc ? 1 : -1;
    auto prev = currentItem_;

    do
    {
        currentItem_ = (currentItem_ + dir + items_.size()) % items_.size();
        auto &item = items_[currentItem_];
        if (item.conditionFunc)
        {
            if (item.conditionFunc())
            {
                break;
            }
        }
        else
        {
            break;
        }
    } while (currentItem_ != prev);
}
