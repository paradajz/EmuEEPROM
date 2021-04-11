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
        return false;

    bool doCache = true;

    _varTransferedArray.resize(maxAddress() / 8 + 1, 0);
    _eepromCache.resize(maxAddress(), 0xFFFF);
    _nextAddToWrite = _storageAccess.pageSize();

    auto page1Status = pageStatus(page_t::page1);
    auto page2Status = pageStatus(page_t::page2);

    /* Check for invalid header states and repair if necessary */
    switch (page1Status)
    {
    case pageStatus_t::erased:
        if (page2Status == pageStatus_t::valid)
        {
            //page 1 erased, page 2 valid
            //format page 1 properly
            _storageAccess.erasePage(page_t::page1);

            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;
        }
        else if (page2Status == pageStatus_t::receiving)
        {
            //page 1 erased, page 2 in receive state
            //again, format page 1 properly
            _storageAccess.erasePage(page_t::page1);

            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;

            //mark page2 as valid
            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            //format both pages and set first page as valid
            if (!format())
                return false;

            //caching is done automatically after formatting if possible
            doCache = false;
        }
        break;

    case pageStatus_t::receiving:
        if (page2Status == pageStatus_t::valid)
        {
            //page 1 in receive state, page 2 valid
            //restart the transfer process by first erasing page 1 and then performing page transfer
            _storageAccess.erasePage(page_t::page1);

            if (pageTransfer() != writeStatus_t::ok)
            {
                //error occured, try to format
                if (!format())
                    return false;
            }

            //page has been transfered and with it, all contents have been cached
            doCache = false;
        }
        else if (page2Status == pageStatus_t::erased)
        {
            //page 1 in receive state, page 2 erased
            //erase page 2
            _storageAccess.erasePage(page_t::page2);

            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;

            //mark page 1 as valid
            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            //invalid state
            if (!format())
                return false;

            //caching is done automatically after formatting if possible
            doCache = false;
        }
        break;

    case pageStatus_t::valid:
        if (page2Status == pageStatus_t::valid)
        {
            //invalid state
            if (!format())
                return false;
        }
        else if (page2Status == pageStatus_t::erased)
        {
            //page 1 valid, page 2 erased
            //format page2
            _storageAccess.erasePage(page_t::page2);

            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;
        }
        else if (page2Status == pageStatus_t::formatted)
        {
            //nothing to do
        }
        else
        {
            //page 1 valid, page 2 in receive state
            //restart the transfer process by first erasing page 2 and then performing page transfer
            _storageAccess.erasePage(page_t::page2);

            if (pageTransfer() != writeStatus_t::ok)
            {
                //error occured, try to format
                if (!format())
                    return false;
            }

            //page has been transfered and with it, all contents have been cached
            doCache = false;
        }
        break;

    default:
        if (!format())
            return false;

        //caching is done automatically after formatting if possible
        doCache = false;
        break;
    }

    if (doCache)
    {
        //if caching fails for any reason, just format everything
        if (!cache())
            format();
    }

    return true;
}

bool EmuEEPROM::format()
{
    //erase both pages and set page 1 as valid

    if (!_storageAccess.erasePage(page_t::page1))
        return false;

    if (!_storageAccess.erasePage(page_t::page2))
        return false;

    //clear out cache
    std::fill(_eepromCache.begin(), _eepromCache.end(), 0xFFFF);

    //copy contents from factory page to page 1 if the page is in correct status
    if (_useFactoryPage && (pageStatus(page_t::pageFactory) == pageStatus_t::valid))
    {
        for (uint32_t i = 0; i < _storageAccess.pageSize(); i += 4)
        {
            uint32_t data;

            if (!_storageAccess.read32(_storageAccess.startAddress(page_t::pageFactory) + i, data))
                return false;

            if (data == 0xFFFFFFFF)
                break;    //empty block, no need to go further

            if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page1) + i, data))
                return false;
        }

        if (!cache())
            return false;
    }
    else
    {
        //set valid status to page1
        if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
            return false;

        if (!_storageAccess.write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
            return false;
    }

    _nextAddToWrite = _storageAccess.pageSize();

    return true;
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t address, uint16_t& data)
{
    if (address >= maxAddress())
        return readStatus_t::readError;

    if (_eepromCache[address] != 0xFFFF)
    {
        data = _eepromCache[address];
        return readStatus_t::ok;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return readStatus_t::noPage;

    readStatus_t status = readStatus_t::noVar;

    //take into account 4-byte page header
    uint32_t startAddress     = _storageAccess.startAddress(validPage);
    uint32_t pageSize         = _storageAccess.pageSize();
    uint32_t pageStartAddress = startAddress + 4;
    uint32_t pageEndAddress   = startAddress + pageSize - 2;

    auto next = [&pageEndAddress]() {
        pageEndAddress = pageEndAddress - 4;
    };

    if (_nextAddToWrite != _storageAccess.pageSize())
    {
        //_nextAddToWrite contains next address to which new data will be written
        //subtracting this value by 2 results in having the last written address
        //this will speed up the finding of read address process since all unused
        //addresses will be skipped
        if (_nextAddToWrite >= 2)
            pageEndAddress = _nextAddToWrite - 2;
    }

    //check each active page address starting from end
    while (pageEndAddress > pageStartAddress)
    {
        /* Get the current location content to be compared with virtual address */
        uint16_t addressValue = 0;

        if (_storageAccess.read16(pageEndAddress, addressValue))
        {
            /* Compare the read address with the virtual address */
            if (addressValue == address)
            {
                /* Get content of Address-2 which is variable value */
                if (!_storageAccess.read16(pageEndAddress - 2, data))
                    return readStatus_t::readError;

                _eepromCache[address] = data;

                return readStatus_t::ok;
            }
            else
            {
                next();
            }
        }
        else
        {
            next();
        }
    }

    return status;
}

EmuEEPROM::writeStatus_t EmuEEPROM::write(uint32_t address, uint16_t data)
{
    if (address >= maxAddress())
        return writeStatus_t::writeError;

    writeStatus_t status;

    /* Write the variable virtual address and value in the EEPROM */
    status = writeInternal(address, data);

    if (status == writeStatus_t::pageFull)
    {
        status = pageTransfer();

        //write the variable again to a new page
        if (status == writeStatus_t::ok)
            status = writeInternal(address, data);
    }

    return status;
}

bool EmuEEPROM::findValidPage(pageOp_t operation, page_t& page)
{
    auto page1Status = pageStatus(page_t::page1);
    auto page2Status = pageStatus(page_t::page2);

    switch (operation)
    {
    case pageOp_t::write:
        if (page2Status == pageStatus_t::valid)
        {
            if (page1Status == pageStatus_t::receiving)
                page = page_t::page1;
            else
                page = page_t::page2;
        }
        else if (page1Status == pageStatus_t::valid)
        {
            if (page2Status == pageStatus_t::receiving)
                page = page_t::page2;
            else
                page = page_t::page1;
        }
        else
        {
            //no valid page found
            return false;
        }
        break;

    case pageOp_t::read:
        if (page1Status == pageStatus_t::valid)
        {
            page = page_t::page1;
        }
        else if (page2Status == pageStatus_t::valid)
        {
            page = page_t::page2;
        }
        else
        {
            //no valid page found
            return false;
        }
        break;

    default:
        page = page_t::page1;
        break;
    }

    return true;
}

EmuEEPROM::writeStatus_t EmuEEPROM::writeInternal(uint16_t address, uint16_t data)
{
    if (address == 0xFFFF)
        return writeStatus_t::writeError;

    page_t validPage;

    if (!findValidPage(pageOp_t::write, validPage))
        return writeStatus_t::noPage;

    //take into account 4-byte page header
    uint32_t startAddress     = _storageAccess.startAddress(validPage);
    uint32_t pageSize         = _storageAccess.pageSize();
    uint32_t pageStartAddress = startAddress + 4;
    uint32_t pageEndAddress   = startAddress + pageSize - 2;

    auto next = [&pageStartAddress]() {
        pageStartAddress += 4;
    };

    if (_nextAddToWrite != _storageAccess.pageSize())
    {
        if (_nextAddToWrite >= pageEndAddress)
            return writeStatus_t::pageFull;

        if (!_storageAccess.write16(_nextAddToWrite, data))
            return writeStatus_t::writeError;

        /* Set variable virtual address */
        if (!_storageAccess.write16(_nextAddToWrite + 2, address))
            return writeStatus_t::writeError;

        _nextAddToWrite += 4;
        _eepromCache[address] = data;
        return writeStatus_t::ok;
    }
    else
    {
        /* Check each active page address starting from begining */
        while (pageStartAddress < pageEndAddress)
        {
            uint32_t readData = 0;

            if (_storageAccess.read32(pageStartAddress, readData))
            {
                if (readData == 0xFFFFFFFF)
                {
                    if (!_storageAccess.write16(pageStartAddress, data))
                        return writeStatus_t::writeError;

                    /* Set variable virtual address */
                    if (!_storageAccess.write16(pageStartAddress + 2, address))
                        return writeStatus_t::writeError;

                    _nextAddToWrite = pageStartAddress + 4;

                    _eepromCache[address] = data;
                    return writeStatus_t::ok;
                }
                else
                {
                    next();
                }
            }
            else
            {
                next();
            }
        }
    }

    return writeStatus_t::pageFull;
}

EmuEEPROM::writeStatus_t EmuEEPROM::pageTransfer()
{
    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return writeStatus_t::noPage;

    uint32_t newPageAddress = _storageAccess.startAddress(page_t::page1);
    page_t   oldPage        = page_t::page1;

    if (validPage == page_t::page2)
    {
        /* New page address where variable will be moved to */
        newPageAddress = _storageAccess.startAddress(page_t::page1);

        /* Old page ID where variable will be taken from */
        oldPage = page_t::page2;
    }
    else if (validPage == page_t::page1)
    {
        /* New page address  where variable will be moved to */
        newPageAddress = _storageAccess.startAddress(page_t::page2);

        /* Old page ID where variable will be taken from */
        oldPage = page_t::page1;
    }

    if (!_storageAccess.write32(newPageAddress, static_cast<uint32_t>(pageStatus_t::receiving)))
        return writeStatus_t::writeError;

    writeStatus_t eepromStatus;
    _nextAddToWrite = _storageAccess.pageSize();

    //move all variables from one page to another
    //start from last address
    for (uint32_t i = _storageAccess.pageSize() - 4; i >= 4; i -= 4)
    {
        uint32_t data;

        if (_storageAccess.read32(_storageAccess.startAddress(oldPage) + i, data))
        {
            if (data == 0xFFFFFFFF)
                continue;    //blank variable

            uint16_t value   = data & 0xFFFF;
            uint16_t address = data >> 16 & 0xFFFF;

            if (address >= maxAddress())
                return writeStatus_t::writeError;

            if (isVarTransfered(address))
                continue;

            //move variable to new active page
            eepromStatus = writeInternal(address, value);

            if (eepromStatus != writeStatus_t::ok)
            {
                return eepromStatus;
            }
            else
            {
                markAsTransfered(address);
            }
        }
    }

    //format old page
    _storageAccess.erasePage(oldPage);

    if (!_storageAccess.write32(_storageAccess.startAddress(oldPage), static_cast<uint32_t>(pageStatus_t::formatted)))
        return writeStatus_t::writeError;

    /* Set new Page status to VALID_PAGE status */
    if (!_storageAccess.write32(newPageAddress, static_cast<uint32_t>(pageStatus_t::valid)))
        return writeStatus_t::writeError;

    //reset transfered flags
    std::fill(_varTransferedArray.begin(), _varTransferedArray.end(), 0);

    return writeStatus_t::ok;
}

EmuEEPROM::pageStatus_t EmuEEPROM::pageStatus(page_t page)
{
    uint32_t     data;
    pageStatus_t status;

    switch (page)
    {
    case page_t::page1:
        _storageAccess.read32(_storageAccess.startAddress(page_t::page1), data);
        break;

    case page_t::page2:
        _storageAccess.read32(_storageAccess.startAddress(page_t::page2), data);
        break;

    case page_t::pageFactory:
        _storageAccess.read32(_storageAccess.startAddress(page_t::pageFactory), data);
        break;

    default:
        return pageStatus_t::erased;
    }

    switch (data)
    {
    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::erased):
        status = EmuEEPROM::pageStatus_t::erased;
        break;

    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::receiving):
        status = EmuEEPROM::pageStatus_t::receiving;
        break;

    case static_cast<uint32_t>(EmuEEPROM::pageStatus_t::valid):
        status = EmuEEPROM::pageStatus_t::valid;
        break;

    default:
        status = EmuEEPROM::pageStatus_t::formatted;
        break;
    }

    return status;
}

bool EmuEEPROM::isVarTransfered(uint16_t address)
{
    uint16_t arrayIndex = address / 8;
    uint16_t varIndex   = address - 8 * arrayIndex;

    return (_varTransferedArray.at(arrayIndex) >> varIndex) & 0x01;
}

void EmuEEPROM::markAsTransfered(uint16_t address)
{
    uint16_t arrayIndex = address / 8;
    uint16_t varIndex   = address - 8 * arrayIndex;

    _varTransferedArray.at(arrayIndex) |= (1UL << varIndex);
}

bool EmuEEPROM::cache()
{
    page_t validPage;
    std::fill(_varTransferedArray.begin(), _varTransferedArray.end(), 0);

    if (!findValidPage(pageOp_t::write, validPage))
        return false;

    for (uint32_t i = _storageAccess.pageSize() - 4; i >= 4; i -= 4)
    {
        uint32_t data;

        if (_storageAccess.read32(_storageAccess.startAddress(validPage) + i, data))
        {
            if (data == 0xFFFFFFFF)
                continue;    //blank variable

            uint16_t value   = data & 0xFFFF;
            uint16_t address = data >> 16 & 0xFFFF;

            if (address >= maxAddress())
                return false;

            if (isVarTransfered(address))
                continue;

            //copy variable to cache
            _eepromCache[address] = value;
            markAsTransfered(address);
        }
    }

    std::fill(_varTransferedArray.begin(), _varTransferedArray.end(), 0);
    return true;
}

const uint32_t EmuEEPROM::maxAddress() const
{
    return (_storageAccess.pageSize() / 4) - 1;
}