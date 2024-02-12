/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 11 2024 17:25:19
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cstring>

struct SerializeHeader
{
    inline static constexpr uint32_t
        MAGIC = 'E' | ('A' << 8) | ('S' << 16) | ('D' << 24);
    inline static constexpr uint32_t CUR_VER = 0;
    inline static constexpr uint32_t MIN_ENABLED_VER = 0;
    uint32_t magic = MAGIC;
    uint32_t version = 0;
    uint32_t size = 0;
    uint32_t reserved[14]{};
};

class Serializer
{
    std::vector<uint8_t> data_;
    size_t limitSize_{};

public:
    Serializer(size_t size, size_t margin);

    bool exceedLimit() const { return data_.size() > limitSize_; }
    void flash();

    void append8u(uint8_t v)
    {
        data_.push_back(v);
    }

    void append8i(int8_t v)
    {
        append8u(static_cast<uint8_t>(v));
    }

    void append16u(uint16_t v)
    {
        append8u(v & 0xff);
        append8u((v >> 8) & 0xff);
    }

    void append16i(int16_t v)
    {
        append16u(static_cast<uint16_t>(v));
    }

    void append32u(uint32_t v)
    {
        append16u(v & 0xffff);
        append16u((v >> 16) & 0xffff);
    }

    void append32i(int32_t v)
    {
        append32u(static_cast<uint32_t>(v));
    }

    void append(int v)
    {
        append32i(v);
    }

    void append(const void *p, size_t size)
    {
        const auto *src = static_cast<const uint8_t *>(p);
        data_.insert(data_.end(), src, src + size);
    }
};

class Deserializer
{
    const uint8_t *p_{};
    const uint8_t *tail_{};
    const SerializeHeader *header_{};

public:
    Deserializer();
    ~Deserializer();

    explicit operator bool() const { return p_; }

    int peek8u()
    {
        return *p_++;
    }

    int peek8i()
    {
        return static_cast<int8_t>(*p_++);
    }

    int peek16u()
    {
        int v = peek8u();
        v |= peek8u() << 8;
        return v;
    }

    int peek16i()
    {
        return static_cast<int16_t>(peek16u());
    }

    int32_t peek32u()
    {
        int v = peek16u();
        v |= peek16u() << 16;
        return v;
    }

    int32_t peek32i()
    {
        return static_cast<int32_t>(peek32u());
    }

    int peek()
    {
        return peek32i();
    }

    void peek(void *p, size_t size)
    {
        memcpy(p, p_, size);
        p_ += size;
    }
};
