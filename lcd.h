/*
 * author : Shuichi TAKANO
 * since  : Sat Feb 24 2024 22:24:23
 */

#pragma once

#include <hardware/i2c.h>
#include <cstdint>

class LCD
{
public:
    void init(i2c_inst_t *i2c);
    void puts(const char *s);
    void locate(int x, int y);
    void clear();
    void defineChar(int n, const uint8_t *data);
    void setContrast(int v);

    int getAvailableNonBlockingSize();
    void putCharNonBlocking(char ch);
    void locateNonBlocking(int x, int y);
    bool printNonBlocking(int x, int y, const char *s, int n);
    bool defineCharNonBlocking(int n, const uint8_t *data);
    void waitForNonBlocking();

    void setDisplayOnOff(bool on); // non block 待ちする

    static LCD &instance();

protected:
    void writeInstruction(uint8_t cmd);
    void writeData(uint8_t data);

    void writeInstructionNonBlocking(uint8_t cmd);
    void writeDataNonBlocking(uint8_t data);
    void setAddrForNonBlocking();

private:
    i2c_inst_t *i2c_{};
    bool needSetAddr_ = true;

    bool dispOn_ = false;
};