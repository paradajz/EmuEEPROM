#include "unity/src/unity.h"
#include "unity/Helpers.h"
#include "EmuEEPROM.h"
#include <string.h>
#include <array>
#include <string>
#include <vector>
#include <iostream>

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
                return EMU_EEPROM_PAGE_SIZE;
        }

        bool erasePage(EmuEEPROM::page_t page) override
        {
            if (page == EmuEEPROM::page_t::page1)
                std::fill(pageArray.begin(), pageArray.end() - EMU_EEPROM_PAGE_SIZE, 0xFF);
            else
                std::fill(pageArray.begin() + EMU_EEPROM_PAGE_SIZE, pageArray.end(), 0xFF);

            pageEraseCounter++;

            return true;
        }

        bool write(uint32_t address, uint8_t data) override
        {
            // 0->1 transition is not allowed
            uint8_t currentData = read(address);

            if (data > currentData)
                return false;

            pageArray.at(address) = data;

            return true;
        }

        uint8_t read(uint32_t address) override
        {
            return pageArray.at(address);
        }

        void reset()
        {
            std::fill(pageArray.begin(), pageArray.end(), 0xFF);
        }

        std::array<uint8_t, EMU_EEPROM_PAGE_SIZE * 2> pageArray;
        size_t                                        pageEraseCounter = 0;
    };

    StorageMock storageMock;
    EmuEEPROM   emuEEPROM(storageMock, false);
}    // namespace

TEST_SETUP()
{
    storageMock.erasePage(EmuEEPROM::page_t::page1);
    storageMock.erasePage(EmuEEPROM::page_t::page2);
    TEST_ASSERT(emuEEPROM.init() == true);
    storageMock.pageEraseCounter = 0;
}

TEST_CASE(FlashFormat)
{
    // fill flash with junk, run init and verify that all content is cleared
    for (int i = 0; i < storageMock.pageArray.size(); i++)
        storageMock.pageArray.at(i) = i;

    emuEEPROM.init();

    // expect the following:
    // first 4 bytes: status
    // rest: 0xFF

    // status for first page should be 0x00
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_UINT32(0x00, storageMock.pageArray.at(i));

    for (int i = 4; i < EMU_EEPROM_PAGE_SIZE; i++)
        TEST_ASSERT_EQUAL_UINT32(0xFF, storageMock.pageArray.at(i));

    // status for second page should be 0xFFFFEEEE
    for (int i = EMU_EEPROM_PAGE_SIZE; i < EMU_EEPROM_PAGE_SIZE + 2; i++)
        TEST_ASSERT_EQUAL_UINT32(0xEE, storageMock.pageArray.at(i));

    for (int i = EMU_EEPROM_PAGE_SIZE + 2; i < EMU_EEPROM_PAGE_SIZE + 4; i++)
        TEST_ASSERT_EQUAL_UINT32(0xFF, storageMock.pageArray.at(i));

    for (int i = EMU_EEPROM_PAGE_SIZE + 4; i < EMU_EEPROM_PAGE_SIZE * 2; i++)
        TEST_ASSERT_EQUAL_UINT32(0xFF, storageMock.pageArray.at(i));
}

TEST_CASE(Insert)
{
    struct entry_t
    {
        uint32_t    index = 0;
        std::string text  = "";
    };

    std::vector<entry_t> entry = {
        {
            0xABCD,
            "Hello!",
        },
        {
            0x1234,
            "Hi!",
        },
        {
            0x54BA,
            "Bonjour!",
        }
    };

    char     readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t readLength                       = 0;

    for (size_t i = 0; i < entry.size(); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::ok, emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::ok, emuEEPROM.read(entry.at(i).index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

        std::string retrievedString = readBuffer;
        TEST_ASSERT(entry.at(i).text == retrievedString);
        memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
    }

    // change the strings, but leave the same indexes
    // write new data and verify that new strings are read

    entry.at(0).text = "Greetings!";
    entry.at(1).text = "This greeting is brought to you by GreetCo LLC";
    entry.at(2).text = "Ola!";

    for (size_t i = 0; i < entry.size(); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::ok, emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::ok, emuEEPROM.read(entry.at(i).index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

        std::string retrievedString = readBuffer;
        TEST_ASSERT(entry.at(i).text == retrievedString);
        memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
    }

    // make sure data which isn't written throws noIndex error
    const uint32_t index = 0xBEEF;
    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::noIndex, emuEEPROM.read(index, readBuffer, readLength, readLength));
}

TEST_CASE(ContentTooLarge)
{
    const uint32_t index                            = 0x42FC;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text;

    for (size_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i++)
        text += "A";

    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::pageFull, emuEEPROM.write(index, text.c_str()));
    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::noIndex, emuEEPROM.read(index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_CASE(InvalidPages)
{
    // make sure both pages are in invalid state
    storageMock.pageArray.at(0)                    = 0xAA;
    storageMock.pageArray.at(EMU_EEPROM_PAGE_SIZE) = 0xAA;

    const uint32_t index                            = 0x01;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "this is a string";

    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::noPage, emuEEPROM.write(index, text.c_str()));
    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::noPage, emuEEPROM.read(index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_CASE(InvalidIndex)
{
    const uint32_t index                            = 0xFFFFFFFF;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "this is a string";

    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::writeError, emuEEPROM.write(index, text.c_str()));
    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::noIndex, emuEEPROM.read(index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_CASE(DataPersistentAfterInit)
{
    const uint32_t index                            = 0xABF4;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "DataPersistentAfterInit";

    // insert data, verify its contents, reinit the module and verify it is still present

    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::ok, emuEEPROM.write(index, text.c_str()));
    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::ok, emuEEPROM.read(index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    std::string retrievedString = readBuffer;
    TEST_ASSERT(text == retrievedString);
    memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);

    emuEEPROM.init();

    TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::readStatus_t::ok, emuEEPROM.read(index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    retrievedString = readBuffer;
    TEST_ASSERT(text == retrievedString);
    memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
}

TEST_CASE(IndexExistsAPI)
{
    // write few indexes and verify that indexExists API returns correct response
    struct entry_t
    {
        uint32_t    index = 0;
        std::string text  = "";
    };

    std::vector<entry_t> entry = {
        {
            0x1234,
            "string 1",
        },
        {
            0x5678,
            "string 2",
        },
        {
            0x9ABC,
            "string 3",
        }
    };

    for (size_t i = 0; i < entry.size(); i++)
    {
        TEST_ASSERT(emuEEPROM.indexExists(entry.at(i).index) == false);
        TEST_ASSERT_EQUAL_UINT32(EmuEEPROM::writeStatus_t::ok, emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        TEST_ASSERT(emuEEPROM.indexExists(entry.at(i).index) == true);
    }
}