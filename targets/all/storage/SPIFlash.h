/*
 * Copyright (c) 2020 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SPIFlash.h
 */

#pragma once

#include <base/base.h>
#include <base/Span.h>

#include <bus/SPI.h>

namespace storage
{

class SPIFlash
{
public:
    SPIFlash(bus::SPI spi, GPIOPin cs)
        : spi(spi), cs(spi.GetChipSelect(cs)) {}

    //! Initialize the SPI flash memory, reading JEDEC data, etc.
    async(Init);
    //! Reads data from the SPI flash memory into the specified buffer
    async(Read, uint32_t addr, Buffer data);
    //! Writes data to the SPI flash memory
    async(Write, uint32_t addr, Span data);
    //! Flushes any unwritten data to the SPI flash memory (noop, just for interface compatibility with BufferedSPIFlash)
    async(Flush) async_def_return(true);
    //! Erases at least the specified range of the SPI flash memory, depending on smallest sector size
    async(Erase, uint32_t addr, uint32_t length);
    //! Erases the first block of the specified range of the SPI flash memory, depending on smallest block size
    //! Returns the address of the next block to be erased
    async(EraseFirst, uint32_t addr, uint32_t length);
    //! Erases the entire SPI flash memory
    async(MassErase);

    //! Size of the SPI flash memory in bytes
    uint32_t Size() const { return size; }
    //! Count of erasable sector sizes
    uint32_t SectorTypeCount() const { return sectorTypeCount; }
    //! Gets the n-th smallest sector size in bytes
    uint32_t SectorSize(uint32_t n = 0) const { return 1 << sector[n].bits; }
    //! Gets the n-th smallest sector size in bits
    uint32_t SectorSizeBits(uint32_t n = 0) const { return sector[n].bits; }

private:
    enum
    {
        OP_STATUS = 0x05,

        OP_WREN = 0x06,
        OP_PROGRAM = 0x02,

        OP_READ = 0x03,
        OP_READ_SFDP = 0x5A,

        OP_CHIP_ERASE = 0x60,

        PAGE_SIZE = 256,
        PAGE_MASK = PAGE_SIZE - 1,
    };

    struct SFDPHeader
    {
        uint32_t sig;
        uint8_t min, maj, cnt;
        uint8_t : 8;
    };

    struct SFDPTable
    {
        uint8_t id, min, maj, words;
        uint32_t addr : 24;
        uint32_t : 8;
    };

    enum JEDECAddressBytes
    {
        Addr3Byte, Addr3Or4Byte, Addr4Byte,
    };

    struct FastReadInfo
    {
        uint8_t ws : 5;
        uint8_t mode : 3;
        uint8_t op;
    };

    struct SectorType
    {
        uint8_t bits;
        uint8_t op;

        static int Compare(SectorType& s1, SectorType& s2) { return s1.bits - s2.bits; }
    };

    struct SFDPJEDEC
    {
        // uint8_t 0
        uint8_t : 1;
        uint8_t noErase4k : 1;
        uint8_t writeGranularity : 1;
        uint8_t vsWriteEnableRequired : 1;
        uint8_t vsWriteEnableOpcode : 1;
        uint8_t : 3;

        // uint8_t 1
        uint8_t opErase4k;

        // uint8_t 2
        uint8_t fast112sup : 1;
        JEDECAddressBytes addressBytes : 2;
        uint8_t dtr : 1;
        uint8_t fast122sup : 1;
        uint8_t fast144sup : 1;
        uint8_t fast114sup : 1;
        uint8_t : 1;

        // uint8_t 3
        uint8_t : 8;

        // uint8_t 4-7
        uint32_t density;

        // uint8_t 8-15 - fast read op data
        FastReadInfo fast144, fast114, fast112, fast122;

        // uint8_t 16
        uint8_t fast222sup : 1;
        uint8_t : 3;
        uint8_t fast444sup : 1;
        uint8_t : 3;

        // uint8_t 17-21
        uint8_t : 8;
        uint32_t : 32;

        // uint8_t 22-23
        FastReadInfo fast222;

        // uint8_t 24-25
        uint16_t : 16;

        // uint8_t 26-27
        FastReadInfo fast444;

        // uint8_t 28-35
        SectorType sec[4];
    };

    bus::SPI spi;
    bus::SPI::ChipSelect cs;
    bool init = false;
    bool deviceBusy = false;

    uint32_t size;
    SectorType sector[4];
    uint32_t sectorTypeCount = 0;

    async(ReadSFDP, uint32_t addr, Buffer data);
    async(WriteUnchecked, uint32_t addr, Span data);
    async(Sync);

    void AddSectorType(SectorType sec);
};

}
