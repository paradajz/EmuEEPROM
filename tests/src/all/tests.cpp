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
            // both pages should be formatted
            ASSERT_EQ(2, _storageMock._pageEraseCounter);
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
                _pageEraseCounter = 0;
                return true;
            }

            bool erasePage(EmuEEPROM::page_t page) override
            {
                if (page == EmuEEPROM::page_t::PAGE_FACTORY)
                {
                    return false;
                }

                std::fill(_pageArray.at(static_cast<uint8_t>(page)).begin(), _pageArray.at(static_cast<uint8_t>(page)).end(), 0xFF);
                _pageEraseCounter++;

                return true;
            }

            bool startWrite(EmuEEPROM::page_t page, uint32_t offset) override
            {
                auto alignment = offset % EMUEEPROM_WRITE_ALIGNMENT;

                EXPECT_EQ(0, alignment);

                return true;
            }

            bool write(EmuEEPROM::page_t page, uint32_t offset, uint8_t data) override
            {
                if (page == EmuEEPROM::page_t::PAGE_FACTORY)
                {
                    return false;
                }

                // value must be erased
                if (_pageArray.at(static_cast<uint8_t>(page)).at(offset) != 0xFF)
                {
                    return false;
                }

                _pageArray.at(static_cast<uint8_t>(page)).at(offset) = data;

                return true;
            }

            bool endWrite(EmuEEPROM::page_t page) override
            {
                return true;
            }

            bool read(EmuEEPROM::page_t page, uint32_t offset, uint8_t& data) override
            {
                data = _pageArray.at(static_cast<uint8_t>(page)).at(offset);
                return true;
            }

            void reset()
            {
                std::fill(_pageArray.at(0).begin(), _pageArray.at(0).end(), 0xFF);
                std::fill(_pageArray.at(1).begin(), _pageArray.at(1).end(), 0xFF);
            }

            void copyToFactory()
            {
                for (size_t i = 0; i < EMU_EEPROM_PAGE_SIZE; i++)
                {
                    _pageArray.at(static_cast<size_t>(EmuEEPROM::page_t::PAGE_FACTORY)).at(i) = _pageArray.at(static_cast<size_t>(EmuEEPROM::page_t::PAGE_1)).at(i);
                }
            }

            std::array<std::array<uint8_t, EMU_EEPROM_PAGE_SIZE>, 3> _pageArray;
            size_t                                                   _pageEraseCounter = 0;
        };

        StorageMock _storageMock;
        EmuEEPROM   _emuEEPROM = EmuEEPROM(_storageMock, false);
    };
}    // namespace

TEST_F(EmuEEPROMTest, ReadNonExisting)
{
    const uint32_t INDEX                            = 0x1234;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;

    ASSERT_EQ(EmuEEPROM::readStatus_t::NO_INDEX, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));
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

    // now copy everything to factory page
    _storageMock.copyToFactory();
    _storageMock.erasePage(EmuEEPROM::page_t::PAGE_1);
    _storageMock.erasePage(EmuEEPROM::page_t::PAGE_2);

    // create a new EmuEEPROM with factory page
    EmuEEPROM _emuEEPROM2 = EmuEEPROM(_storageMock, true);
    ASSERT_TRUE(_emuEEPROM2.init());

    // verify that the new instance has everything copied to page 1
    for (size_t i = 0; i < entry.size(); i++)
    {
        ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM2.read(entry.at(i).index, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

        std::string retrievedString = readBuffer;
        ASSERT_TRUE(entry.at(i).text == retrievedString);
        memset(readBuffer, 0x00, EMU_EEPROM_PAGE_SIZE);
    }

    // rewrite the first file
    ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM2.write(entry.at(0).index, entry.at(0).text.c_str()));
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
    _storageMock._pageArray.at(0).at(0) = 0x00;
    _storageMock._pageArray.at(1).at(0) = 0x01;

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

    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_1) == EmuEEPROM::pageStatus_t::ACTIVE);
    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_2) == EmuEEPROM::pageStatus_t::FORMATTED);

    for (size_t i = 0; i < loopsBeforeTransfer; i++)
    {
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(INDEX, text.c_str()));
    }

    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_1) == EmuEEPROM::pageStatus_t::FORMATTED);
    ASSERT_TRUE(_emuEEPROM.pageStatus(EmuEEPROM::page_t::PAGE_2) == EmuEEPROM::pageStatus_t::ACTIVE);

    // content must still be valid after transfer
    ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    std::string retrievedString = readBuffer;
    ASSERT_TRUE(text == retrievedString);
}

TEST_F(EmuEEPROMTest, MultiplePageTransfer)
{
    const uint32_t INDEX                            = 0xEEEE;
    char           readBuffer[EMU_EEPROM_PAGE_SIZE] = {};
    uint16_t       readLength                       = 0;
    std::string    text                             = "page transfer";

    auto         sizeInEEPROM        = EmuEEPROM::entrySize(text.size());
    auto         loopsBeforeTransfer = EMU_EEPROM_PAGE_SIZE / sizeInEEPROM;
    const size_t TRANSFERS           = loopsBeforeTransfer * 4;

    for (size_t i = 0; i < TRANSFERS; i++)
    {
        ASSERT_EQ(EmuEEPROM::writeStatus_t::OK, _emuEEPROM.write(INDEX, text.c_str()));
    }

    // content must still be valid after transfer
    ASSERT_EQ(EmuEEPROM::readStatus_t::OK, _emuEEPROM.read(INDEX, readBuffer, readLength, EMU_EEPROM_PAGE_SIZE));

    std::string retrievedString = readBuffer;
    ASSERT_TRUE(text == retrievedString);
}