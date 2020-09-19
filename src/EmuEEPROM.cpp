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
    if (!storageAccess.init())
        return false;

    auto page1Status = pageStatus(page_t::page1);
    auto page2Status = pageStatus(page_t::page2);

    /* Check for invalid header states and repair if necessary */
    switch (page1Status)
    {
    case pageStatus_t::erased:
        if (page2Status == pageStatus_t::valid)
        {
            //page 1 erased, page 1 valid
            //erase page 1
            storageAccess.erasePage(page_t::page1);
        }
        else if (page2Status == pageStatus_t::receiving)
        {
            //page 1 erased, page 2 in receive state
            //erase page1
            storageAccess.erasePage(page_t::page1);

            if (!storageAccess.write32(storageAccess.startAddress(page_t::page2), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            //erase both pages and set first page as valid
            if (!format())
                return false;
        }
        break;

    case pageStatus_t::receiving:
        if (page2Status == pageStatus_t::valid)
        {
            //page 1 in receive state, page 2 valid
            //restart the transfer process by first erasing page 1 and then performing page transfer
            storageAccess.erasePage(page_t::page1);

            if (pageTransfer() != writeStatus_t::ok)
                return false;
        }
        else if (page2Status == pageStatus_t::erased)
        {
            //page 1 in receive state, page 2 erased
            //erase page 2
            storageAccess.erasePage(page_t::page2);

            //mark page 1 as valid
            if (!storageAccess.write32(storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
                return false;
        }
        else
        {
            //invalid state
            if (!format())
                return false;
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
            //erase page 2
            storageAccess.erasePage(page_t::page2);
        }
        else
        {
            //page 1 valid, page 2 in receive state
            //restart the transfer process by first erasing page 2 and then performing page transfer
            storageAccess.erasePage(page_t::page2);

            if (pageTransfer() != writeStatus_t::ok)
                return false;
        }
        break;

    default:
        if (!format())
            return false;
        break;
    }

    return true;
}

bool EmuEEPROM::format()
{
    //erase both pages and set page 1 as valid

    if (!storageAccess.erasePage(page_t::page1))
        return false;

    //copy contents from factory page to page 1 if the page is in correct status
    if (_useFactoryPage && (pageStatus(page_t::pageFactory) == pageStatus_t::valid))
    {
        for (size_t i = 0; i < storageAccess.pageSize(); i += 4)
        {
            uint32_t data;

            if (!storageAccess.read32(storageAccess.startAddress(page_t::pageFactory) + i, data))
                return false;

            if (data == 0xFFFFFFFF)
                break;    //empty block, no need to go further

            if (!storageAccess.write32(storageAccess.startAddress(page_t::page1) + i, data))
                return false;
        }
    }
    else
    {
        //just set valid status to page1
        if (!storageAccess.write32(storageAccess.startAddress(page_t::page1), static_cast<uint32_t>(pageStatus_t::valid)))
            return false;
    }

    return storageAccess.erasePage(page_t::page2);
}

EmuEEPROM::readStatus_t EmuEEPROM::read(uint32_t address, uint16_t& data)
{
    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return readStatus_t::noPage;

    readStatus_t status = readStatus_t::noVar;

    //take into account 4-byte page header
    uint32_t startAddress     = storageAccess.startAddress(validPage);
    uint32_t pageSize         = storageAccess.pageSize();
    uint32_t pageStartAddress = startAddress + 4;
    uint32_t pageEndAddress   = startAddress + pageSize - 2;

    auto next = [&pageEndAddress]() {
        pageEndAddress = pageEndAddress - 4;
    };

    //check each active page address starting from end
    while (pageEndAddress > pageStartAddress)
    {
        /* Get the current location content to be compared with virtual address */
        uint16_t addressValue = 0;

        if (storageAccess.read16(pageEndAddress, addressValue))
        {
            /* Compare the read address with the virtual address */
            if (addressValue == address)
            {
                /* Get content of Address-2 which is variable value */
                if (!storageAccess.read16(pageEndAddress - 2, data))
                    return readStatus_t::readError;

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
    uint32_t startAddress     = storageAccess.startAddress(validPage);
    uint32_t pageSize         = storageAccess.pageSize();
    uint32_t pageStartAddress = startAddress + 4;
    uint32_t pageEndAddress   = startAddress + pageSize - 2;

    auto next = [&pageStartAddress]() {
        pageStartAddress += 4;
    };

    /* Check each active page address starting from begining */
    while (pageStartAddress < pageEndAddress)
    {
        uint32_t readData = 0;

        if (storageAccess.read32(pageStartAddress, readData))
        {
            if (readData == 0xFFFFFFFF)
            {
                if (!storageAccess.write16(pageStartAddress, data))
                    return writeStatus_t::writeError;

                /* Set variable virtual address */
                if (!storageAccess.write16(pageStartAddress + 2, address))
                    return writeStatus_t::writeError;

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

    return writeStatus_t::pageFull;
}

EmuEEPROM::writeStatus_t EmuEEPROM::pageTransfer()
{
    page_t validPage;

    if (!findValidPage(pageOp_t::read, validPage))
        return writeStatus_t::noPage;

    uint32_t newPageAddress = storageAccess.startAddress(page_t::page1);
    page_t   oldPage        = page_t::page1;

    if (validPage == page_t::page2)
    {
        /* New page address where variable will be moved to */
        newPageAddress = storageAccess.startAddress(page_t::page1);

        /* Old page ID where variable will be taken from */
        oldPage = page_t::page2;
    }
    else if (validPage == page_t::page1)
    {
        /* New page address  where variable will be moved to */
        newPageAddress = storageAccess.startAddress(page_t::page2);

        /* Old page ID where variable will be taken from */
        oldPage = page_t::page1;
    }

    if (!storageAccess.write32(newPageAddress, static_cast<uint32_t>(pageStatus_t::receiving)))
        return writeStatus_t::writeError;

    writeStatus_t eepromStatus;
    uint16_t      data;

    //move all variables from one page to another
    //total number of variables is calculated by dividing page size size with 4
    //2 bytes for address
    //2 bytes for single variable
    //remove 1 since first 4 bytes are header
    for (uint32_t i = 0; i < (storageAccess.pageSize() / 4) - 1; i++)
    {
        /* Read the other last variable updates */
        /* In case variable corresponding to the virtual address was found */
        if (read(i, data) == readStatus_t::ok)
        {
            /* Transfer the variable to the new active page */
            eepromStatus = writeInternal(i, data);

            if (eepromStatus != writeStatus_t::ok)
                return eepromStatus;
        }
    }

    /* Erase the old Page: Set old Page status to ERASED status */
    storageAccess.erasePage(oldPage);

    /* Set new Page status to VALID_PAGE status */
    if (!storageAccess.write32(newPageAddress, static_cast<uint32_t>(pageStatus_t::valid)))
        return writeStatus_t::writeError;

    return writeStatus_t::ok;
}

EmuEEPROM::pageStatus_t EmuEEPROM::pageStatus(page_t page)
{
    uint32_t     data;
    pageStatus_t status;

    switch (page)
    {
    case page_t::page1:
        storageAccess.read32(storageAccess.startAddress(page_t::page1), data);
        break;

    case page_t::page2:
        storageAccess.read32(storageAccess.startAddress(page_t::page2), data);
        break;

    case page_t::pageFactory:
        storageAccess.read32(storageAccess.startAddress(page_t::pageFactory), data);
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
    default:
        status = EmuEEPROM::pageStatus_t::valid;
        break;
    }

    return status;
}