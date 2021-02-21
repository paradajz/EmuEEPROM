#include "unity/src/unity.h"
#include "unity/Helpers.h"
#include "EmuEEPROM.h"
#include <string.h>
#include <array>

#define PAGE_SIZE (128)

namespace
{
    class StorageMock : public EmuEEPROM::StorageAccess
    {
        public:
        StorageMock() {}

        bool init() override
        {
            return true;
        }

        uint32_t startAddress(EmuEEPROM::page_t page) override
        {
            if (page == EmuEEPROM::page_t::page1)
                return 0;
            else
                return PAGE_SIZE;
        }

        bool erasePage(EmuEEPROM::page_t page) override
        {
            if (page == EmuEEPROM::page_t::page1)
                std::fill(pageArray.begin(), pageArray.end() - PAGE_SIZE, 0xFF);
            else
                std::fill(pageArray.begin() + PAGE_SIZE, pageArray.end(), 0xFF);

            return true;
        }

        bool write16(uint32_t address, uint16_t data) override
        {
            pageArray.at(address + 0) = (data >> 0) & (uint16_t)0xFF;
            pageArray.at(address + 1) = (data >> 8) & (uint16_t)0xFF;

            return true;
        }

        bool write32(uint32_t address, uint32_t data) override
        {
            pageArray.at(address + 0) = (data >> 0) & (uint32_t)0xFF;
            pageArray.at(address + 1) = (data >> 8) & (uint32_t)0xFF;
            pageArray.at(address + 2) = (data >> 16) & (uint32_t)0xFF;
            pageArray.at(address + 3) = (data >> 24) & (uint32_t)0xFF;

            return true;
        }

        bool read16(uint32_t address, uint16_t& data) override
        {
            data = pageArray.at(address + 1);
            data <<= 8;
            data |= pageArray.at(address + 0);

            return true;
        }

        bool read32(uint32_t address, uint32_t& data) override
        {
            data = pageArray.at(address + 3);
            data <<= 8;
            data |= pageArray.at(address + 2);
            data <<= 8;
            data |= pageArray.at(address + 1);
            data <<= 8;
            data |= pageArray.at(address + 0);

            return true;
        }

        uint32_t pageSize() override
        {
            return PAGE_SIZE;
        }

        void reset()
        {
            std::fill(pageArray.begin(), pageArray.end(), 0xFF);
        }

        std::array<uint8_t, PAGE_SIZE * 2> pageArray;
    };

    StorageMock storageMock;
    EmuEEPROM   emuEEPROM(storageMock, false);
}    // namespace

TEST_SETUP()
{
    storageMock.erasePage(EmuEEPROM::page_t::page1);
    storageMock.erasePage(EmuEEPROM::page_t::page2);

    TEST_ASSERT(emuEEPROM.init() == true);
}

TEST_CASE(Insert)
{
    uint16_t value;

    TEST_ASSERT(emuEEPROM.write(0, 0x1234) == EmuEEPROM::writeStatus_t::ok);
    TEST_ASSERT(emuEEPROM.write(0, 0x1235) == EmuEEPROM::writeStatus_t::ok);
    TEST_ASSERT(emuEEPROM.write(0, 0x1236) == EmuEEPROM::writeStatus_t::ok);
    TEST_ASSERT(emuEEPROM.write(0, 0x1237) == EmuEEPROM::writeStatus_t::ok);

    TEST_ASSERT(emuEEPROM.read(0, value) == EmuEEPROM::readStatus_t::ok);

    //last value should be read
    TEST_ASSERT(value == 0x1237);
}

TEST_CASE(PageTransfer)
{
    uint16_t value;
    uint16_t writeValue;

    //initially, first page is active, while second one is erased
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page1) == EmuEEPROM::pageStatus_t::valid);
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page2) == EmuEEPROM::pageStatus_t::erased);

    //write variable to the same address n times in order to fill the entire page
    //page transfer should occur after which new page will only have single variable (latest one)
    for (int i = 0; i < PAGE_SIZE / 4; i++)
    {
        writeValue = 0x1234 + i;
        TEST_ASSERT(emuEEPROM.write(0, writeValue) == EmuEEPROM::writeStatus_t::ok);
    }

    TEST_ASSERT(emuEEPROM.read(0, value) == EmuEEPROM::readStatus_t::ok);
    TEST_ASSERT(value == writeValue);

    //verify that the second page is active and first one erased
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page2) == EmuEEPROM::pageStatus_t::valid);
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page1) == EmuEEPROM::pageStatus_t::erased);
}

TEST_CASE(PageTransfer2)
{
    //initially, first page is active, while second one is erased
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page1) == EmuEEPROM::pageStatus_t::valid);
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page2) == EmuEEPROM::pageStatus_t::erased);

    //fill half of the page
    for (int i = 0; i < PAGE_SIZE / 4 / 2 - 1; i++)
    {
        TEST_ASSERT(emuEEPROM.write(i, 0) == EmuEEPROM::writeStatus_t::ok);
    }

    //verify values
    for (int i = 0; i < PAGE_SIZE / 4 / 2 - 1; i++)
    {
        uint16_t value;

        TEST_ASSERT(emuEEPROM.read(i, value) == EmuEEPROM::readStatus_t::ok);
        TEST_ASSERT(value == 0);
    }

    //now fill full page with same addresses but with different values
    for (int i = 0; i < PAGE_SIZE / 4 - 1; i++)
    {
        TEST_ASSERT(emuEEPROM.write(i, 1) == EmuEEPROM::writeStatus_t::ok);
    }

    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page2) == EmuEEPROM::pageStatus_t::valid);
    TEST_ASSERT(emuEEPROM.pageStatus(EmuEEPROM::page_t::page1) == EmuEEPROM::pageStatus_t::erased);

    //also verify that the memory contains only updated values
    for (int i = 0; i < PAGE_SIZE / 4 - 1; i++)
    {
        uint16_t value;

        TEST_ASSERT(emuEEPROM.read(i, value) == EmuEEPROM::readStatus_t::ok);
        TEST_ASSERT(value == 1);
    }
}

TEST_CASE(OverFlow)
{
    uint32_t readData = 0;

    //manually prepare flash pages
    storageMock.reset();

    //set page 1 to valid state and page 2 to erased
    storageMock.write32(0, static_cast<uint32_t>(EmuEEPROM::pageStatus_t::valid));
    storageMock.write32(PAGE_SIZE, static_cast<uint32_t>(EmuEEPROM::pageStatus_t::erased));

    //now, write data with address being larger than the max page size

    //value 0, address PAGE_SIZE + 1
    //emulated storage writes value first (2 bytes) and then address (2 bytes)
    //use raw address 4 - first four bytes are for page status
    storageMock.write32(4, static_cast<uint32_t>(PAGE_SIZE + 1) << 16 | 0x0000);
    storageMock.read32(4, readData);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PAGE_SIZE + 1) << 16 | 0x0000, readData);

    emuEEPROM.init();

    //expect page1 to be formatted due to invalid data
    storageMock.read32(4, readData);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, readData);
}