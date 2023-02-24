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
    resetTransferedIndexes();

    if (!_storageAccess.init())
    {
        return false;
    }

    auto page1Status = pageStatus(page_t::PAGE_1);
    auto page2Status = pageStatus(page_t::PAGE_2);

    // check for invalid header states and repair if necessary

    switch (page1Status)
    {
    case pageStatus_t::ERASED:
    {
        // page erased but formatted marker not set
        // repeat the erasure in case power loss or other error occured
        if (!erasePage(page_t::PAGE_1))
        {
            return false;
        }
    }
        [[fallthrough]];

    case pageStatus_t::FORMATTED:
    {
        switch (page2Status)
        {
        case pageStatus_t::ERASED:
        {
            // page erased but formatted marker not set
            // repeat the erasure in case power loss or other error occured
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }
        }

            [[fallthrough]];

        case pageStatus_t::FORMATTED:
        {
            // both pages are formatted - mark the first one as active
            if (!writePageStatus(page_t::PAGE_1, pageStatus_t::ACTIVE))
            {
                return false;
            }
        }
        break;

        case pageStatus_t::RECEIVE:
        {
            // invalid state: page 1 should be in full state
            // erase page 2 and mark the first one as active
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }

            if (!writePageStatus(page_t::PAGE_1, pageStatus_t::ACTIVE))
            {
                return false;
            }
        }
        break;

        case pageStatus_t::ACTIVE:
        {
            // page 1 formatted, page 2 active
            // nothing to do
        }
        break;

        case pageStatus_t::FULL:
        {
            // perform page transfer
            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured
                return false;
            }
        }
        break;

        default:
            return false;    // invalid state
        }
    }
    break;

    case pageStatus_t::RECEIVE:
    {
        switch (page2Status)
        {
        case pageStatus_t::ACTIVE:
        {
            // page 2 active, still writeable
            // invalid status for page 1 - format it
            if (!erasePage(page_t::PAGE_1))
            {
                return false;
            }
        }
        break;

        case pageStatus_t::FULL:
        {
            // incomplete page transfer
            // first, format page 1 properly
            if (!erasePage(page_t::PAGE_1))
            {
                return false;
            }

            // perform page transfer
            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured
                return false;
            }
        }
        break;

        default:
        {
            // if page 2 is in ERASED or FORMATTED state, then this is invalid because there is nothing to transfer to page 1
            // if page 2 is in RECEIVE state, this is invalid because 2 pages cannot be in RECEIVE state
            // in either case, format everything

            if (!format())
            {
                return false;
            }
        }
        break;
        }
    }
    break;

    case pageStatus_t::ACTIVE:
    {
        switch (page2Status)
        {
        case pageStatus_t::ERASED:
        {
            // page erased but formatted marker not set
            // repeat the erasure in case power loss or other error occured
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }
        }

            [[fallthrough]];

        case pageStatus_t::FORMATTED:
        {
            // nothing to do
        }
        break;

        default:
        {
            // in any other case just format page 2
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }
        }
        break;
        }
    }
    break;

    case pageStatus_t::FULL:
    {
        switch (page2Status)
        {
        case pageStatus_t::ERASED:
        {
            // page erased but formatted marker not set
            // repeat the erasure in case power loss or other error occured
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }
        }

            [[fallthrough]];

        case pageStatus_t::FORMATTED:
        {
            // page 1 full, page 2 formatted
            // transfer contents
            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured
                return false;
            }
        }
        break;

        case pageStatus_t::RECEIVE:
        {
            // page 1 full, page 2 started to receive
            // format page 2 first and then restart the transfer
            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }

            // transfer contents
            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured
                return false;
            }
        }
        break;

        case pageStatus_t::ACTIVE:
        {
            // page transfer complete but page 1 isn't formatted yet
            if (!erasePage(page_t::PAGE_1))
            {
                return false;
            }
        }
        break;

        case pageStatus_t::FULL:
        {
            // two full pages?
            // format page 2 and attempt to transfer contents (assumption is that page 1 holds data)

            if (!erasePage(page_t::PAGE_2))
            {
                return false;
            }

            // transfer contents
            if (pageTransfer() != writeStatus_t::OK)
            {
                // error occured
                return false;
            }
        }
        break;

        default:
            return false;
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
    }
    break;
    }

    return copyFromFactory() == writeStatus_t::OK;
}

bool EmuEEPROM::format()
{
    // erase both pages and set page 1 as active

    if (!_storageAccess.erasePage(page_t::PAGE_1))
    {
        return false;
    }

    if (!_storageAccess.erasePage(page_t::PAGE_2))
    {
        return false;
    }

    if (!writePageStatus(page_t::PAGE_1, pageStatus_t::ACTIVE))
    {
        return false;
    }

    if (!writePageStatus(page_t::PAGE_1, pageStatus_t::FORMATTED))
    {
        return false;
    }

    return true;
}

EmuEEPROM::writeStatus_t EmuEEPROM::copyFromFactory()
{
    // copy contents from factory page to page 1 if the page is in correct status
    if (USE_FACTORY_PAGE && (pageStatus(page_t::PAGE_FACTORY) == pageStatus_t::ACTIVE))
    {
        page_t activePage;

        if (!findValidPage(pageOp_t::WRITE, activePage))
        {
            return writeStatus_t::NO_PAGE;
        }

        for (uint32_t i = EMU_EEPROM_PAGE_SIZE - 4; i > PAGE_STATUS_BYTES; i -= 4)
        {
            uint32_t readOffset = i;

            auto retrieved = read32(page_t::PAGE_FACTORY, readOffset);

            if (retrieved == std::nullopt)
            {
                return writeStatus_t::DATA_ERROR;
            }

            if (*retrieved == CONTENT_END_MARKER)
            {
                readOffset -= sizeof(CONTENT_END_MARKER);
                auto indexRetrieved = read32(page_t::PAGE_FACTORY, readOffset);

                if (indexRetrieved == std::nullopt)
                {
                    return writeStatus_t::DATA_ERROR;
                }

                uint32_t index = *indexRetrieved;

                // copy only if found content doesn't exist in currently active page
                if (indexExists(index))
                {
                    continue;
                }

                readOffset -= sizeof(uint16_t);
                auto lengthRetrieved = read16(page_t::PAGE_FACTORY, readOffset);

                if (lengthRetrieved == std::nullopt)
                {
                    return writeStatus_t::DATA_ERROR;
                }

                readOffset -= sizeof(uint16_t);
                auto crcRetrieved = read16(page_t::PAGE_FACTORY, readOffset);

                if (crcRetrieved == std::nullopt)
                {
                    return writeStatus_t::DATA_ERROR;
                }

                readOffset--;

                uint16_t length = *lengthRetrieved;
                uint16_t crc    = *crcRetrieved;

                // at this point we can start writing data to the active page
                // content first

                auto writeOffset = nextOffsetToWrite(activePage);

                if (writeOffset == std::nullopt)
                {
                    return writeStatus_t::NO_PAGE;
                }

                if (!_storageAccess.startWrite(activePage, writeOffset.value()))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                for (int dataIndex = paddingBytes(length) + length - 1; dataIndex >= 0; dataIndex--)
                {
                    auto content = read8(page_t::PAGE_FACTORY, readOffset - dataIndex);

                    if (content == std::nullopt)
                    {
                        return writeStatus_t::DATA_ERROR;
                    }

                    if (!write8(activePage, writeOffset.value()++, *content))
                    {
                        return writeStatus_t::WRITE_ERROR;
                    }
                }

                // skip this block of content but make sure that next loop
                // iteration reads the content end marker
                i -= (entrySize(length) - sizeof(CONTENT_END_MARKER));

                // crc
                if (!write16(activePage, writeOffset.value(), crc))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                writeOffset.value() += 2;

                // size
                if (!write16(activePage, writeOffset.value(), length))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                writeOffset.value() += 2;

                // index
                if (!write32(activePage, writeOffset.value(), index))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                writeOffset.value() += 4;

                // content end marker
                if (!write32(activePage, writeOffset.value(), CONTENT_END_MARKER))
                {
                    return writeStatus_t::WRITE_ERROR;
                }

                if (!_storageAccess.endWrite(activePage))
                {
                    return writeStatus_t::WRITE_ERROR;
                }
            }
        }
    }

    return writeStatus_t::OK;
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t index, char* data, uint16_t& length, const uint16_t maxLength)
{
    if (index == INVALID_INDEX)
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

    uint32_t readOffset = EMU_EEPROM_PAGE_SIZE - sizeof(CONTENT_END_MARKER);

    auto previous = [&readOffset]()
    {
        readOffset = readOffset - sizeof(CONTENT_END_MARKER);
    };

    // check each active page offset starting from end
    // take into account page header / alignment
    while (readOffset >= PAGE_STATUS_BYTES)
    {
        auto retrieved = read32(validPage, readOffset);

        if (retrieved == std::nullopt)
        {
            return readStatus_t::READ_ERROR;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            previous();
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

            previous();
        }
        else
        {
            previous();
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
    if (entrySize(length) >= (EMU_EEPROM_PAGE_SIZE - PAGE_STATUS_BYTES - sizeof(CONTENT_END_MARKER)))
    {
        return writeStatus_t::PAGE_FULL;
    }

    writeStatus_t status;

    status = writeInternal(index, data, length);

    if (status == writeStatus_t::PAGE_FULL)
    {
        // mark the currently active write page as full
        page_t fullPage;

        if (!findValidPage(pageOp_t::WRITE, fullPage))
        {
            return writeStatus_t::NO_PAGE;
        }

        if (!writePageStatus(fullPage, pageStatus_t::FULL))
        {
            return writeStatus_t::WRITE_ERROR;
        }

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
    case pageOp_t::READ:
    {
        if ((page1Status == pageStatus_t::ACTIVE) || (page1Status == pageStatus_t::FULL))
        {
            page = page_t::PAGE_1;
        }
        else if ((page2Status == pageStatus_t::ACTIVE) || (page2Status == pageStatus_t::FULL))
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

    case pageOp_t::WRITE:
    {
        if ((page1Status == pageStatus_t::ACTIVE) || (page1Status == pageStatus_t::RECEIVE))
        {
            page = page_t::PAGE_1;
        }
        else if ((page2Status == pageStatus_t::ACTIVE) || (page2Status == pageStatus_t::RECEIVE))
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
        return false;
    }

    return true;
}

EmuEEPROM::writeStatus_t EmuEEPROM::writeInternal(uint32_t index, const char* data, uint16_t length)
{
    if (index == INVALID_INDEX)
    {
        return writeStatus_t::WRITE_ERROR;
    }

    page_t writePage;

    if (!findValidPage(pageOp_t::WRITE, writePage))
    {
        return writeStatus_t::NO_PAGE;
    }

    auto write = [&](uint32_t offset)
    {
        // write the data in following order:
        // content
        // crc
        // size (without added padding)
        // index
        // end marker

        uint16_t crc = 0x0000;

        if (offset >= PAGE_END_OFFSET)
        {
            return writeStatus_t::PAGE_FULL;
        }

        if ((PAGE_END_OFFSET - offset) < entrySize(length))
        {
            return writeStatus_t::PAGE_FULL;
        }

        if (!_storageAccess.startWrite(writePage, offset))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        // content
        for (uint16_t i = 0; i < length; i++)
        {
            if (!write8(writePage, offset++, data[i]))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            crc = xmodemCRCUpdate(crc, data[i]);
        }

        // padding
        for (uint16_t i = 0; i < paddingBytes(length); i++)
        {
            if (!write8(writePage, offset++, 0xFF))
            {
                return writeStatus_t::WRITE_ERROR;
            }
        }
        //

        // crc
        if (!write16(writePage, offset, crc))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        offset += 2;
        //

        // size
        if (!write16(writePage, offset, length))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        offset += 2;
        //

        // index
        if (!write32(writePage, offset, index))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        offset += 4;
        //

        // end marker
        if (!write32(writePage, offset, CONTENT_END_MARKER))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        //

        if (!_storageAccess.endWrite(writePage))
        {
            return writeStatus_t::WRITE_ERROR;
        }

        return writeStatus_t::OK;
    };

    auto writeOffset = nextOffsetToWrite(writePage);

    if (writeOffset == std::nullopt)
    {
        return writeStatus_t::DATA_ERROR;
    }

    return write(writeOffset.value());
}

std::optional<uint32_t> EmuEEPROM::nextOffsetToWrite(page_t page)
{
    uint32_t writeOffset = EMU_EEPROM_PAGE_SIZE - sizeof(CONTENT_END_MARKER);

    auto previous = [&]()
    {
        writeOffset -= sizeof(CONTENT_END_MARKER);
    };

    auto next = [&]()
    {
        writeOffset += sizeof(CONTENT_END_MARKER);
    };

    // check each active page address starting from end
    while (writeOffset >= PAGE_STATUS_BYTES)
    {
        auto retrieved = read32(page, writeOffset);

        if (retrieved == std::nullopt)
        {
            return std::nullopt;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            next();
            break;
        }

        previous();
    }

    // no content end marker set - nothing is written yet
    writeOffset = writeOffsetAligned(writeOffset);

    return writeOffset;
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

    // old page already set to FULL

    if (!writePageStatus(newPage, pageStatus_t::RECEIVE))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    auto writeOffset = nextOffsetToWrite(newPage);

    if (writeOffset == std::nullopt)
    {
        return writeStatus_t::NO_PAGE;
    }

    if (!_storageAccess.startWrite(newPage, writeOffset.value()))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    if (!write32(newPage, writeOffset.value(), CONTENT_END_MARKER))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    if (!_storageAccess.endWrite(newPage))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    writeOffset.value() = writeOffsetAligned(writeOffset.value());

    // move all data from one page to another
    // start from last address
    for (uint32_t i = EMU_EEPROM_PAGE_SIZE - 4; i > PAGE_STATUS_BYTES; i -= 4)
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

            if (!_storageAccess.startWrite(newPage, writeOffset.value()))
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

                if (!write8(newPage, writeOffset.value()++, *content))
                {
                    return writeStatus_t::WRITE_ERROR;
                }
            }

            // skip this block of content but make sure that next loop
            // iteration reads the content end marker
            i -= (entrySize(length) - sizeof(CONTENT_END_MARKER));

            // crc
            if (!write16(newPage, writeOffset.value(), crc))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            writeOffset.value() += 2;

            // size
            if (!write16(newPage, writeOffset.value(), length))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            writeOffset.value() += 2;

            // index
            if (!write32(newPage, writeOffset.value(), index))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            writeOffset.value() += 4;

            // content end marker
            if (!write32(newPage, writeOffset.value(), CONTENT_END_MARKER))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            if (!_storageAccess.endWrite(newPage))
            {
                return writeStatus_t::WRITE_ERROR;
            }

            writeOffset.value() = writeOffsetAligned(writeOffset.value());

            markAsTransfered(index);
        }
    }

    // set new page status to ACTIVE status
    if (!writePageStatus(newPage, pageStatus_t::ACTIVE))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    // format old page
    if (!erasePage(oldPage))
    {
        return writeStatus_t::WRITE_ERROR;
    }

    resetTransferedIndexes();

    return writeStatus_t::OK;
}

bool EmuEEPROM::isIndexTransfered(uint32_t index)
{
    for (size_t i = 0; i < MAX_STRINGS; i++)
    {
        if (_indexTransferedArray.at(i) == index)
        {
            return true;
        }
    }

    return false;
}

void EmuEEPROM::markAsTransfered(uint32_t index)
{
    for (size_t i = 0; i < MAX_STRINGS; i++)
    {
        if (_indexTransferedArray.at(i) == INVALID_INDEX)
        {
            _indexTransferedArray.at(i) = index;
            break;
        }
    }
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

bool EmuEEPROM::write64(page_t page, uint32_t offset, uint64_t data)
{
    for (size_t i = 0; i < sizeof(uint64_t); i++)
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

std::optional<uint64_t> EmuEEPROM::read64(page_t page, uint32_t offset)
{
    uint8_t  temp[8] = {};
    uint64_t data    = 0;

    for (size_t i = 0; i < 8; i++)
    {
        auto retrieved = read8(page, offset + i);

        if (retrieved == std::nullopt)
        {
            return std::nullopt;
        }

        temp[i] = *retrieved;
    }

    data = temp[7];
    data <<= 8;
    data |= temp[6];
    data <<= 8;
    data |= temp[5];
    data <<= 8;
    data |= temp[4];
    data <<= 8;
    data |= temp[3];
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
    if (index == INVALID_INDEX)
    {
        return false;
    }

    page_t validPage;

    if (!findValidPage(pageOp_t::READ, validPage))
    {
        return false;
    }

    uint32_t readOffset = EMU_EEPROM_PAGE_SIZE - sizeof(CONTENT_END_MARKER);

    auto previous = [&readOffset]()
    {
        readOffset = readOffset - sizeof(CONTENT_END_MARKER);
    };

    // check each active page offset starting from end
    while (readOffset >= PAGE_STATUS_BYTES)
    {
        auto retrieved = read32(validPage, readOffset);

        if (retrieved == std::nullopt)
        {
            return false;
        }

        if (*retrieved == CONTENT_END_MARKER)
        {
            previous();
            retrieved = read32(validPage, readOffset);

            if (retrieved == std::nullopt)
            {
                return false;
            }

            if (*retrieved == index)
            {
                return true;
            }

            previous();
        }
        else
        {
            previous();
        }
    }

    return false;
}

EmuEEPROM::pageStatus_t EmuEEPROM::pageStatus(page_t page)
{
    auto status1 = read64(page, 0 + (sizeof(pageStatus_t) * 0));
    auto status2 = read64(page, 0 + (sizeof(pageStatus_t) * 1));
    auto status3 = read64(page, 0 + (sizeof(pageStatus_t) * 2));
    auto status4 = read64(page, 0 + (sizeof(pageStatus_t) * 3));

    if ((status1 == std::nullopt) ||
        (status2 == std::nullopt) ||
        (status3 == std::nullopt) ||
        (status4 == std::nullopt))
    {
        return pageStatus_t::ERASED;
    }

    // verify that each status entry contains valid values
    if (
        ((status1.value() != PAGE_MARKER_ERASED) && (status1.value() != PAGE_MARKER_PROGRAMED)) ||
        ((status2.value() != PAGE_MARKER_ERASED) && (status2.value() != PAGE_MARKER_PROGRAMED)) ||
        ((status3.value() != PAGE_MARKER_ERASED) && (status3.value() != PAGE_MARKER_PROGRAMED)) ||
        ((status4.value() != PAGE_MARKER_ERASED) && (status4.value() != PAGE_MARKER_PROGRAMED)))
    {
        return pageStatus_t::ERASED;
    }

    if (status4.value() == PAGE_MARKER_PROGRAMED)
    {
        return pageStatus_t::ACTIVE;
    }

    if (status3.value() == PAGE_MARKER_PROGRAMED)
    {
        return pageStatus_t::FULL;
    }

    if (status2.value() == PAGE_MARKER_PROGRAMED)
    {
        return pageStatus_t::RECEIVE;
    }

    if (status1.value() == PAGE_MARKER_PROGRAMED)
    {
        return pageStatus_t::FORMATTED;
    }

    return pageStatus_t::ERASED;
}

bool EmuEEPROM::writePageStatus(page_t page, pageStatus_t status)
{
    switch (status)
    {
    case pageStatus_t::ERASED:
    {
        // in this case there is nothing to write - page should be formatted instead
        // verify

        auto status1 = read64(page_t::PAGE_1, 0 + (sizeof(pageStatus_t) * 0));
        auto status2 = read64(page_t::PAGE_1, 0 + (sizeof(pageStatus_t) * 1));
        auto status3 = read64(page_t::PAGE_1, 0 + (sizeof(pageStatus_t) * 2));
        auto status4 = read64(page_t::PAGE_1, 0 + (sizeof(pageStatus_t) * 3));

        if ((status1 == std::nullopt) ||
            (status2 == std::nullopt) ||
            (status3 == std::nullopt) ||
            (status4 == std::nullopt))
        {
            return false;
        }

        if (status1 != PAGE_MARKER_ERASED)
        {
            return false;
        }

        if (status2 != PAGE_MARKER_ERASED)
        {
            return false;
        }

        if (status3 != PAGE_MARKER_ERASED)
        {
            return false;
        }

        if (status4 != PAGE_MARKER_ERASED)
        {
            return false;
        }

        return true;
    }
    break;

    case pageStatus_t::FORMATTED:
    {
        static constexpr uint32_t OFFSET = 0;

        if (_storageAccess.startWrite(page, OFFSET))
        {
            if (write64(page, OFFSET, PAGE_MARKER_PROGRAMED))
            {
                return _storageAccess.endWrite(page);
            }
        }
    }
    break;

    case pageStatus_t::RECEIVE:
    {
        static constexpr uint32_t OFFSET = 8;

        if (_storageAccess.startWrite(page, OFFSET))
        {
            if (write64(page, OFFSET, PAGE_MARKER_PROGRAMED))
            {
                return _storageAccess.endWrite(page);
            }
        }
    }
    break;

    case pageStatus_t::FULL:
    {
        static constexpr uint32_t OFFSET = 16;

        if (_storageAccess.startWrite(page, OFFSET))
        {
            if (write64(page, OFFSET, PAGE_MARKER_PROGRAMED))
            {
                return _storageAccess.endWrite(page);
            }
        }
    }
    break;

    case pageStatus_t::ACTIVE:
    {
        static constexpr uint32_t OFFSET = 24;

        if (_storageAccess.startWrite(page, OFFSET))
        {
            if (write64(page, OFFSET, PAGE_MARKER_PROGRAMED))
            {
                return _storageAccess.endWrite(page);
            }
        }
    }
    break;

    default:
        break;
    }

    return false;
}

bool EmuEEPROM::erasePage(page_t page)
{
    if (!_storageAccess.erasePage(page))
    {
        return false;
    }

    // mark as ready for future use
    if (!writePageStatus(page, pageStatus_t::FORMATTED))
    {
        return false;
    }

    return true;
}

void EmuEEPROM::resetTransferedIndexes()
{
    std::fill(_indexTransferedArray.begin(), _indexTransferedArray.end(), INVALID_INDEX);
}