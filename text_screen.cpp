/*
 * author : Shuichi TAKANO
 * since  : Sat Mar 02 2024 20:26:30
 */

#include "text_screen.h"
#include "lcd.h"
#include <algorithm>
#include <cstring>

void TextScreen::fill(int x, int y, Layer layer, int n, char c)
{
    int pt = x + y * WIDTH + static_cast<int>(layer) * LAYER_SIZE;
    n = std::min(n, BUFFER_SIZE - pt);
    memset(&buf_[pt], c, n);
    textChanged_ = true;
}

void TextScreen::print(int x, int y, Layer layer, const char *s)
{
    int pt = x + y * WIDTH + static_cast<int>(layer) * LAYER_SIZE;
    int n = strlen(s);
    n = std::min(n, WIDTH - x);
    memcpy(&buf_[pt], s, n);
    textChanged_ = true;
}

void TextScreen::clearLayer(Layer layer)
{
    auto ofs = static_cast<int>(layer) * LAYER_SIZE;
    memset(&buf_[ofs], layer == Layer::BASE ? ' ' : 0, LAYER_SIZE);
    textChanged_ = true;
}

void TextScreen::clearAll()
{
    clearLayer(Layer::BASE);
    clearLayer(Layer::MAIN);
    clearLayer(Layer::INFO);
}

void TextScreen::setFont(int i, const uint8_t *data)
{
    auto p = &fonts_[i];
    if (p->data != data)
    {
        p->data = data;
        p->dirty = true;
    }
}

void TextScreen::update(int dt_clk)
{
    if (infoLayerClearTimer_ > 0)
    {
        infoLayerClearTimer_ -= dt_clk;
        if (infoLayerClearTimer_ <= 0)
        {
            infoLayerClearTimer_ = 0;
            clearLayer(Layer::INFO);
            if (infoLayerClearCallback_)
            {
                infoLayerClearCallback_();
                infoLayerClearCallback_ = {};
            }
        }
    }

    if (textChanged_ && enableFlip_)
    {
        textChanged_ = false;
        flip();
    }

    enableFlip_ = sendText();
    sendFont();
}

void TextScreen::flip()
{
    currentDBID_ ^= 1;

    // composite
    char *dst = compositeBuf_[currentDBID_];
    auto *src = buf_;
    auto *tail = src + LAYER_SIZE;
    for (; src < tail; ++src, ++dst)
    {
        char l0 = src[0];
        char l1 = src[LAYER_SIZE];
        char l2 = src[LAYER_SIZE * 2];
        char ch = l2 ? l2 : (l1 ? l1 : l0);
        *dst = ch == 160 ? 0 : ch; // '\240' は \0 に変換
    }

    currentTransferOfs_ = 0;
}

bool TextScreen::sendText()
{
    auto &lcd = LCD::instance();

    const char *pOld = compositeBuf_[currentDBID_ ^ 1];
    const char *pNew = compositeBuf_[currentDBID_];

    int i = currentTransferOfs_;
    for (; i < HEIGHT; ++i)
    {
        int ofs = i * WIDTH;
        auto l0 = pOld + ofs;
        auto l1 = pNew + ofs;
        if (memcmp(l0, l1, WIDTH))
        {
            if (!lcd.printNonBlocking(0, i, l1, WIDTH))
            {
                break;
            }
        }
    }
    currentTransferOfs_ = i;
    return currentTransferOfs_ >= HEIGHT;
}

bool TextScreen::sendFont()
{
    auto &lcd = LCD::instance();
    int i = 0;
    for (; i < N_FONT; ++i)
    {
        auto &f = fonts_[i];
        if (f.dirty)
        {
            if (lcd.defineCharNonBlocking(i, f.data))
            {
                f.dirty = false;
            }
            else
            {
                break;
            }
        }
    }
    return i >= N_FONT;
}
