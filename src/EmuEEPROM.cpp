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

#include <string.h>
#include "EmuEEPROM.h"

bool EmuEEPROM::init()
{
    if (!_storageAccess.init())
        return false;

    _nextAddToWrite = 0;

    auto page1Status = pageStatus(page_t::page1);
    auto page2Status = pageStatus(page_t::page2);

    // check for invalid header states and repair if necessary
    switch (page1Status)
    {
    case pageStatus_t::erased:
        if (page2Status == pageStatus_t::valid)
        {
            // page 1 erased, page 2 valid
            // format page 1 properly
            _storageAccess.erasePage(page_t::page1);

            if (!write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;
        }
        else if (page2Status == pageStatus_t::receiving)
        {
            // page 1 erased, page 2 in receive state
            // again, format page 1 properly
            _storageAccess.erasePage(page_t::page1);

            if (!write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;

            // mark page2 as valid
            if (!write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            // format both pages and set first page as valid
            if (!format())
                return false;
        }
        break;

    case pageStatus_t::receiving:
        if (page2Status == pageStatus_t::valid)
        {
            // page 1 in receive state, page 2 valid
            // restart the transfer process by first erasing page 1 and then performing page transfer
            _storageAccess.erasePage(page_t::page1);

            if (pageTransfer() != writeStatus_t::ok)
            {
                // error occured, try to format
                if (!format())
                    return false;
            }
        }
        else if (page2Status == pageStatus_t::erased)
        {
            // page 1 in receive state, page 2 erased
            // erase page 2
            _storageAccess.erasePage(page_t::page2);

            if (!write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;

            // mark page 1 as valid
            if (!write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            // invalid state
            if (!format())
                return false;
        }
        break;

    case pageStatus_t::valid:
        if (page2Status == pageStatus_t::valid)
        {
            // invalid state
            if (!format())
                return false;
        }
        else if (page2Status == pageStatus_t::erased)
        {
            // page 1 valid, page 2 erased
            // format page2
            _storageAccess.erasePage(page_t::page2);

            if (!write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
                return false;
        }
        else if (page2Status == pageStatus_t::formatted)
        {
            // nothing to do
        }
        else
        {
            // page 1 valid, page 2 in receive state
            // restart the transfer process by first erasing page 2 and then performing page transfer
            _storageAccess.erasePage(page_t::page2);

            if (pageTransfer() != writeStatus_t::ok)
            {
                // error occured, try to format
                if (!format())
                    return false;
            }
        }
        break;

    default:
    {
        if (!format())
            return false;
    }
    break;
    }

    return true;
}

bool EmuEEPROM::format()
{
    // erase both pages and set page 1 as valid

    if (!_storageAccess.erasePage(page_t::page1))
        return false;

    if (!_storageAccess.erasePage(page_t::page2))
        return false;

    // copy contents from factory page to page 1 if the page is in correct status
    if (_useFactoryPage && (pageStatus(page_t::pageFactory) == pageStatus_t::valid))
    {
        for (uint32_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i += 4)
        {
            auto data = read32(_storageAccess.startAddress(page_t::pageFactory) + i);

            if (data == 0xFFFFFFFF)
                break;    // empty block, no need to go further

            if (!write32(_storageAccess.startAddress(page_t::page1) + i, data))
                return false;
        }
    }
    else
    {
        // set valid status to page1
        if (!write32(_storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
            return false;

        if (!write32(_storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::formatted)))
            return false;
    }

    _nextAddToWrite = 0;

    return true;
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t index, char* data, uint16_t& length, const uint16_t maxLength)
{
    if (index == 0xFFFFFFFF)
        return readStatus_t::noIndex;

    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return readStatus_t::noPage;

    readStatus_t status = readStatus_t::noIndex;

    memset(data, 0x00, maxLength);

    // take into account 4-byte page header
    const uint32_t startAddress     = _storageAccess.startAddress(validPage);
    const uint32_t pageStartAddress = startAddress + 4;
    uint32_t       readAddress      = startAddress + EMU_EEPROM_PAGE_SIZE;

    auto next = [&readAddress]() {
        readAddress = readAddress - 4;
    };

    if (_nextAddToWrite)
    {
        //_nextAddToWrite contains next address to which new data will be written
        // when reading, skip all addresses after that one to speed up the process
        readAddress = _nextAddToWrite - 4;
    }

    // check each active page address starting from end
    while (readAddress > pageStartAddress)
    {
        auto readValue = read32(readAddress);

        if (readValue == _contentEndMarker)
        {
            next();
            readValue = read32(readAddress);

            if (readValue == index)
            {
                // read the data in following order:
                // size (2 bytes)
                // crc (2 bytes)
                // content (size bytes)

                readAddress -= 2;
                length = read16(readAddress);

                // use extra character for termination
                if ((length + 1) >= maxLength)
                    return readStatus_t::bufferTooSmall;

                readAddress -= 2;
                auto crcRetrieved = read16(readAddress);

                readAddress--;

                uint16_t dataCount = 0;
                uint16_t crcActual = 0;

                // make sure data is read in correct order
                for (int i = paddingBytes(length) + length - 1; i >= 0; i--)
                {
                    auto value = read8(readAddress - i);

                    // don't append padding bytes
                    if (dataCount < length)
                    {
                        data[dataCount] = value;
                        crcActual       = xmodemCRCUpdate(crcActual, value);
                    }

                    if (++dataCount >= maxLength)
                        break;
                }

                if (crcActual != crcRetrieved)
                    return readStatus_t::invalidCrc;

                data[length] = '\0';

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

EmuEEPROM::writeStatus_t EmuEEPROM::write(const uint32_t index, const char* data)
{
    auto length = strlen(data);

    // no amount of page transfers is going to make this fit
    if (entrySize(length) >= (EMU_EEPROM_PAGE_SIZE - sizeof(pageStatus_t) - sizeof(_contentEndMarker)))
        return writeStatus_t::pageFull;

    writeStatus_t status;

    status = writeInternal(index, data, length);

    if (status == writeStatus_t::pageFull)
    {
        status = pageTransfer();

        if (status == writeStatus_t::ok)
            status = writeInternal(index, data, length);
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
            // no valid page found
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
            // no valid page found
            return false;
        }
        break;

    default:
        page = page_t::page1;
        break;
    }

    return true;
}

EmuEEPROM::writeStatus_t EmuEEPROM::writeInternal(uint32_t index, const char* data, uint16_t length)
{
    if (index == 0xFFFFFFFF)
        return writeStatus_t::writeError;

    page_t validPage;

    if (!findValidPage(pageOp_t::write, validPage))
        return writeStatus_t::noPage;

    // take into account 4-byte page header
    const uint32_t startAddress   = _storageAccess.startAddress(validPage);
    const uint32_t pageEndAddress = startAddress + EMU_EEPROM_PAGE_SIZE;
    uint32_t       writeAddress   = startAddress + sizeof(pageStatus_t);

    auto next = [&writeAddress]() {
        writeAddress += 4;
    };

    auto write = [&]() {
        // write the data in following order:
        // content
        // crc
        // size (without added padding)
        // index
        // end marker

        uint16_t crc = 0x0000;

        if ((pageEndAddress - writeAddress) < entrySize(length))
            return writeStatus_t::pageFull;

        // content
        for (uint16_t i = 0; i < length; i++)
        {
            if (!write8(writeAddress++, data[i]))
                return writeStatus_t::writeError;

            crc = xmodemCRCUpdate(crc, data[i]);
        }

        // padding
        for (uint16_t i = 0; i < paddingBytes(length); i++)
        {
            if (!write8(writeAddress++, 0xFF))
                return writeStatus_t::writeError;
        }
        //

        // crc
        if (!write16(writeAddress, crc))
            return writeStatus_t::writeError;

        writeAddress += 2;
        //

        // size
        if (!write16(writeAddress, length))
            return writeStatus_t::writeError;

        writeAddress += 2;
        //

        // index
        if (!write32(writeAddress, index))
            return writeStatus_t::writeError;

        writeAddress += 4;
        //

        // end marker
        if (!write32(writeAddress, _contentEndMarker))
            return writeStatus_t::writeError;

        _nextAddToWrite = writeAddress + 4;
        //

        return writeStatus_t::ok;
    };

    if (_nextAddToWrite)
    {
        if (_nextAddToWrite >= pageEndAddress)
            return writeStatus_t::pageFull;

        if ((_nextAddToWrite - writeAddress) < entrySize(length))
            return writeStatus_t::pageFull;

        writeAddress = _nextAddToWrite;

        return write();
    }
    else
    {
        // check each active page address starting from beginning
        while (writeAddress < pageEndAddress)
        {
            if (read32(writeAddress) == _contentEndMarker)
            {
                next();
                return write();
            }
            else
            {
                next();
            }
        }

        // no content end marker set - nothing is written yet
        writeAddress = startAddress + sizeof(pageStatus_t);

        return write();
    }

    return writeStatus_t::pageFull;
}

EmuEEPROM::writeStatus_t EmuEEPROM::pageTransfer()
{
    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return writeStatus_t::noPage;

    _nextAddToWrite = _storageAccess.startAddress(page_t::page1);
    page_t oldPage  = page_t::page1;

    if (validPage == page_t::page2)
    {
        // new page address where content will be moved to
        _nextAddToWrite = _storageAccess.startAddress(page_t::page1);

        // old page ID where content will be taken from
        oldPage = page_t::page2;
    }
    else if (validPage == page_t::page1)
    {
        // new page address where content will be moved to
        _nextAddToWrite = _storageAccess.startAddress(page_t::page2);

        // old page ID where content will be taken from
        oldPage = page_t::page1;
    }

    if (!write32(_nextAddToWrite, static_cast<uint32_t>(pageStatus_t::receiving)))
        return writeStatus_t::writeError;

    _nextAddToWrite += sizeof(pageStatus_t);

    if (!write32(_nextAddToWrite, _contentEndMarker))
        return writeStatus_t::writeError;

    _nextAddToWrite += 4;

    // move all data from one page to another
    // start from last address
    for (uint32_t i = EMU_EEPROM_PAGE_SIZE - 4; i > 4; i -= 4)
    {
        uint32_t address = _storageAccess.startAddress(oldPage) + i;

        auto readValue = read32(address);

        if (readValue == _contentEndMarker)
        {
            address -= sizeof(_contentEndMarker);
            auto index = read32(address);

            if (isIndexTransfered(index))
                continue;

            uint16_t length = 0;
            uint16_t crc    = 0;

            address -= sizeof(uint16_t);
            length = read16(address);

            address -= sizeof(uint16_t);
            crc = read16(address);

            address--;

            // at this point we can start writing data to the new page
            // content first

            for (int dataIndex = paddingBytes(length) + length - 1; dataIndex >= 0; dataIndex--)
            {
                auto value = read8(address - dataIndex);

                if (!write8(_nextAddToWrite++, value))
                    return writeStatus_t::writeError;
            }

            // skip this block of content but make sure that next loop
            // iteration reads the content end marker
            i -= (entrySize(length) - sizeof(_contentEndMarker));

            // crc
            if (!write16(_nextAddToWrite, crc))
                return writeStatus_t::writeError;

            _nextAddToWrite += 2;

            // size
            if (!write16(_nextAddToWrite, length))
                return writeStatus_t::writeError;

            _nextAddToWrite += 2;

            // index
            if (!write32(_nextAddToWrite, index))
                return writeStatus_t::writeError;

            _nextAddToWrite += 4;

            // content end marker
            if (!write32(_nextAddToWrite, _contentEndMarker))
                return writeStatus_t::writeError;

            _nextAddToWrite += 4;

            markAsTransfered(index);
        }
    }

    // format old page
    _storageAccess.erasePage(oldPage);

    if (!write32(_storageAccess.startAddress(oldPage), static_cast<uint32_t>(pageStatus_t::formatted)))
        return writeStatus_t::writeError;

    // set new page status to VALID_PAGE status
    if (!write32(_storageAccess.startAddress(oldPage == page_t::page1 ? page_t::page2 : page_t::page1),
                 static_cast<uint32_t>(pageStatus_t::valid)))
        return writeStatus_t::writeError;

    // reset transfered flags
    std::fill(_indexTransferedArray.begin(), _indexTransferedArray.end(), 0);

    return writeStatus_t::ok;
}

EmuEEPROM::pageStatus_t EmuEEPROM::pageStatus(page_t page)
{
    uint32_t     data;
    pageStatus_t status;

    switch (page)
    {
    case page_t::page1:
        data = read32(_storageAccess.startAddress(page_t::page1));
        break;

    case page_t::page2:
        data = read32(_storageAccess.startAddress(page_t::page2));
        break;

    case page_t::pageFactory:
        data = read32(_storageAccess.startAddress(page_t::pageFactory));
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

bool EmuEEPROM::isIndexTransfered(uint32_t index)
{
    uint32_t arrayIndex = index / 32;
    uint32_t varIndex   = index - 32 * arrayIndex;

    return (_indexTransferedArray.at(arrayIndex) >> varIndex) & 0x01;
}

void EmuEEPROM::markAsTransfered(uint32_t index)
{
    uint32_t arrayIndex = index / 32;
    uint32_t varIndex   = index - 32 * arrayIndex;

    _indexTransferedArray.at(arrayIndex) |= (1UL << varIndex);
}

bool EmuEEPROM::write8(uint32_t address, uint8_t data)
{
    return _storageAccess.write(address, data);
}

bool EmuEEPROM::write16(uint32_t address, uint16_t data)
{
    for (size_t i = 0; i < sizeof(uint16_t); i++)
    {
        if (!_storageAccess.write(address + i, static_cast<uint8_t>((data) >> (8 * i) & 0xFF)))
            return false;
    }

    return true;
}

bool EmuEEPROM::write32(uint32_t address, uint32_t data)
{
    for (size_t i = 0; i < sizeof(uint32_t); i++)
    {
        if (!_storageAccess.write(address + i, static_cast<uint8_t>((data) >> (8 * i) & 0xFF)))
            return false;
    }

    return true;
}

uint8_t EmuEEPROM::read8(uint32_t address)
{
    return _storageAccess.read(address);
}

uint16_t EmuEEPROM::read16(uint32_t address)
{
    uint8_t  temp[2] = {};
    uint16_t data    = 0;

    for (size_t i = 0; i < 2; i++)
        temp[i] = _storageAccess.read(address + i);

    data = temp[1];
    data <<= 8;
    data |= temp[0];

    return data;
}

uint32_t EmuEEPROM::read32(uint32_t address)
{
    uint8_t  temp[4] = {};
    uint32_t data    = 0;

    for (size_t i = 0; i < 4; i++)
        temp[i] = _storageAccess.read(address + i);

    data = temp[3];
    data <<= 8;
    data |= temp[2];
    data <<= 8;
    data |= temp[1];
    data <<= 8;
    data |= temp[0];

    return data;
}

uint16_t EmuEEPROM::xmodemCRCUpdate(uint16_t crc, char data)
{
    // update existing crc

    crc = crc ^ (static_cast<uint16_t>(data) << 8);

    for (int i = 0; i < 8; i++)
    {
        if (crc & 0x8000)
        {
            crc = (crc << 1) ^ 0x1021;
        }
        else
        {
            crc <<= 1;
        }
    }

    return crc;
}

bool EmuEEPROM::indexExists(uint32_t index)
{
    if (index == 0xFFFFFFFF)
        return false;

    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return false;

    // take into account 4-byte page header
    const uint32_t startAddress     = _storageAccess.startAddress(validPage);
    const uint32_t pageStartAddress = startAddress + 4;
    uint32_t       readAddress      = startAddress + EMU_EEPROM_PAGE_SIZE;

    auto next = [&readAddress]() {
        readAddress = readAddress - 4;
    };

    if (_nextAddToWrite)
    {
        //_nextAddToWrite contains next address to which new data will be written
        // when reading, skip all addresses after that one to speed up the process
        readAddress = _nextAddToWrite - 4;
    }

    // check each active page address starting from end
    while (readAddress > pageStartAddress)
    {
        auto readValue = read32(readAddress);

        if (readValue == _contentEndMarker)
        {
            next();
            readValue = read32(readAddress);

            if (readValue == index)
            {
                return true;
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

    return false;
}
