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
        VALID     = 0x00,          ///< Page containing valid data
        ERASED    = 0xFFFFFFFF,    ///< Page is empty
        FORMATTED = 0xFFFFEEEE,    ///< Page is prepared for use but currently unused
        RECEIVING = 0xEEEEEEEE     ///< Page is marked to receive data
    };

    enum class readStatus_t : uint8_t
    {
        OK,
        NO_VAR,
        NO_PAGE,
        READ_ERROR
    };

    enum class writeStatus_t : uint8_t
    {
        OK,
        PAGE_FULL,
        NO_PAGE,
        WRITE_ERROR
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
        virtual bool init()                                                = 0;
        virtual bool erasePage(page_t page)                                = 0;
        virtual bool write32(page_t page, uint32_t address, uint32_t data) = 0;
        virtual bool read32(page_t page, uint32_t address, uint32_t& data) = 0;
    };

    EmuEEPROM(StorageAccess& storageAccess, bool useFactoryPage)
        : _storageAccess(storageAccess)
        , _useFactoryPage(useFactoryPage)
    {}

    bool          init();
    readStatus_t  read(uint32_t address, uint16_t& data);
    readStatus_t  readCached(uint32_t address, uint16_t& data);
    writeStatus_t write(uint32_t address, uint16_t data);
    bool          format();
    pageStatus_t  pageStatus(page_t page);
    writeStatus_t pageTransfer();
    uint32_t      maxAddress() const;

    private:
    enum class pageOp_t : uint8_t
    {
        READ,
        WRITE
    };

    static constexpr uint32_t                MAX_ADDRESS = (EMU_EEPROM_PAGE_SIZE / 4) - 1;
    StorageAccess&                           _storageAccess;
    bool                                     _useFactoryPage;
    std::array<uint8_t, MAX_ADDRESS / 8 + 1> _varWrittenArray = {};
    std::array<uint16_t, MAX_ADDRESS>        _eepromCache     = {};
    uint32_t                                 _nextOffsetToWrite;

    bool          isVarWritten(uint16_t address);
    void          markAsWritten(uint16_t address);
    bool          findValidPage(pageOp_t operation, page_t& page);
    writeStatus_t writeInternal(uint16_t address, uint16_t data);
    bool          cache();
};