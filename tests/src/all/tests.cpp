#include "unity/src/unity.h"
#include "unity/Helpers.h"
#include "EmuEEPROM.h"
#include <string.h>

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
                memset(pageArray, 0xFF, PAGE_SIZE);
            else
                memset(&pageArray[PAGE_SIZE], 0xFF, PAGE_SIZE);

            return true;
        }

        bool write16(uint32_t address, uint16_t data) override
        {
            pageArray[address + 0] = (data >> 0) & (uint16_t)0xFF;
            pageArray[address + 1] = (data >> 8) & (uint16_t)0xFF;

            return true;
        }

        bool write32(uint32_t address, uint32_t data) override
        {
            pageArray[address + 0] = (data >> 0) & (uint32_t)0xFF;
            pageArray[address + 1] = (data >> 8) & (uint32_t)0xFF;
            pageArray[address + 2] = (data >> 16) & (uint32_t)0xFF;
            pageArray[address + 3] = (data >> 24) & (uint32_t)0xFF;

            return true;
        }

        bool read16(uint32_t address, uint16_t& data) override
        {
            data = pageArray[address + 1];
            data <<= 8;
            data |= pageArray[address + 0];

            return true;
        }

        bool read32(uint32_t address, uint32_t& data) override
        {
            data = pageArray[address + 3];
            data <<= 8;
            data |= pageArray[address + 2];
            data <<= 8;
            data |= pageArray[address + 1];
            data <<= 8;
            data |= pageArray[address + 0];

            return true;
        }

        size_t pageSize() override
        {
            return PAGE_SIZE;
        }

        private:
        uint8_t pageArray[PAGE_SIZE * 2];
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