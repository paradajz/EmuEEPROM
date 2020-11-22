# Emulated EEPROM based on STM32 application note AN3969

Compared to the original implementation from ST, this one:

* is written in C++
* is hardware-independent - flash access is done through `StorageAccess` interface
* offers significantly faster read/write access to flash memory and page transfers, mainly due to the fact that the
contents of entire flash page is stored in RAM
* is unsuitable for devices with low amunt of RAM
* offers ability to specify factory flash page which will get copied to first page when formatting is initiated