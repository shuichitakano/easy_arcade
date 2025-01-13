/*
 * author : Shuichi TAKANO
 * since  : Sat Sep 09 2023 14:39:32
 */
#pragma once

#include <cstdint>
#include <vector>
#include <tuple>

class Serializer;
class Deserializer;

class PadConfig
{
public:
    enum class Type : uint8_t
    {
        NA,
        BUTTON,
        ANALOG,
        HAT,
    };

    enum class AnalogPos : uint8_t
    {
        H,
        MID,
        L,
    };

    enum class HatPos : uint8_t
    {
        LEFT,
        RIGHT,
        UP,
        DOWN,

        INVALID,
    };

    struct Unit
    {
        Type type = Type::BUTTON;
        uint8_t number = 0;
        AnalogPos analogOn{};
        AnalogPos analogOff{};
        HatPos hatPos{};

        uint8_t index = 0;     // 出力ボタン、アナログ番号
        uint8_t subIndex = 0;  // 同一ボタン出力の中でのサブインデックス
        uint8_t inPortOfs = 0; // 入力ポートオフセット

    public:
        bool testAnalog(uint8_t v) const;

        auto makeTie() const
        {
            return std::tie(type, number, analogOn, analogOff, hatPos, index, subIndex, inPortOfs);
        };
        friend bool operator<(const Unit &a, const Unit &b)
        {
            return a.makeTie() < b.makeTie();
        }
        friend bool operator==(const Unit &a, const Unit &b)
        {
            return a.makeTie() == b.makeTie();
        }

        void dump() const;
    };

    using DeviceID = std::tuple<uint16_t, uint16_t, uint8_t>;

public:
    PadConfig() = default;
    PadConfig(int vid, int pid, int outPortOfs,
              const std::vector<Unit> &buttons, const std::vector<Unit> &analogs);
    PadConfig(Deserializer &s);

    bool convertButton(int i, const uint32_t *buttons, int nButtons, const int *analogs, int nAnalogs, int hat) const;
    int8_t convertAnalog(int i, const uint32_t *buttons, int nButtons, const int *analogs, int nAnalogs, int hat) const;

    size_t getButtonCount() const { return buttons_.size(); }
    size_t getAnalogCount() const { return analogs_.size(); }

    const Unit &getButtonUnit(size_t i) const { return buttons_[i]; }
    const Unit &getAnalogUnit(size_t i) const { return analogs_[i]; }

    int getVID() const { return vid_; }
    int getPID() const { return pid_; }
    int getOutPortOfs() const { return outPortOfs_; }

    DeviceID getDeviceID() const { return {vid_, pid_, outPortOfs_}; }

    void serialize(Serializer &s) const;
    void dump() const;

private:
    std::vector<Unit> buttons_;
    std::vector<Unit> analogs_;

    uint16_t vid_ = 0;
    uint16_t pid_ = 0;
    uint8_t outPortOfs_ = 0; // 出力ポートオフセット
};

int getLevel(PadConfig::AnalogPos p);
bool testHat(int hat, PadConfig::HatPos hatPos);
PadConfig::AnalogPos getAnalogPos(int v);

// デバイスIDと対応する設定を管理
class PadTranslator
{
    std::vector<PadConfig> configs_;
    PadConfig defaultConfig_;

public:
    PadTranslator();

    void setDefaultConfig(PadConfig &&cnf) { defaultConfig_ = std::move(cnf); }
    void append(PadConfig &&cnf);
    const PadConfig *find(int vid, int pid, int portOfs = 0) const;

    void serialize(Serializer &s) const;
    void deserialize(Deserializer &s);

    void reset();

protected:
    PadConfig *_find(int vid, int pid, int portOfs = 0);

    void sort();
};
