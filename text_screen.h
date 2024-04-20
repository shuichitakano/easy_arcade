/*
 * author : Shuichi TAKANO
 * since  : Sat Mar 02 2024 20:25:50
 */
#pragma once

#include <cstdint>
#include <array>
#include <functional>

class TextScreen
{
public:
    static constexpr int WIDTH = 8;
    static constexpr int HEIGHT = 2;
    static constexpr int LAYERS = 3;
    static constexpr int LAYER_SIZE = WIDTH * HEIGHT;
    static constexpr int BUFFER_SIZE = WIDTH * HEIGHT * LAYERS;
    static constexpr int N_FONT = 6;

    enum class Layer
    {
        BASE,
        MAIN,
        INFO,
    };

public:
    void fill(int x, int y, Layer layer, int n, char c);
    void print(int x, int y, Layer layer, const char *s);
    void clearLayer(Layer layer);
    void clearAll();
    void flip();

    void printInfo(int x, int y, const char *s) { print(x, y, Layer::INFO, s); }
    void printBase(int x, int y, const char *s) { print(x, y, Layer::BASE, s); }
    void printMain(int x, int y, const char *s) { print(x, y, Layer::MAIN, s); }

    void setFont(int i, const uint8_t *data);

    void setInfoLayerClearTimer(int clk) { infoLayerClearTimer_ = clk; }
    void setInfoLayerClearCallback(std::function<void()> cb) { infoLayerClearCallback_ = cb; }

    void update(int dt_clk);

    bool isInfoActive() const { return infoLayerClearTimer_ > 0; }

protected:
    bool sendText();
    bool sendFont();

private:
    char buf_[BUFFER_SIZE]{};
    bool textChanged_ = true;
    bool enableFlip_ = true;

    char compositeBuf_[2][LAYER_SIZE]{};
    int currentDBID_ = 0;
    int currentTransferOfs_ = 0;

    struct FontInfo
    {
        const uint8_t *data{};
        bool dirty = true;
    };
    std::array<FontInfo, N_FONT> fonts_;

    int infoLayerClearTimer_ = 0;
    std::function<void()> infoLayerClearCallback_;
};