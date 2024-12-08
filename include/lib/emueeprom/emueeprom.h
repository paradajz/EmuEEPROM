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

#include "common.h"

#include <stdio.h>
#include <array>

namespace lib::emueeprom
{
    class EmuEEPROM
    {
        public:
        EmuEEPROM(Hwa& hwa, bool useFactoryPage)
            : _hwa(hwa)
            , _useFactoryPage(useFactoryPage)
        {}

        bool          init();
        readStatus_t  read(uint32_t address, uint16_t& data);
        writeStatus_t write(uint32_t address, uint16_t data, bool cacheOnly = false);
        bool          format();
        pageStatus_t  pageStatus(page_t page);
        writeStatus_t pageTransfer();
        uint32_t      maxAddress() const;
        void          writeCacheToFlash();

        private:
        enum class pageOp_t : uint8_t
        {
            READ,
            WRITE
        };

        static constexpr uint32_t MAX_ADDRESS = (EMU_EEPROM_PAGE_SIZE / 4) - 1;

        Hwa&                              _hwa;
        bool                              _useFactoryPage;
        std::array<uint16_t, MAX_ADDRESS> _eepromCache = {};
        uint32_t                          _nextOffsetToWrite;

        bool          findValidPage(pageOp_t operation, page_t& page);
        writeStatus_t writeInternal(uint16_t address, uint16_t data, bool cacheOnly = false);
        bool          cache();
    };
}    // namespace lib::emueeprom
