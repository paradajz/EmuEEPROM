#include "tests/common.h"
#include "lib/emueeprom/emueeprom.h"

#include <array>

using namespace lib::emueeprom;

namespace
{
    class EmuEEPROMTest : public ::testing::Test
    {
        protected:
        void SetUp()
        {
            _hwa.erasePage(page_t::PAGE_1);
            _hwa.erasePage(page_t::PAGE_2);
            ASSERT_TRUE(_emuEEPROM.init());
            _hwa._pageEraseCounter = 0;
        }

        void TearDown()
        {}

        class HwaTest : public Hwa
        {
            public:
            HwaTest() = default;

            bool init() override
            {
                return true;
            }

            bool erasePage(page_t page) override
            {
                if (page == page_t::PAGE_FACTORY)
                {
                    return false;
                }

                std::fill(_pageArray.at(static_cast<uint8_t>(page)).begin(), _pageArray.at(static_cast<uint8_t>(page)).end(), 0xFF);
                _pageEraseCounter++;

                return true;
            }

            bool write32(page_t page, uint32_t offset, uint32_t data) override
            {
                if (page == page_t::PAGE_FACTORY)
                {
                    return false;
                }

                // 0->1 transition is not allowed
                uint32_t currentData = 0;
                read32(page, offset, currentData);

                if (data > currentData)
                {
                    return false;
                }

                _pageArray.at(static_cast<uint8_t>(page)).at(offset + 0) = (data >> 0) & static_cast<uint16_t>(0xFF);
                _pageArray.at(static_cast<uint8_t>(page)).at(offset + 1) = (data >> 8) & static_cast<uint16_t>(0xFF);
                _pageArray.at(static_cast<uint8_t>(page)).at(offset + 2) = (data >> 16) & static_cast<uint16_t>(0xFF);
                _pageArray.at(static_cast<uint8_t>(page)).at(offset + 3) = (data >> 24) & static_cast<uint16_t>(0xFF);

                return true;
            }

            bool read32(page_t page, uint32_t offset, uint32_t& data) override
            {
                data = _pageArray.at(static_cast<uint8_t>(page)).at(offset + 3);
                data <<= 8;
                data |= _pageArray.at(static_cast<uint8_t>(page)).at(offset + 2);
                data <<= 8;
                data |= _pageArray.at(static_cast<uint8_t>(page)).at(offset + 1);
                data <<= 8;
                data |= _pageArray.at(static_cast<uint8_t>(page)).at(offset + 0);

                return true;
            }

            std::array<std::array<uint8_t, EMU_EEPROM_PAGE_SIZE>, 2> _pageArray;
            size_t                                                   _pageEraseCounter = 0;
        } _hwa;

        EmuEEPROM _emuEEPROM = EmuEEPROM(_hwa, false);
    };
}    // namespace

TEST_F(EmuEEPROMTest, ReadNonExisting)
{
    uint16_t value;
    ASSERT_EQ(readStatus_t::NO_VAR, _emuEEPROM.read(0, value));
}

TEST_F(EmuEEPROMTest, Insert)
{
    uint16_t value;

    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1234));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1235));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1236));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1237));

    ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(0, value));

    // last value should be read
    ASSERT_EQ(0x1237, value);
}

TEST_F(EmuEEPROMTest, PageTransfer)
{
    uint16_t value;
    uint16_t writeValue;

    // initially, first page is active, while second one is formatted
    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_1));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_2));

    // write variable to the same address n times in order to fill the entire page
    // page transfer should occur after which new page will only have single variable (latest one)
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4; i++)
    {
        writeValue = 0x1234 + i;
        ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, writeValue));
    }

    ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(0, value));
    ASSERT_EQ(writeValue, value);

    // verify that the second page is active and first one formatted
    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_2));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_1));

    // the states should be preserved after init
    _emuEEPROM.init();
    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_2));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_1));
}

TEST_F(EmuEEPROMTest, PageTransfer2)
{
    // initially, first page is active, while second one is formatted
    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_1));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_2));

    // fill half of the page
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4 / 2 - 1; i++)
    {
        ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(i, 0));
    }

    // verify values
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4 / 2 - 1; i++)
    {
        uint16_t value;

        ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(i, value));
        ASSERT_EQ(0, value);
    }

    // now fill full page with same addresses but with different values
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4 - 1; i++)
    {
        ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(i, 1));
    }

    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_2));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_1));

    // also verify that the memory contains only updated values
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4 - 1; i++)
    {
        uint16_t value;

        ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(i, value));
        ASSERT_EQ(1, value);
    }

    // repeat the test after init
    _emuEEPROM.init();

    ASSERT_EQ(pageStatus_t::VALID, _emuEEPROM.pageStatus(page_t::PAGE_2));
    ASSERT_EQ(pageStatus_t::FORMATTED, _emuEEPROM.pageStatus(page_t::PAGE_1));

    // also verify that the memory contains only updated values
    for (int i = 0; i < EMU_EEPROM_PAGE_SIZE / 4 - 1; i++)
    {
        uint16_t value;

        ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(i, value));
        ASSERT_EQ(1, value);
    }
}

TEST_F(EmuEEPROMTest, OverFlow)
{
    uint32_t readData   = 0;
    uint16_t readData16 = 0;

    // manually prepare flash pages
    _hwa.erasePage(page_t::PAGE_1);
    _hwa.erasePage(page_t::PAGE_2);

    // set page 1 to valid state and page 2 to formatted
    _hwa.write32(page_t::PAGE_1, 0, static_cast<uint32_t>(pageStatus_t::VALID));
    _hwa.write32(page_t::PAGE_2, 0, static_cast<uint32_t>(pageStatus_t::FORMATTED));

    // now, write data with address being larger than the max page size

    // value 0, address EMU_EEPROM_PAGE_SIZE + 1
    // emulated storage writes value first (2 bytes) and then address (2 bytes)
    // use raw address 4 - first four bytes are for page status
    _hwa.write32(page_t::PAGE_1, 4, static_cast<uint32_t>(EMU_EEPROM_PAGE_SIZE + 1) << 16 | 0x0000);
    _hwa.read32(page_t::PAGE_1, 4, readData);
    ASSERT_EQ(static_cast<uint32_t>(EMU_EEPROM_PAGE_SIZE + 1) << 16 | 0x0000, readData);

    _emuEEPROM.init();

    // expect page1 to be formatted due to invalid data
    _hwa.read32(page_t::PAGE_1, 4, readData);
    ASSERT_EQ(0xFFFFFFFF, readData);

    // attempt to write and read an address larger than max allowed (page size / 4 minus one address)
    ASSERT_EQ(writeStatus_t::WRITE_ERROR, _emuEEPROM.write((EMU_EEPROM_PAGE_SIZE / 4) - 1, 0));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write((EMU_EEPROM_PAGE_SIZE / 4) - 2, 0));

    ASSERT_EQ(readStatus_t::READ_ERROR, _emuEEPROM.read((EMU_EEPROM_PAGE_SIZE / 4) - 1, readData16));
    ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read((EMU_EEPROM_PAGE_SIZE / 4) - 2, readData16));
}

TEST_F(EmuEEPROMTest, PageErase)
{
    // at this point, emueeprom is prepared
    ASSERT_EQ(0, _hwa._pageEraseCounter);

    // run init again and verify that no pages have been erased again
    _emuEEPROM.init();
    ASSERT_EQ(0, _hwa._pageEraseCounter);
}

TEST_F(EmuEEPROMTest, CachedWrite)
{
    uint16_t value;

    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1234, true));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1235, true));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1236, true));
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1237, true));

    ASSERT_EQ(readStatus_t::OK, _emuEEPROM.read(0, value));

    // last value should be read
    ASSERT_EQ(0x1237, value);

    // now init the library again - read should return NO_VAR since the value was written just in cache
    _emuEEPROM.init();

    ASSERT_EQ(readStatus_t::NO_VAR, _emuEEPROM.read(0, value));

    // write in cache again, but this time,transfer everything to NVM memory
    ASSERT_EQ(writeStatus_t::OK, _emuEEPROM.write(0, 0x1237, true));
    ASSERT_EQ(0x1237, value);

    _emuEEPROM.writeCacheToFlash();

    _emuEEPROM.init();

    // after another initialization, read value should be the one that was written
    ASSERT_EQ(0x1237, value);
}