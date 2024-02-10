/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 04 2024 02:20:37
 */

#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <array>

class HIDInfo
{
public:
    static constexpr int N_ANALOGS = 9;

    struct Report
    {
        uint32_t usage_ = 0;
        int bitOfs_ = 0;
        int bits_ = 1;
        int min_ = 0;
        int max_ = 0;
        bool isConst_ = false;
        bool isArray_ = false;
        bool isNullable_ = false;

        bool isButton() const { return (usage_ >> 16) == 0x09; }
        bool isHat() const { return usage_ == 0x00010039; }
        int getAnalogIndex() const
        {
            // X, Y, Z, RX, RY, RZ, SLIDER, DIAL, WHEEL
            if (usage_ >= 0x00010030 && usage_ <= 0x00010038)
            {
                return usage_ - 0x00010030;
            }
            return -1;
        }

        void dump() const;
        friend bool operator<(const Report &a, const Report &b)
        {
            auto makeTie = [](const Report &r)
            {
                return std::tie(r.usage_, r.bitOfs_);
            };
            return makeTie(a) < makeTie(b);
        }
    };

    struct ReportSet
    {
        std::vector<Report> inputs_;
        std::vector<Report> outputs_;
        std::vector<Report> features_;
        // usage でソート

        void dump() const;
    };

    int usageLV0_ = 0;

    int vid_ = 0;
    int pid_ = 0;

public:
    void parseDesc(const uint8_t *p, const uint8_t *tail,
                   bool enableUnknowns = false,
                   bool enableOutput = false, bool enableFeature = false);

    void parseReport(const uint8_t *p, size_t size,
                     uint32_t &buttons,
                     int &hat,
                     std::array<int, N_ANALOGS> &analogs) const;

    void setVID(int vid) { vid_ = vid; }
    void setPID(int pid) { pid_ = pid; }
    int getVID() const { return vid_; }
    int getPID() const { return pid_; }

    void dump();

private:
    std::map<int, ReportSet> reportSets_; // reportID -> ReportSet
};
