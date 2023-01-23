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

#include <string.h>
#include "EmuEEPROM.h"

bool EmuEEPROM::init()
{
    if (!_storageAccess.init())
    {
        return false;
    }

    _nextOffsetToWrite = 0;

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

            if (!writePageStatus(page_t::PAGE_1, pageStatus_t::FORMATTED))
            {
                return false;
            }
        }
        else if (page2Status == pageStatus_t::RECEIVING)
        {
            // page 1 erased, page 2 in receive state
            // again, format page 1 properly
            _storageAccess.erasePage(page_t::PAGE_1);

            if (!writePageStatus(page_t::PAGE_1, pageStatus_t::FORMATTED))
            {
                return false;
            }

            // mark page2 as valid
            if (!writePageStatus(page_t::PAGE_2, pageStatus_t::VALID))
            {
                return false;
            }
        }
        else
        {
            // format both pages and set first page as valid
            if (!format())
            {
                return false;
            }
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
        }
        else if (page2Status == pageStatus_t::ERASED)
        {
            // page 1 in receive state, page 2 erased
            // erase page 2
            _storageAccess.erasePage(page_t::PAGE_2);

            if (!writePageStatus(page_t::PAGE_2, pageStatus_t::FORMATTED))
            {
                return false;
            }

            // mark page 1 as valid
            if (!writePageStatus(page_t::PAGE_1, pageStatus_t::VALID))
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

            if (!writePageStatus(page_t::PAGE_2, pageStatus_t::FORMATTED))
            {
                return false;
            }
        }
        else if (page2Status == pageStatus_t::FORMATTED)
        {
            // nothing to do
        }
        else
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
        }
    }
    break;

    default:
    {
        if (!format())
        {
            return false;
        }
    }
    break;
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

    // copy contents from factory page to page 1 if the page is in correct status
    if (USE_FACTORY_PAGE && (pageStatus(page_t::PAGE_FACTORY) == pageStatus_t::VALID))
    {
        for (uint32_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i += EMUEEPROM_WRITE_ALIGNMENT)
        {
            static constexpr size_t TOTAL_CHUNKS = EMUEEPROM_WRITE_ALIGNMENT / 4;

            if (!_storageAccess.startWrite(page_t::PAGE_1, i))
            {
                return false;
            }

            for (size_t chunk = 0; chunk < TOTAL_CHUNKS; chunk++)
            {
                uint32_t offset = i + (chunk * 4);
                auto     result = read32(page_t::PAGE_FACTORY, offset);

                if (result == std::nullopt)
                {
                    return false;
                }

                if (!write32(page_t::PAGE_1, offset, *result))
                {
                    return false;
                }
            }

            if (!_storageAccess.endWrite(page_t::PAGE_1))
            {
                return false;
            }
        }
    }
    else
    {
        // set valid status to page1
        if (!writePageStatus(page_t::PAGE_1, pageStatus_t::VALID))
        {
            return false;
        }

        if (!writePageStatus(page_t::PAGE_2, pageStatus_t::FORMATTED))
        {
            return false;
        }
    }

    _nextOffsetToWrite = 0;

    return true;
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t index, char* data, uint16_t& length, const uint16_t maxLength)
{
    if (index == 0xFFFFFFFF)
    {
        return readStatus_t::NO_INDEX;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::READ, validPage))
    {
        return readStatus_t::NO_PAGE;
    }

    readStatus_t status = readStatus_t::NO_INDEX;

    memset(data, 0x00, maxLength);

    uint32_t readOffset = EMU_EEPROM_PAGE_SIZE - 4;

    auto next = [&readOffset]()
    {
        readOffset = readOffset - 4;
    };

    if (_nextOffsetToWrite)
    {
        //_nextOffsetToWrite contains next offset to which new data will be written
        // when reading, skip all offsets after that one to speed up the process
        readOffset = _nextOffsetToWrite - 4;
    }

    // check each active page offset starting from end
    // take into account page header / alignment
    while (readOffset > EMUEEPROM_WRITE_ALIGNMENT)
    {
        auto retrieved = read32(validPage, readOffset);

        if (retrieved == std::nullopt)
        {
            return readStatus_t::READ_ERROR;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            next();
            retrieved = read32(validPage, readOffset);

            if (retrieved == std::nullopt)
            {
                return readStatus_t::READ_ERROR;
            }

            if (*retrieved == index)
            {
                // read the data in following order:
                // size (2 bytes)
                // crc (2 bytes)
                // content (size bytes)

                readOffset -= 2;
                auto lengthRetrieved = read16(validPage, readOffset);

                if (lengthRetrieved == std::nullopt)
                {
                    return readStatus_t::READ_ERROR;
                }

                length = *lengthRetrieved;

                // use extra character for termination
                if ((length + 1) >= maxLength)
                {
                    return readStatus_t::BUFFER_TOO_SMALL;
                }

                readOffset -= 2;
                auto crcRetrieved = read16(validPage, readOffset);

                if (crcRetrieved == std::nullopt)
                {
                    return readStatus_t::READ_ERROR;
                }

                readOffset--;

                uint16_t dataCount = 0;
                uint16_t crcActual = 0;

                // make sure data is read in correct order
                for (int i = paddingBytes(length) + length - 1; i >= 0; i--)
                {
                    auto content = read8(validPage, readOffset - i);

                    if (content == std::nullopt)
                    {
                        return readStatus_t::READ_ERROR;
                    }

                    // don't append padding bytes
                    if (dataCount < length)
                    {
                        data[dataCount] = *content;
                        crcActual       = xmodemCRCUpdate(crcActual, *content);
                    }

                    if (++dataCount >= maxLength)
                    {
                        break;
                    }
                }

                if (crcActual != *crcRetrieved)
                {
                    return readStatus_t::INVALID_CRC;
                }

                data[length] = '\0';

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

EmuEEPROM::writeStatus_t EmuEEPROM::write(const uint32_t index, const char* data)
{
    if (data == nullptr)
    {
        return writeStatus_t::DATA_ERROR;
    }

    auto length = strlen(data);

    if (!length)
    {
        return writeStatus_t::DATA_ERROR;
    }

    // no amount of page transfers is going to make this fit
    if (entrySize(length) >= (EMU_EEPROM_PAGE_SIZE - sizeof(pageStatus_t) - sizeof(CONTENT_END_MARKER)))
    {
        return writeStatus_t::PAGE_FULL;
    }

    writeStatus_t status;

    status = writeInternal(index, data, length);

    if (status == writeStatus_t::PAGE_FULL)
    {
        status = pageTransfer();

        if (status == writeStatus_t::OK)
        {
            status = writeInternal(index, data, length);
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

EmuEEPROM::writeStatus_t EmuEEPROM::writeInternal(uint32_t index, const char* data, uint16_t length)
{
    if (index == 0xFFFFFFFF)
    {
        return writeStatus_t::WRITE_ERROR;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::WRITE, validPage))
    {
        return writeStatus_t::NO_PAGE;
    }

    const uint32_t PAGE_END_OFFSET = EMU_EEPROM_PAGE_SIZE;
    uint32_t       writeOffset     = EMUEEPROM_WRITE_ALIGNMENT;

    auto next = [&]()
    {
        writeOffset += EMUEEPROM_WRITE_ALIGNMENT;
    };

    auto write = [&]()
    {
        // write the data in following order:
        // content
        // crc
        // size (without added padding)
        // index
        // end marker

        uint16_t crc = 0x0000;

        if ((PAGE_END_OFFSET - writeOffset) < entrySize(length))
        {
            return writeStatus_t::PAGE_FULL;
        }

        if (!_storageAccess.startWrite(validPage, writeOffset))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        // content
        for (uint16_t i = 0; i < length; i++)
        {
            if (!write8(validPage, writeOffset++, data[i]))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            crc = xmodemCRCUpdate(crc, data[i]);
        }

        // padding
        for (uint16_t i = 0; i < paddingBytes(length); i++)
        {
            if (!write8(validPage, writeOffset++, 0xFF))
            {
                return writeStatus_t::WRITE_ERROR;
            }
        }
        //

        // crc
        if (!write16(validPage, writeOffset, crc))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        writeOffset += 2;
        //

        // size
        if (!write16(validPage, writeOffset, length))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        writeOffset += 2;
        //

        // index
        if (!write32(validPage, writeOffset, index))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        writeOffset += 4;
        //

        // end marker
        if (!write32(validPage, writeOffset, CONTENT_END_MARKER))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        _nextOffsetToWrite = writeOffsetAligned(writeOffset);
        //

        if (!_storageAccess.endWrite(validPage))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        return writeStatus_t::OK;
    };

    if (_nextOffsetToWrite)
    {
        if (_nextOffsetToWrite >= PAGE_END_OFFSET)
        {
            return writeStatus_t::PAGE_FULL;
        }

        if ((_nextOffsetToWrite + entrySize(length)) >= PAGE_END_OFFSET)
        {
            return writeStatus_t::PAGE_FULL;
        }

        writeOffset = _nextOffsetToWrite;

        return write();
    }

    // check each active page address starting from beginning
    while (writeOffset < PAGE_END_OFFSET)
    {
        auto retrieved = read32(validPage, writeOffset);

        if (retrieved == std::nullopt)
        {
            return writeStatus_t::DATA_ERROR;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            next();
            return write();
        }

        next();
    }

    // no content end marker set - nothing is written yet
    writeOffset = writeOffsetAligned(sizeof(pageStatus_t));

    return write();
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

        // old page ID where content will be taken from
        oldPage = page_t::PAGE_2;
    }
    else if (oldPage == page_t::PAGE_1)
    {
        // new page where content will be moved to
        newPage = page_t::PAGE_2;

        // old page ID where content will be taken from
        oldPage = page_t::PAGE_1;
    }
    else
    {
        return writeStatus_t::NO_PAGE;
    }

    if (!writePageStatus(newPage, pageStatus_t::RECEIVING))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    _nextOffsetToWrite = writeOffsetAligned(sizeof(pageStatus_t));

    if (!_storageAccess.startWrite(newPage, _nextOffsetToWrite))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    if (!write32(newPage, _nextOffsetToWrite, CONTENT_END_MARKER))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    if (!_storageAccess.endWrite(newPage))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    _nextOffsetToWrite = writeOffsetAligned(_nextOffsetToWrite);

    // move all data from one page to another
    // start from last address
    for (uint32_t i = EMU_EEPROM_PAGE_SIZE - 4; i > 4; i -= 4)
    {
        uint32_t offset = i;

        auto retrieved = read32(oldPage, offset);

        if (retrieved == std::nullopt)
        {
            return writeStatus_t::DATA_ERROR;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            offset -= sizeof(CONTENT_END_MARKER);
            auto indexRetrieved = read32(oldPage, offset);

            if (indexRetrieved == std::nullopt)
            {
                return writeStatus_t::DATA_ERROR;
            }

            uint32_t index = *indexRetrieved;

            if (isIndexTransfered(index))
            {
                continue;
            }

            offset -= sizeof(uint16_t);
            auto lengthRetrieved = read16(oldPage, offset);

            if (lengthRetrieved == std::nullopt)
            {
                return writeStatus_t::DATA_ERROR;
            }

            offset -= sizeof(uint16_t);
            auto crcRetrieved = read16(oldPage, offset);

            if (crcRetrieved == std::nullopt)
            {
                return writeStatus_t::DATA_ERROR;
            }

            offset--;

            uint16_t length = *lengthRetrieved;
            uint16_t crc    = *crcRetrieved;

            // at this point we can start writing data to the new page
            // content first

            if (!_storageAccess.startWrite(newPage, _nextOffsetToWrite))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            for (int dataIndex = paddingBytes(length) + length - 1; dataIndex >= 0; dataIndex--)
            {
                auto content = read8(oldPage, offset - dataIndex);

                if (content == std::nullopt)
                {
                    return writeStatus_t::DATA_ERROR;
                }

                if (!write8(newPage, _nextOffsetToWrite++, *content))
                {
                    return writeStatus_t::WRITE_ERROR;
                }
            }

            // skip this block of content but make sure that next loop
            // iteration reads the content end marker
            i -= (entrySize(length) - sizeof(CONTENT_END_MARKER));

            // crc
            if (!write16(newPage, _nextOffsetToWrite, crc))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            _nextOffsetToWrite += 2;

            // size
            if (!write16(newPage, _nextOffsetToWrite, length))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            _nextOffsetToWrite += 2;

            // index
            if (!write32(newPage, _nextOffsetToWrite, index))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            _nextOffsetToWrite += 4;

            // content end marker
            if (!write32(newPage, _nextOffsetToWrite, CONTENT_END_MARKER))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            if (!_storageAccess.endWrite(newPage))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            _nextOffsetToWrite = writeOffsetAligned(_nextOffsetToWrite);

            markAsTransfered(index);
        }
    }

    // format old page
    _storageAccess.erasePage(oldPage);

    if (!writePageStatus(oldPage, pageStatus_t::FORMATTED))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    // set new page status to VALID_PAGE status
    if (!writePageStatus(newPage, pageStatus_t::VALID))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    // reset transfered flags
    std::fill(_indexTransferedArray.begin(), _indexTransferedArray.end(), 0);

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
        auto retrieved = read32(page_t::PAGE_1, 0);

        if (retrieved == std::nullopt)
        {
            return pageStatus_t::ERASED;
        }

        data = *retrieved;
    }
    break;

    case page_t::PAGE_2:
    {
        auto retrieved = read32(page_t::PAGE_2, 0);

        if (retrieved == std::nullopt)
        {
            return pageStatus_t::ERASED;
        }

        data = *retrieved;
    }
    break;

    case page_t::PAGE_FACTORY:
    {
        auto retrieved = read32(page_t::PAGE_FACTORY, 0);

        if (retrieved == std::nullopt)
        {
            return pageStatus_t::ERASED;
        }

        data = *retrieved;
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

bool EmuEEPROM::write8(page_t page, uint32_t offset, uint8_t data)
{
    return _storageAccess.write(page, offset, data);
}

bool EmuEEPROM::write16(page_t page, uint32_t offset, uint16_t data)
{
    for (size_t i = 0; i < sizeof(uint16_t); i++)
    {
        if (!_storageAccess.write(page, offset + i, static_cast<uint8_t>((data) >> (8 * i) & 0xFF)))
        {
            return false;
        }
    }

    return true;
}

bool EmuEEPROM::write32(page_t page, uint32_t offset, uint32_t data)
{
    for (size_t i = 0; i < sizeof(uint32_t); i++)
    {
        if (!_storageAccess.write(page, offset + i, static_cast<uint8_t>((data) >> (8 * i) & 0xFF)))
        {
            return false;
        }
    }

    return true;
}

std::optional<uint8_t> EmuEEPROM::read8(page_t page, uint32_t offset)
{
    uint8_t data;

    if (_storageAccess.read(page, offset, data))
    {
        return data;
    }

    return std::nullopt;
}

std::optional<uint16_t> EmuEEPROM::read16(page_t page, uint32_t offset)
{
    uint8_t  temp[2] = {};
    uint16_t data    = 0;

    for (size_t i = 0; i < 2; i++)
    {
        auto retrieved = read8(page, offset + i);

        if (retrieved == std::nullopt)
        {
            return std::nullopt;
        }

        temp[i] = *retrieved;
    }

    data = temp[1];
    data <<= 8;
    data |= temp[0];

    return data;
}

std::optional<uint32_t> EmuEEPROM::read32(page_t page, uint32_t offset)
{
    uint8_t  temp[4] = {};
    uint32_t data    = 0;

    for (size_t i = 0; i < 4; i++)
    {
        auto retrieved = read8(page, offset + i);

        if (retrieved == std::nullopt)
        {
            return std::nullopt;
        }

        temp[i] = *retrieved;
    }

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
    {
        return false;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::READ, validPage))
    {
        return false;
    }

    uint32_t readOffset = EMU_EEPROM_PAGE_SIZE - 4;

    auto next = [&readOffset]()
    {
        readOffset = readOffset - 4;
    };

    if (_nextOffsetToWrite)
    {
        //_nextOffsetToWrite contains next offset to which new data will be written
        // when reading, skip all offsets after that one to speed up the process
        readOffset = _nextOffsetToWrite - 4;
    }

    // check each active page offset starting from end
    while (readOffset >= EMUEEPROM_WRITE_ALIGNMENT)
    {
        auto retrieved = read32(validPage, readOffset);

        if (retrieved == std::nullopt)
        {
            return false;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            next();
            retrieved = read32(validPage, readOffset);

            if (retrieved == std::nullopt)
            {
                return false;
            }

            if (*retrieved == index)
            {
                return true;
            }

            next();
        }
        else
        {
            next();
        }
    }

    return false;
}

bool EmuEEPROM::writePageStatus(page_t page, pageStatus_t status)
{
    if (_storageAccess.startWrite(page, 0))
    {
        if (write32(page, 0, static_cast<uint32_t>(status)))
        {
            return _storageAccess.endWrite(page);
        }
    }

    return false;
}
