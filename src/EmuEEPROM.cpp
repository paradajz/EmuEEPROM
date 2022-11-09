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

#include "EmuEEPROM.h"

bool EmuEEPROM::init()
{
    if (!_storageAccess.init())
    {
        return false;
    }

    bool doCache = true;

    _nextOffsetToWrite = 0;
    std::fill(_eepromCache.begin(), _eepromCache.end(), 0xFFFF);

    auto page1Status = pageStatus(page_t::PAGE_1);
    auto page2Status = pageStatus(page_t::PAGE_2);

    // check for invalid header states and repair if necessary
    switch (page1Status)
    {
    case pageStatus_t::ERASED:
    {
        if (page2Status == pageStatus_t::VALID)
        {
            // page 1 erased, page 2 valid
            // format page 1 properly
            _storageAccess.erasePage(page_t::PAGE_1);

            if (!_storageAccess.write32(page_t::PAGE_1, 0, static_cast<uint32_t>(pageStatus_t::FORMATTED)))
            {
                return false;
            }
        }
        else
        {
            // invalid state
            if (!format())
            {
                return false;
            }

            // caching is done automatically after formatting if possible
            doCache = false;
        }
    }
    break;

    case pageStatus_t::RECEIVING:
    {
        if (page2Status == pageStatus_t::VALID)
        {
            // page 1 in receive state, page 2 valid
            // restart the transfer process by first erasing page 1 and then performing page transfer
            _storageAccess.erasePage(page_t::PAGE_1);

            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured, try to format
                if (!format())
                {
                    return false;
                }
            }

            // page has been transfered and with it, all contents have been cached
            // if that failed, pages have been formatted so no caching is required
            doCache = false;
        }
        else
        {
            // invalid state
            if (!format())
            {
                return false;
            }

            // caching is done automatically after formatting if possible
            doCache = false;
        }
    }
    break;

    case pageStatus_t::VALID:
    {
        if (page2Status == pageStatus_t::VALID)
        {
            // invalid state
            if (!format())
            {
                return false;
            }
        }
        else if (page2Status == pageStatus_t::ERASED)
        {
            // page 1 valid, page 2 erased
            // format page2
            _storageAccess.erasePage(page_t::PAGE_2);

            if (!_storageAccess.write32(page_t::PAGE_2, 0, static_cast<uint32_t>(pageStatus_t::FORMATTED)))
            {
                return false;
            }
        }
        else if (page2Status == pageStatus_t::FORMATTED)
        {
            // nothing to do
        }
        else if (page2Status == pageStatus_t::RECEIVING)
        {
            // page 1 valid, page 2 in receive state
            // restart the transfer process by first erasing page 2 and then performing page transfer
            _storageAccess.erasePage(page_t::PAGE_2);

            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured, try to format
                if (!format())
                {
                    return false;
                }
            }

            // page has been transfered and with it, all contents have been cached
            // if that failed, pages have been formatted so no caching is required
            doCache = false;
        }
        else
        {
            // invalid state
            if (!format())
            {
                return false;
            }

            // caching is done automatically after formatting if possible
            doCache = false;
        }
    }
    break;

    case pageStatus_t::FORMATTED:
    {
        if (page2Status == pageStatus_t::VALID)
        {
            // nothing to do
        }
        else
        {
            // invalid state
            if (!format())
            {
                return false;
            }

            // caching is done automatically after formatting if possible
            doCache = false;
        }
    }
    break;

    default:
    {
        // invalid state
        if (!format())
        {
            return false;
        }

        // caching is done automatically after formatting if possible
        doCache = false;
    }
    break;
    }

    if (doCache)
    {
        // if caching fails for any reason, just format everything
        if (!cache())
        {
            format();
        }
    }

    return true;
}

bool EmuEEPROM::format()
{
    // erase both pages and set page 1 as valid

    if (!_storageAccess.erasePage(page_t::PAGE_1))
    {
        return false;
    }

    if (!_storageAccess.erasePage(page_t::PAGE_2))
    {
        return false;
    }

    // clear out cache
    std::fill(_eepromCache.begin(), _eepromCache.end(), 0xFFFF);

    // copy contents from factory page to page 1 if the page is in correct status
    if (_useFactoryPage && (pageStatus(page_t::PAGE_FACTORY) == pageStatus_t::VALID))
    {
        for (uint32_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i += 4)
        {
            uint32_t data;

            if (!_storageAccess.read32(page_t::PAGE_FACTORY, i, data))
            {
                return false;
            }

            if (data == 0xFFFFFFFF)
            {
                break;    // empty block, no need to go further
            }

            if (!_storageAccess.write32(page_t::PAGE_1, i, data))
            {
                return false;
            }
        }

        if (!cache())
        {
            return false;
        }
    }
    else
    {
        // set valid status to page1
        if (!_storageAccess.write32(page_t::PAGE_1, 0, static_cast<uint32_t>(pageStatus_t::VALID)))
        {
            return false;
        }

        if (!_storageAccess.write32(page_t::PAGE_2, 0, static_cast<uint32_t>(pageStatus_t::FORMATTED)))
        {
            return false;
        }
    }

    _nextOffsetToWrite = 0;

    return true;
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t address, uint16_t& data)
{
    if (address >= maxAddress())
    {
        return readStatus_t::READ_ERROR;
    }

    if (_eepromCache[address] != 0xFFFF)
    {
        data = _eepromCache[address];
        return readStatus_t::OK;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::READ, validPage))
    {
        return readStatus_t::NO_PAGE;
    }

    readStatus_t status = readStatus_t::NO_VAR;

    // take into account 4-byte page header
    const uint32_t PAGE_START_OFFSET = sizeof(pageStatus_t);
    uint32_t       readOffset        = EMU_EEPROM_PAGE_SIZE - 4;

    auto next = [&readOffset]()
    {
        readOffset = readOffset - 4;
    };

    if (_nextOffsetToWrite)
    {
        //_nextOffsetToWrite contains next offset to which new data will be written in current page.
        // Subtracting this value by 4 results in having the last written address.
        // This will speed up the finding of read offset process since all unused
        // offsets will be skipped.
        if (_nextOffsetToWrite >= 4)
        {
            readOffset = _nextOffsetToWrite - 4;
        }
    }

    // check each active page address starting from end
    while (readOffset >= PAGE_START_OFFSET)
    {
        uint32_t retrieved = 0;

        if (_storageAccess.read32(validPage, readOffset, retrieved))
        {
            if ((retrieved >> 16) == address)
            {
                _eepromCache[address] = retrieved & 0xFFFF;
                return readStatus_t::OK;
            }

            next();
        }
        else
        {
            next();
        }
    }

    return status;
}

EmuEEPROM::writeStatus_t EmuEEPROM::write(uint32_t address, uint16_t data, bool cacheOnly)
{
    if (address >= maxAddress())
    {
        return writeStatus_t::WRITE_ERROR;
    }

    writeStatus_t status;

    // rite the variable virtual address and value in the EEPROM
    status = writeInternal(address, data, cacheOnly);

    if (status == writeStatus_t::PAGE_FULL)
    {
        status = pageTransfer();

        // write the variable again to a new page
        if (status == writeStatus_t::OK)
        {
            status = writeInternal(address, data);
        }
    }

    return status;
}

bool EmuEEPROM::findValidPage(pageOp_t operation, page_t& page)
{
    auto page1Status = pageStatus(page_t::PAGE_1);
    auto page2Status = pageStatus(page_t::PAGE_2);

    switch (operation)
    {
    case pageOp_t::WRITE:
    {
        if (page2Status == pageStatus_t::VALID)
        {
            if (page1Status == pageStatus_t::RECEIVING)
            {
                page = page_t::PAGE_1;
            }
            else
            {
                page = page_t::PAGE_2;
            }
        }
        else if (page1Status == pageStatus_t::VALID)
        {
            if (page2Status == pageStatus_t::RECEIVING)
            {
                page = page_t::PAGE_2;
            }
            else
            {
                page = page_t::PAGE_1;
            }
        }
        else
        {
            // no valid page found
            return false;
        }
    }
    break;

    case pageOp_t::READ:
    {
        if (page1Status == pageStatus_t::VALID)
        {
            page = page_t::PAGE_1;
        }
        else if (page2Status == pageStatus_t::VALID)
        {
            page = page_t::PAGE_2;
        }
        else
        {
            // no valid page found
            return false;
        }
    }
    break;

    default:
    {
        page = page_t::PAGE_1;
    }
    break;
    }

    return true;
}

EmuEEPROM::writeStatus_t EmuEEPROM::writeInternal(uint16_t address, uint16_t data, bool cacheOnly)
{
    if (address == 0xFFFF)
    {
        return writeStatus_t::WRITE_ERROR;
    }

    if (cacheOnly)
    {
        _eepromCache[address] = data;
        return writeStatus_t::OK;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::WRITE, validPage))
    {
        return writeStatus_t::NO_PAGE;
    }

    const uint32_t PAGE_END_OFFSET = EMU_EEPROM_PAGE_SIZE;
    uint32_t       writeOffset     = sizeof(pageStatus_t);

    auto next = [&writeOffset]()
    {
        writeOffset += 4;
    };

    if (_nextOffsetToWrite)
    {
        if (_nextOffsetToWrite >= PAGE_END_OFFSET)
        {
            return writeStatus_t::PAGE_FULL;
        }

        if (!_storageAccess.write32(validPage, _nextOffsetToWrite, address << 16 | data))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        _nextOffsetToWrite += 4;
        _eepromCache[address] = data;
        return writeStatus_t::OK;
    }

    // check each active page address starting from begining
    while (writeOffset < PAGE_END_OFFSET)
    {
        uint32_t readData = 0;

        if (_storageAccess.read32(validPage, writeOffset, readData))
        {
            if (readData == 0xFFFFFFFF)
            {
                if (!_storageAccess.write32(validPage, writeOffset, address << 16 | data))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                _nextOffsetToWrite = writeOffset + 4;

                _eepromCache[address] = data;
                return writeStatus_t::OK;
            }

            next();
        }
        else
        {
            next();
        }
    }

    return writeStatus_t::PAGE_FULL;
}

EmuEEPROM::writeStatus_t EmuEEPROM::pageTransfer()
{
    page_t oldPage;
    page_t newPage = page_t::PAGE_1;

    if (!findValidPage(pageOp_t::READ, oldPage))
    {
        return writeStatus_t::NO_PAGE;
    }

    if (oldPage == page_t::PAGE_2)
    {
        // new page where content will be moved to
        newPage = page_t::PAGE_1;

        // old page where content will be taken from
        oldPage = page_t::PAGE_2;
    }
    else if (oldPage == page_t::PAGE_1)
    {
        // new page where content will be moved to
        newPage = page_t::PAGE_2;

        // old page where content will be taken from
        oldPage = page_t::PAGE_1;
    }
    else
    {
        return writeStatus_t::NO_PAGE;
    }

    if (!_storageAccess.write32(newPage, 0, static_cast<uint32_t>(pageStatus_t::RECEIVING)))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    // reset written flags
    std::fill(_varWrittenArray.begin(), _varWrittenArray.end(), 0);

    _nextOffsetToWrite = sizeof(pageStatus_t);

    // normally this procedure should move all variables from one page to another,
    // starting from the last address
    // since we're using cache, just dump the entire cache to the new page

    for (size_t i = 0; i < MAX_ADDRESS; i++)
    {
        if (_eepromCache[i] != 0xFFFF)
        {
            writeInternal(i, _eepromCache[i]);
        }
    }

    // format old page
    _storageAccess.erasePage(oldPage);

    if (!_storageAccess.write32(oldPage, 0, static_cast<uint32_t>(pageStatus_t::FORMATTED)))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    // set new Page status to VALID_PAGE status
    if (!_storageAccess.write32(newPage, 0, static_cast<uint32_t>(pageStatus_t::VALID)))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    return writeStatus_t::OK;
}

EmuEEPROM::pageStatus_t EmuEEPROM::pageStatus(page_t page)
{
    uint32_t     data;
    pageStatus_t status;

    switch (page)
    {
    case page_t::PAGE_1:
    {
        _storageAccess.read32(page_t::PAGE_1, 0, data);
    }
    break;

    case page_t::PAGE_2:
    {
        _storageAccess.read32(page_t::PAGE_2, 0, data);
    }
    break;

    case page_t::PAGE_FACTORY:
    {
        _storageAccess.read32(page_t::PAGE_FACTORY, 0, data);
    }
    break;

    default:
        return pageStatus_t::ERASED;
    }

    switch (data)
    {
    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::ERASED):
    {
        status = EmuEEPROM::pageStatus_t::ERASED;
    }
    break;

    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::RECEIVING):
    {
        status = EmuEEPROM::pageStatus_t::RECEIVING;
    }
    break;

    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::VALID):
    {
        status = EmuEEPROM::pageStatus_t::VALID;
    }
    break;

    default:
    {
        status = EmuEEPROM::pageStatus_t::FORMATTED;
    }
    break;
    }

    return status;
}

bool EmuEEPROM::isVarWritten(uint16_t address)
{
    uint16_t arrayIndex = address / 8;
    uint16_t varIndex   = address - 8 * arrayIndex;

    return (_varWrittenArray.at(arrayIndex) >> varIndex) & 0x01;
}

void EmuEEPROM::markAsWritten(uint16_t address)
{
    uint16_t arrayIndex = address / 8;
    uint16_t varIndex   = address - 8 * arrayIndex;

    _varWrittenArray.at(arrayIndex) |= (1UL << varIndex);
}

bool EmuEEPROM::cache()
{
    page_t validPage;
    std::fill(_varWrittenArray.begin(), _varWrittenArray.end(), 0);

    if (!findValidPage(pageOp_t::WRITE, validPage))
    {
        return false;
    }

    for (uint32_t i = EMU_EEPROM_PAGE_SIZE - 4; i >= 4; i -= 4)
    {
        uint32_t data;

        if (_storageAccess.read32(validPage, i, data))
        {
            if (data == 0xFFFFFFFF)
            {
                continue;    // blank variable
            }

            uint16_t value   = data & 0xFFFF;
            uint16_t address = data >> 16 & 0xFFFF;

            if (address >= maxAddress())
            {
                return false;
            }

            if (isVarWritten(address))
            {
                continue;
            }

            // copy variable to cache
            _eepromCache[address] = value;
            markAsWritten(address);
        }
    }

    return true;
}

uint32_t EmuEEPROM::maxAddress() const
{
    return MAX_ADDRESS;
}

void EmuEEPROM::writeCacheToFlash()
{
    // page transfer will make sure the cache is written out
    pageTransfer();
}