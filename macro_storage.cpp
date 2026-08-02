#include "macro_storage.h"

#include "usb_storage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>

extern "C"
{
#include <ff.h>
}

namespace Macro
{
namespace
{
constexpr uint32_t STORE_OFFSET = 0x190000; // after the existing 64 KiB config reservation
constexpr uint32_t BANK_HEADER_SIZE = FLASH_SECTOR_SIZE;
constexpr uint32_t SLOT_SIZE = 3 * FLASH_SECTOR_SIZE;
constexpr uint32_t BANK_SIZE = BANK_HEADER_SIZE + PROFILE_SLOTS * SLOT_SIZE;
constexpr uint32_t STATE_OFFSET = STORE_OFFSET + 2 * BANK_SIZE;
constexpr uint32_t BANK_MAGIC = 0x4b4e424d; // MBNK
constexpr uint32_t STATE_MAGIC = 0x4154534d; // MSTA
constexpr uint32_t STORE_VERSION = 1;
constexpr size_t MAX_FILENAME = 64;

struct FlashSlot
{
    uint32_t used;
    uint32_t size;
    uint32_t crc;
    uint32_t lastUse;
    char filename[MAX_FILENAME + 1];
    char profileName[MAX_FILENAME + 1];
    uint8_t reserved[2];
};

struct BankHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    int32_t activeSlot;
    uint32_t useCounter;
    uint32_t headerCrc;
    FlashSlot slots[PROFILE_SLOTS];
};
static_assert(sizeof(BankHeader) <= BANK_HEADER_SIZE);
static_assert(sizeof(FlashSlot) == 148);

struct StateRecord
{
    uint32_t magic;
    uint32_t generation;
    uint32_t bankGeneration;
    int8_t activeSlot;
    uint8_t reserved[3];
    uint32_t useCounter;
    uint32_t lastUse[PROFILE_SLOTS];
    uint32_t crc;
};
static_assert(sizeof(StateRecord) <= FLASH_PAGE_SIZE);
static_assert(sizeof(StateRecord) == 56);
static_assert(STATE_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_SIZE_BYTES);

const uint8_t *flashAddress(uint32_t offset)
{
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
}

uint32_t bankOffset(int bank)
{
    return STORE_OFFSET + bank * BANK_SIZE;
}

uint32_t slotOffset(int bank, int slot)
{
    return bankOffset(bank) + BANK_HEADER_SIZE + slot * SLOT_SIZE;
}

uint32_t headerCrc(BankHeader header)
{
    header.headerCrc = 0;
    return crc32(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
}

bool validHeader(const BankHeader &header)
{
    if (header.magic != BANK_MAGIC || header.version != STORE_VERSION ||
        header.headerCrc != headerCrc(header) ||
        header.activeSlot < -1 || header.activeSlot >= PROFILE_SLOTS)
        return false;
    for (const auto &slot : header.slots)
    {
        if (slot.used > 1 || slot.filename[MAX_FILENAME] || slot.profileName[MAX_FILENAME] ||
            (slot.used && (!slot.size || slot.size > MAX_FILE_SIZE)))
            return false;
    }
    return true;
}

bool newer(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b) > 0;
}

bool sameFilename(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool hasMacroExtension(const char *filename)
{
    const std::string name = filename ? filename : "";
    constexpr const char *extension = ".eamacro";
    if (name.size() <= std::strlen(extension))
        return false;
    return sameFilename(name.substr(name.size() - std::strlen(extension)), extension);
}

void eraseFlash(uint32_t offset, size_t size)
{
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, size);
    restore_interrupts(irq);
}

void programFlash(uint32_t offset, const uint8_t *data, size_t size)
{
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_program(offset, data, size);
    restore_interrupts(irq);
}

std::vector<uint8_t> paddedPage(const void *data, size_t size, size_t paddedSize)
{
    std::vector<uint8_t> result(paddedSize, 0xff);
    std::memcpy(result.data(), data, size);
    return result;
}

uint32_t stateCrc(StateRecord record)
{
    record.crc = 0;
    return crc32(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
}
}

void Storage::init()
{
    const BankHeader *headers[2] = {
        reinterpret_cast<const BankHeader *>(flashAddress(bankOffset(0))),
        reinterpret_cast<const BankHeader *>(flashAddress(bankOffset(1))),
    };
    const bool valid[2] = {validHeader(*headers[0]), validHeader(*headers[1])};
    if (valid[0] && valid[1])
        activeBank_ = newer(headers[1]->generation, headers[0]->generation) ? 1 : 0;
    else if (valid[0])
        activeBank_ = 0;
    else if (valid[1])
        activeBank_ = 1;
    else
    {
        activeBank_ = -1;
        activeSlot_ = -1;
        generation_ = 0;
        useCounter_ = 0;
        slots_ = {};
        return;
    }

    const auto &header = *headers[activeBank_];
    generation_ = header.generation;
    useCounter_ = header.useCounter;
    activeSlot_ = header.activeSlot;
    for (int i = 0; i < PROFILE_SLOTS; ++i)
    {
        const auto &source = header.slots[i];
        auto &slot = slots_[i];
        slot.used = source.used;
        slot.size = source.size;
        slot.crc = source.crc;
        slot.lastUse = source.lastUse;
        slot.filename = source.filename;
        slot.profileName = source.profileName;
        if (slot.used && crc32(flashAddress(slotOffset(activeBank_, i)), slot.size) != slot.crc)
            slot = {};
    }
    loadState();
    if (activeSlot_ < -1 || activeSlot_ >= PROFILE_SLOTS ||
        (activeSlot_ >= 0 && !slots_[activeSlot_].used))
        activeSlot_ = -1;
    if (!loadActiveProfile())
        activeSlot_ = -1;
}

std::vector<std::string> Storage::listUsbFiles() const
{
    std::vector<std::string> files;
    if (!usbStorageMounted())
        return files;
    DIR directory{};
    if (f_opendir(&directory, usbStorageRootPath()) != FR_OK)
        return files;
    FILINFO info{};
    while (f_readdir(&directory, &info) == FR_OK && info.fname[0])
    {
        if (!(info.fattrib & AM_DIR) && hasMacroExtension(info.fname))
            files.emplace_back(info.fname);
    }
    f_closedir(&directory);
    std::sort(files.begin(), files.end(), [](const std::string &a, const std::string &b)
              {
                  return std::lexicographical_compare(
                      a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y)); });
              });
    return files;
}

Result Storage::importUsbFile(const std::string &filename)
{
    if (!usbStorageMounted())
        return Result::NO_STORAGE;
    if (filename.empty() || filename.size() > MAX_FILENAME || !hasMacroExtension(filename.c_str()) ||
        filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos)
        return Result::NO_FILE;
    const std::string path = std::string(usbStorageRootPath()) + "/" + filename;
    FILINFO info{};
    const auto stat = f_stat(path.c_str(), &info);
    if (stat == FR_NO_FILE || stat == FR_NO_PATH)
        return Result::NO_FILE;
    if (stat != FR_OK)
        return Result::IO_ERROR;
    if (!info.fsize || info.fsize > MAX_FILE_SIZE)
        return Result::TOO_LARGE;
    FIL file{};
    if (f_open(&file, path.c_str(), FA_READ) != FR_OK)
        return Result::IO_ERROR;
    std::vector<uint8_t> bytes(info.fsize);
    UINT read = 0;
    const auto readResult = f_read(&file, bytes.data(), bytes.size(), &read);
    const auto closeResult = f_close(&file);
    if (readResult != FR_OK || closeResult != FR_OK || read != bytes.size())
        return Result::IO_ERROR;
    Profile parsed;
    const auto result = parse(bytes.data(), bytes.size(), parsed);
    if (result != Result::OK)
        return result;

    int target = -1;
    for (int i = 0; i < PROFILE_SLOTS; ++i)
        if (slots_[i].used && sameFilename(slots_[i].filename, filename))
            target = i;
    if (target < 0)
        for (int i = 0; i < PROFILE_SLOTS; ++i)
            if (!slots_[i].used)
            {
                target = i;
                break;
            }
    if (target < 0)
    {
        target = 0;
        for (int i = 1; i < PROFILE_SLOTS; ++i)
            if (slots_[i].lastUse < slots_[target].lastUse)
                target = i;
    }
    if (!rewriteBank(target, target, &filename, &bytes, &parsed))
        return Result::IO_ERROR;
    activeProfile_ = std::move(parsed);
    saveState();
    return Result::OK;
}

bool Storage::select(int slot)
{
    if (slot < -1 || slot >= PROFILE_SLOTS || (slot >= 0 && !slots_[slot].used))
        return false;
    if (slot == activeSlot_)
        return true;
    const int previousSlot = activeSlot_;
    activeSlot_ = slot;
    if (!loadActiveProfile())
    {
        activeSlot_ = previousSlot;
        loadActiveProfile();
        return false;
    }
    ++useCounter_;
    if (slot >= 0)
        slots_[slot].lastUse = useCounter_;
    saveState();
    return true;
}

const char *Storage::slotFilename(int slot) const
{
    return slotUsed(slot) ? slots_[slot].filename.c_str() : "Empty";
}

const char *Storage::slotProfileName(int slot) const
{
    return slotUsed(slot) ? slots_[slot].profileName.c_str() : "Empty";
}

bool Storage::slotUsed(int slot) const
{
    return slot >= 0 && slot < PROFILE_SLOTS && slots_[slot].used;
}

bool Storage::rewriteBank(int newActiveSlot, int replacementSlot,
                          const std::string *filename,
                          const std::vector<uint8_t> *replacementData,
                          const Profile *replacementProfile)
{
    const int destination = activeBank_ == 0 ? 1 : 0;
    BankHeader header{};
    header.magic = BANK_MAGIC;
    header.version = STORE_VERSION;
    header.generation = generation_ + 1;
    header.activeSlot = newActiveSlot;
    header.useCounter = useCounter_ + 1;
    auto nextSlots = slots_;
    if (replacementSlot >= 0)
    {
        auto &slot = nextSlots[replacementSlot];
        slot.used = true;
        slot.size = replacementData->size();
        slot.crc = crc32(replacementData->data(), replacementData->size());
        slot.filename = *filename;
        slot.profileName = replacementProfile->name.empty() ? *filename : replacementProfile->name;
    }
    if (newActiveSlot >= 0)
        nextSlots[newActiveSlot].lastUse = header.useCounter;

    for (int i = 0; i < PROFILE_SLOTS; ++i)
    {
        const auto &slot = nextSlots[i];
        auto &flashSlot = header.slots[i];
        flashSlot.used = slot.used;
        flashSlot.size = slot.size;
        flashSlot.crc = slot.crc;
        flashSlot.lastUse = slot.lastUse;
        std::snprintf(flashSlot.filename, sizeof(flashSlot.filename), "%s", slot.filename.c_str());
        std::snprintf(flashSlot.profileName, sizeof(flashSlot.profileName), "%s", slot.profileName.c_str());
    }

    eraseFlash(bankOffset(destination), BANK_SIZE);
    for (int i = 0; i < PROFILE_SLOTS; ++i)
    {
        if (!nextSlots[i].used)
            continue;
        std::vector<uint8_t> bytes;
        if (i == replacementSlot)
            bytes = *replacementData;
        else
        {
            if (activeBank_ < 0 || !slots_[i].used)
                return false;
            bytes.assign(flashAddress(slotOffset(activeBank_, i)),
                         flashAddress(slotOffset(activeBank_, i)) + slots_[i].size);
        }
        const size_t programSize = (bytes.size() + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
        bytes.resize(programSize, 0xff);
        programFlash(slotOffset(destination, i), bytes.data(), bytes.size());
    }
    for (int i = 0; i < PROFILE_SLOTS; ++i)
        if (nextSlots[i].used &&
            crc32(flashAddress(slotOffset(destination, i)), nextSlots[i].size) != nextSlots[i].crc)
            return false;
    header.headerCrc = headerCrc(header);
    auto headerBytes = paddedPage(&header, sizeof(header), BANK_HEADER_SIZE);
    programFlash(bankOffset(destination), headerBytes.data(), headerBytes.size());

    activeBank_ = destination;
    generation_ = header.generation;
    useCounter_ = header.useCounter;
    activeSlot_ = newActiveSlot;
    slots_ = std::move(nextSlots);
    return true;
}

bool Storage::loadActiveProfile()
{
    if (activeSlot_ < 0)
    {
        activeProfile_ = {};
        return true;
    }
    if (activeBank_ < 0 || !slots_[activeSlot_].used)
        return false;
    Profile parsed;
    if (parse(flashAddress(slotOffset(activeBank_, activeSlot_)),
              slots_[activeSlot_].size, parsed) != Result::OK)
        return false;
    activeProfile_ = std::move(parsed);
    return true;
}

void Storage::loadState()
{
    const StateRecord *best = nullptr;
    const auto *base = flashAddress(STATE_OFFSET);
    for (size_t offset = 0; offset < FLASH_SECTOR_SIZE; offset += FLASH_PAGE_SIZE)
    {
        const auto *record = reinterpret_cast<const StateRecord *>(base + offset);
        if (record->magic != STATE_MAGIC || record->crc != stateCrc(*record) ||
            record->bankGeneration != generation_)
            continue;
        if (!best || newer(record->generation, best->generation))
            best = record;
    }
    if (best)
    {
        activeSlot_ = best->activeSlot;
        useCounter_ = best->useCounter;
        for (int i = 0; i < PROFILE_SLOTS; ++i)
            slots_[i].lastUse = best->lastUse[i];
    }
}

void Storage::saveState()
{
    const auto *base = flashAddress(STATE_OFFSET);
    size_t target = FLASH_SECTOR_SIZE;
    uint32_t stateGeneration = 0;
    for (size_t offset = 0; offset < FLASH_SECTOR_SIZE; offset += FLASH_PAGE_SIZE)
    {
        const auto *record = reinterpret_cast<const StateRecord *>(base + offset);
        if (record->magic == 0xffffffffu)
        {
            if (target == FLASH_SECTOR_SIZE)
                target = offset;
            continue;
        }
        if (record->magic == STATE_MAGIC && record->crc == stateCrc(*record))
            stateGeneration = std::max(stateGeneration, record->generation);
    }
    if (target == FLASH_SECTOR_SIZE)
    {
        eraseFlash(STATE_OFFSET, FLASH_SECTOR_SIZE);
        target = 0;
    }
    StateRecord record{};
    record.magic = STATE_MAGIC;
    record.generation = stateGeneration + 1;
    record.bankGeneration = generation_;
    record.activeSlot = activeSlot_;
    record.useCounter = useCounter_;
    for (int i = 0; i < PROFILE_SLOTS; ++i)
        record.lastUse[i] = slots_[i].lastUse;
    record.crc = stateCrc(record);
    auto bytes = paddedPage(&record, sizeof(record), FLASH_PAGE_SIZE);
    programFlash(STATE_OFFSET + target, bytes.data(), bytes.size());
}
}
