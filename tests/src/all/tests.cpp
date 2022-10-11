#include "framework/Framework.h"
#include "EmuEEPROM.h"
#include <string.h>
#include <array>

namespace
{
    class EmuEEPROMTest : public ::testing::Test
    {
        protected:
        void SetUp()
        {
            _storageMock.erasePage(EmuEEPROM::page_t::PAGE_1);
            _storageMock.erasePage(EmuEEPROM::page_t::PAGE_2);
            ASSERT_TRUE(_emuEEPROM.init());
            _storageMock._pageEraseCounter = 0;
        }

        void TearDown()
        {}

        class StorageMock : public EmuEEPROM::StorageAccess
        {
            public:
            StorageMock() = default;

            bool init() override
            {
                return true;
            }

            uint32_t startAddress(EmuEEPROM::page_t page) override
            {
                if (page == EmuEEPROM::page_t::PAGE_1)
                {
                    return 0;
                }

                return EMU_EEPROM_PAGE_SIZE;
            }

            bool erasePage(EmuEEPROM::page_t page) override
            {
                if (page == EmuEEPROM::page_t::PAGE_1)
                {
                    std::fill(_pageArray.begin(), _pageArray.end() - EMU_EEPROM_PAGE_SIZE, 0xFF);
                }
                else
                {
                    std::fill(_pageArray.begin() + EMU_EEPROM_PAGE_SIZE, _pageArray.end(), 0xFF);
                }

                _pageEraseCounter++;

                return true;
            }

            bool write(uint32_t address, uint8_t data) override
            {
                // 0->1 transition is not allowed
                if (data > _pageArray.at(address))
                {
                    return false;
                }

                _pageArray.at(address) = data;

                return true;
            }

            bool read(uint32_t address, uint8_t& data) override
            {
                data = _pageArray.at(address);
                return true;
            }

            void reset()
            {
                std::fill(_pageArray.begin(), _pageArray.end(), 0xFF);
            }

            std::array<uint8_t, EMU_EEPROM_PAGE_SIZE * 2> _pageArray;
            size_t                                        _pageEraseCounter = 0;
        };

        StorageMock _storageMock;
        EmuEEPROM   _emuEEPROM = EmuEEPROM(_storageMock, false);
    };
}    // namespace

TEST_F(EmuEEPROMTest, FlashFormat)
{
    // fill flash with junk, run init and verify that all content is cleared
    for (int i = 0; i < _storageMock._pageArray.size(); i++)
    {
        _storageMock._pageArray.at(i) = i;
    }

    _emuEEPROM.init();

    // expect the following:
    // first 4 bytes: status
    // rest: 0xFF

    // status for first page should be 0x00
    for (int i = 0; i < 4; i++)
    {
        ASSERT_EQ(0x00, _storageMock._pageArray.at(i));
    }

    for (int i = 4; i < EMU_EEPROM_PAGE_SIZE; i++)
    {
        ASSERT_EQ(0xFF, _storageMock._pageArray.at(i));
    }

    // status for second page should be 0xFFFFEEEE
    for (int i = EMU_EEPROM_PAGE_SIZE; i < EMU_EEPROM_PAGE_SIZE + 2; i++)
    {
        ASSERT_EQ(0xEE, _storageMock._pageArray.at(i));
    }

    for (int i = EMU_EEPROM_PAGE_SIZE + 2; i < EMU_EEPROM_PAGE_SIZE + 4; i++)
    {
        ASSERT_EQ(0xFF, _storageMock._pageArray.at(i));
    }

    for (int i = EMU_EEPROM_PAGE_SIZE + 4; i < EMU_EEPROM_PAGE_SIZE * 2; i++)
    {
        ASSERT_EQ(0xFF, _storageMock._pageArray.at(i));
    }
}

TEST_F(EmuEEPROMTest, Insert)
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
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(entry.at(i).index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

        std::string retrievedString = readBuffer;
        ASSERT_TRUE(entry.at(i).text == retrievedString);
        memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
    }

    // change the strings, but leave the same indexes
    // write new data and verify that new strings are read

    entry.at(0).text = "Greetings!";
    entry.at(1).text = "This greeting is brought to you by GreetCo LLC";
    entry.at(2).text = "Ola!";

    for (size_t i = 0; i < entry.size(); i++)
    {
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(entry.at(i).index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

        std::string retrievedString = readBuffer;
        ASSERT_TRUE(entry.at(i).text == retrievedString);
        memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
    }

    // make sure data which isn't written throws noIndex error
    const uint32_t INDEX = 0xBEEF;
    ASSERT_EQ(EmuEEPROM::readStatus_t::NO_INDEX, _emuEEPROM.read(INDEX, readBuffer, readLength, readLength));
}

TEST_F(EmuEEPROMTest, ContentTooLarge)
{
    const uint32_t INDEX                            = 0x42FC;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text;

    for (size_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i++)
    {
        text += "A";
    }

    ASSERT_EQ(EmuEEPROM::writeStatus_t::PAGE_FULL, _emuEEPROM.write(INDEX, text.c_str()));
    ASSERT_EQ(EmuEEPROM::readStatus_t::NO_INDEX, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_F(EmuEEPROMTest, InvalidPages)
{
    // make sure both pages are in invalid state
    _storageMock._pageArray.at(0)                    = 0xAA;
    _storageMock._pageArray.at(EMU_EEPROM_PAGE_SIZE) = 0xAA;

    const uint32_t INDEX                            = 0x01;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "this is a string";

    ASSERT_EQ(EmuEEPROM::writeStatus_t::NO_PAGE, _emuEEPROM.write(INDEX, text.c_str()));
    ASSERT_EQ(EmuEEPROM::readStatus_t::NO_PAGE, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_F(EmuEEPROMTest, InvalidIndex)
{
    const uint32_t INDEX                            = 0xFFFFFFFF;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "this is a string";

    ASSERT_EQ(EmuEEPROM::writeStatus_t::WRITE_ERROR, _emuEEPROM.write(INDEX, text.c_str()));
    ASSERT_EQ(EmuEEPROM::readStatus_t::NO_INDEX, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
}

TEST_F(EmuEEPROMTest, InvalidString)
{
    const uint32_t INDEX = 0xABCD;

    ASSERT_EQ(EmuEEPROM::writeStatus_t::DATA_ERROR, _emuEEPROM.write(INDEX, nullptr));
}

TEST_F(EmuEEPROMTest, DataPersistentAfterInit)
{
    const uint32_t INDEX                            = 0xABF4;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "DataPersistentAfterInit";

    // insert data, verify its contents, reinit the module and verify it is still present

    ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(INDEX, text.c_str()));
    ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    std::string retrievedString = readBuffer;
    ASSERT_TRUE(text == retrievedString);
    memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);

    _emuEEPROM.init();

    ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    retrievedString = readBuffer;
    ASSERT_TRUE(text == retrievedString);
    memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
}

TEST_F(EmuEEPROMTest, IndexExistsAPI)
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
        ASSERT_TRUE(_emuEEPROM.indexExists(entry.at(i).index) == false);
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(entry.at(i).index, entry.at(i).text.c_str()));
        ASSERT_TRUE(_emuEEPROM.indexExists(entry.at(i).index) == true);
    }
}

TEST_F(EmuEEPROMTest, PageTransfer)
{
    const uint32_t INDEX                            = 0xEEEE;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "page transfer";

    auto sizeInEEPROM        = EmuEEPROM::entrySize(text.size());
    auto loopsBeforeTransfer = EMU_EEPROM_PAGE_SIZE / sizeInEEPROM;

    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_1) == EmuEEPROM::pageStatus_t::VALID);
    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_2) == EmuEEPROM::pageStatus_t::FORMATTED);

    for (size_t i = 0; i < loopsBeforeTransfer; i++)
    {
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(INDEX, text.c_str()));
    }

    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_1) == EmuEEPROM::pageStatus_t::FORMATTED);
    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_2) == EmuEEPROM::pageStatus_t::VALID);

    // content must still be valid after transfer
    ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    std::string retrievedString = readBuffer;
    ASSERT_TRUE(text == retrievedString);
}