/*
    Copyright 2017-2022 Igor Petrovic

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <array>
#include <optional>

#ifdef EMUEEPROM_INCLUDE_CONFIG
#include "EmuEEPROMConfig.h"
#else
#define EMU_EEPROM_PAGE_SIZE      1024
#define EMUEEPROM_WRITE_ALIGNMENT 4
#endif

static_assert(EMUEEPROM_WRITE_ALIGNMENT % 4 == 0, "Write alignment needs to be multiple of 4");

class EmuEEPROM
{
    public:
    enum class pageStatus_t : uint64_t
    {
        ERASED,
        FORMATTED,
        ACTIVE,
        FULL,
        RECEIVE,
    };

    enum class readStatus_t : uint8_t
    {
        OK,
        NO_INDEX,
        NO_PAGE,
        BUFFER_TOO_SMALL,
        READ_ERROR,
        INVALID_CRC
    };

    enum class writeStatus_t : uint8_t
    {
        OK,
        PAGE_FULL,
        NO_PAGE,
        WRITE_ERROR,
        DATA_ERROR,
    };

    enum class page_t : uint8_t
    {
        PAGE_1,
        PAGE_2,
        PAGE_FACTORY
    };

    class StorageAccess
    {
        public:
        StorageAccess() = default;

        virtual bool init()                                            = 0;
        virtual bool erasePage(page_t page)                            = 0;
        virtual bool startWrite(page_t page, uint32_t offset)          = 0;
        virtual bool write(page_t page, uint32_t offset, uint8_t data) = 0;
        virtual bool endWrite(page_t page)                             = 0;
        virtual bool read(page_t page, uint32_t offset, uint8_t& data) = 0;
    };

    EmuEEPROM(StorageAccess& storageAccess,
              bool           useFactoryPage)
        : _storageAccess(storageAccess)
        , USE_FACTORY_PAGE(useFactoryPage)
    {}

    bool          init();
    readStatus_t  read(uint32_t index, char* data, uint16_t& length, const uint16_t maxLength);
    writeStatus_t write(const uint32_t index, const char* data);
    bool          format();
    pageStatus_t  pageStatus(page_t page);
    writeStatus_t pageTransfer();
    bool          indexExists(uint32_t index);

    static constexpr uint8_t paddingBytes(uint16_t size)
    {
        return ((PAD_CONTENT_TO_BYTES - size % 4) % PAD_CONTENT_TO_BYTES);
    }

    static constexpr uint32_t entrySize(uint16_t size = 1)
    {
        // single entry consists of:
        // content (uint32_t padded)
        // CRC (uint16_t)
        // size of data (uint16_t)
        // index (uint32_t)
        // content end marker (uint32_t)
        return size + paddingBytes(size) + CONTENT_METADATA_SIZE;
    }

    static constexpr uint32_t writeOffsetAligned(uint32_t offset)
    {
        return offset + EMUEEPROM_WRITE_ALIGNMENT - (offset % EMUEEPROM_WRITE_ALIGNMENT);
    }

    private:
    enum class pageOp_t : uint8_t
    {
        READ,
        WRITE
    };

    StorageAccess&            _storageAccess;
    const bool                USE_FACTORY_PAGE;
    static constexpr uint32_t CONTENT_END_MARKER    = 0x00;
    static constexpr uint64_t PAGE_MARKER_ERASED    = 0xFFFFFFFFFFFFFFFF;
    static constexpr uint64_t PAGE_MARKER_PROGRAMED = 0xAAAAAAAAAAAAAAAA;
    static constexpr uint32_t PAGE_END_OFFSET       = EMU_EEPROM_PAGE_SIZE;
    static constexpr size_t   PAGE_STATUS_BYTES     = 32;
    static constexpr uint8_t  PAD_CONTENT_TO_BYTES  = 4;
    static constexpr uint8_t  CONTENT_METADATA_SIZE = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t);
    static constexpr uint32_t INVALID_INDEX         = 0xFFFFFFFF;

    // Calculate the max amount of strings which can be stored in a page and use it to create an array
    // in which transfered indexes will be stored during page transfer. When performing page transfer,
    // transfered index must be stored in RAM so that it's transfered only once (the latest version).
    static constexpr uint32_t         MAX_STRINGS           = (EMU_EEPROM_PAGE_SIZE - PAGE_STATUS_BYTES) / (PAD_CONTENT_TO_BYTES + CONTENT_METADATA_SIZE);
    std::array<uint32_t, MAX_STRINGS> _indexTransferedArray = {};

    bool                    writePageStatus(page_t page, pageStatus_t status);
    bool                    isIndexTransfered(uint32_t index);
    void                    markAsTransfered(uint32_t index);
    bool                    findValidPage(pageOp_t operation, page_t& page);
    writeStatus_t           writeInternal(uint32_t index, const char* data, uint16_t length);
    bool                    write8(page_t page, uint32_t offset, uint8_t data);
    bool                    write16(page_t page, uint32_t offset, uint16_t data);
    bool                    write32(page_t page, uint32_t offset, uint32_t data);
    bool                    write64(page_t page, uint32_t offset, uint64_t data);
    std::optional<uint8_t>  read8(page_t page, uint32_t offset);
    std::optional<uint16_t> read16(page_t page, uint32_t offset);
    std::optional<uint32_t> read32(page_t page, uint32_t offset);
    std::optional<uint64_t> read64(page_t page, uint32_t offset);
    uint16_t                xmodemCRCUpdate(uint16_t crc, char data);
    bool                    erasePage(page_t page);
    writeStatus_t           copyFromFactory();
    std::optional<uint32_t> nextOffsetToWrite(page_t page);
    void                    resetTransferedIndexes();
};