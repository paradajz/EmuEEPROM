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
#include <vector>

class EmuEEPROM
{
    public:
    enum class pageStatus_t : uint32_t
    {
        valid     = 0x00,          ///< Page containing valid data
        erased    = 0xFFFFFFFF,    ///< Page is empty
        receiving = 0xEEEEEEEE     ///< Page is marked to receive data
    };

    enum class readStatus_t : uint8_t
    {
        ok,
        noVar,
        noPage,
        readError
    };

    enum class writeStatus_t : uint8_t
    {
        ok,
        pageFull,
        noPage,
        writeError
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

        virtual bool     init()                                   = 0;
        virtual uint32_t startAddress(page_t page)                = 0;
        virtual bool     erasePage(page_t page)                   = 0;
        virtual bool     write16(uint32_t address, uint16_t data) = 0;
        virtual bool     write32(uint32_t address, uint32_t data) = 0;
        virtual bool     read16(uint32_t address, uint16_t& data) = 0;
        virtual bool     read32(uint32_t address, uint32_t& data) = 0;
        virtual uint32_t pageSize()                               = 0;
    };

    EmuEEPROM(StorageAccess& storageAccess, bool useFactoryPage)
        : _storageAccess(storageAccess)
        , _useFactoryPage(useFactoryPage)
    {}

    bool           init();
    readStatus_t   read(uint32_t address, uint16_t& data);
    writeStatus_t  write(uint32_t address, uint16_t data);
    bool           format();
    pageStatus_t   pageStatus(page_t page);
    writeStatus_t  pageTransfer();
    const uint32_t maxAddress() const;

    private:
    enum class pageOp_t : uint8_t
    {
        read,
        write
    };

    StorageAccess&        _storageAccess;
    bool                  _useFactoryPage;
    std::vector<uint8_t>  _varTransferedArray = {};
    std::vector<uint16_t> _eepromCache        = {};
    uint32_t              _nextAddToWrite;

    bool          isVarTransfered(uint16_t address);
    void          markAsTransfered(uint16_t address);
    bool          findValidPage(pageOp_t operation, page_t& page);
    writeStatus_t writeInternal(uint16_t address, uint16_t data);
    bool          cache();
};