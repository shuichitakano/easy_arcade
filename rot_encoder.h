/*
 * author : Shuichi TAKANO
 * since  : Sun Jun 09 2024 02:12:55
 */
#pragma once

#include <utility>
#include <cstdint>

class RotEncoder
{
public:
    void update(int dclk, int velocity);

    std::pair<bool, bool> getEncState() const;
    uint32_t overrideButton(uint32_t buttons, int bitA, int bitB) const;

    void setAxis(int axis) { axis_ = axis; }
    int getAxis() const { return axis_; }
    void setScale(int scale) { scale_ = scale; }

    explicit operator bool() const { return axis_ >= 0; }

private:
    int axis_ = -1;
    int scale_ = 1;

    int rct_ = 0;
    int state_ = 0; // 4相で変化する
    // A: 0011
    // B: 1001
};
