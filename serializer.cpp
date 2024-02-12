/*
 * author : Shuichi TAKANO
 * since  : Sun Feb 11 2024 17:41:30
 */

#include "serializer.h"
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <cassert>
#include "debug.h"

namespace
{
    constexpr uint32_t getFlashOfs()
    {
        return 1536 * 1024; // flash 先頭から 1.5MiB
    }

    constexpr const uint8_t *getFlashAddr()
    {
        return reinterpret_cast<const uint8_t *>(getFlashOfs() + XIP_BASE);
    }
}

Serializer::Serializer(size_t size, size_t margin)
{
    auto alignedSize = (size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    data_.reserve(alignedSize);
    data_.resize(sizeof(SerializeHeader));
    limitSize_ = alignedSize - margin;
}

void Serializer::flash()
{
    auto actualSize = data_.size();
    data_.resize((data_.size() + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));

    SerializeHeader *p = reinterpret_cast<SerializeHeader *>(data_.data());
    *p = {};
    p->size = actualSize;

    auto dstOfs = getFlashOfs();

    {
        auto save = save_and_disable_interrupts();
        flash_range_erase(dstOfs, data_.size());
        flash_range_program(dstOfs, data_.data(), data_.size());
        restore_interrupts(save);
    }

    DPRINT(("flash: %d bytes. actual %d bytes.\n", data_.size(), actualSize));
}

///////////////////
Deserializer::Deserializer()
{
    p_ = reinterpret_cast<const uint8_t *>(getFlashAddr());
    header_ = reinterpret_cast<const SerializeHeader *>(p_);

    if (header_->magic != SerializeHeader::MAGIC ||
        header_->version < SerializeHeader::MIN_ENABLED_VER)
    {
        p_ = nullptr;
        return;
    }

    tail_ = p_ + header_->size;
    DPRINT(("Deserialize: %d bytes.\n", header_->size));
    p_ += sizeof(SerializeHeader);
}

Deserializer::~Deserializer()
{
    assert(p_ <= tail_);
}