/*
    Copyright 2017-2020 Igor Petrovic

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

#ifdef EMUEEPROM_INCLUDE_CONFIG
#include "EmuEEPROMConfig.h"
#else
#define EMU_EEPROM_PAGE_SIZE 1024
#endif

class EmuEEPROM
{
    public:
    enum class pageStatus_t : uint32_t
    {
        valid     = 0x00,          ///< Page containing valid data
        erased    = 0xFFFFFFFF,    ///< Page is empty
        formatted = 0xFFFFEEEE,    ///< Page is prepared for use but currently unused
        receiving = 0xEEEEEEEE     ///< Page is marked to receive data
    };

    enum class readStatus_t : uint8_t
    {
        ok,
        noIndex,
        noPage,
        bufferTooSmall,
        readError,
        invalidCrc
    };

    enum class writeStatus_t : uint8_t
    {
        ok,
        pageFull,
        noPage,
        writeError,
        dataError,
    };

    enum class page_t : uint8_t
    {
        page1,
        page2,
        pageFactory
    };

    class StorageAccess
    {
        public:
        StorageAccess() {}

        virtual bool     init()                                = 0;
        virtual uint32_t startAddress(page_t page)             = 0;
        virtual bool     erasePage(page_t page)                = 0;
        virtual bool     write(uint32_t address, uint8_t data) = 0;
        virtual uint8_t  read(uint32_t address)                = 0;
    };

    EmuEEPROM(StorageAccess& storageAccess, bool useFactoryPage)
        : _storageAccess(storageAccess)
        , _useFactoryPage(useFactoryPage)
    {}

    bool          init();
    readStatus_t  read(uint32_t index, char* data, uint16_t& length, const uint16_t maxLength);
    writeStatus_t write(const uint32_t index, const char* data);
    bool          format();
    pageStatus_t  pageStatus(page_t page);
    writeStatus_t pageTransfer();
    bool          indexExists(uint32_t index);

    constexpr uint8_t paddingBytes(uint16_t size)
    {
        return ((4 - size % 4) % 4);
    }

    constexpr uint32_t entrySize(uint16_t size = 1)
    {
        // single entry consists of:
        // content (uint32_t padded)
        // CRC (uint16_t)
        // size of data (uint16_t)
        // index (uint32_t)
        // content end marker (uint32_t)
        return size + paddingBytes(size) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t);
    }

    private:
    enum class pageOp_t : uint8_t
    {
        read,
        write
    };

    StorageAccess&            _storageAccess;
    const bool                _useFactoryPage;
    static constexpr uint32_t _contentEndMarker = 0x00;
    uint32_t                  _nextAddToWrite   = 0;

    // first four bytes are reserved for page status, and next four for first (blank) content marker
    static constexpr uint32_t                                            _maxIndexes           = 0xFFFF - 1;
    std::array<uint32_t, (_maxIndexes / 32) + ((_maxIndexes % 32) != 0)> _indexTransferedArray = {};

    bool          isIndexTransfered(uint32_t index);
    void          markAsTransfered(uint32_t index);
    bool          findValidPage(pageOp_t operation, page_t& page);
    writeStatus_t writeInternal(uint32_t index, const char* data, uint16_t length);
    bool          write8(uint32_t address, uint8_t data);
    bool          write16(uint32_t address, uint16_t data);
    bool          write32(uint32_t address, uint32_t data);
    uint8_t       read8(uint32_t address);
    uint16_t      read16(uint32_t address);
    uint32_t      read32(uint32_t address);
    uint16_t      xmodemCRCUpdate(uint16_t crc, char data);
};