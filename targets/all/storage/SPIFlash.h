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

#include <io/PipeWriter.h>

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
    async(Read, uint32_t addr, Buffer data) { return async_forward(ReadImpl, addr, data.Pointer(), data.Length()); }
    //! Reads data from the SPI flash memory into the specified memory location (e.g. hardware register)
    async(ReadToRegister, uint32_t addr, volatile void* reg, size_t length);
    //! Reads data from the SPI flash memory directly into the specified I/O pipe
    async(ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout = Timeout::Infinite);
    //! Writes data to the SPI flash memory
    async(Write, uint32_t addr, Span data) { return async_forward(WriteImpl, addr, data.Pointer(), data.Length()); }
    //! Fills a range of the SPI flash memory
    async(Fill, uint32_t addr, uint8_t value, size_t length);
    //! Checks if a range of the SPI flash memory is empty
    async(IsEmpty, uint32_t addr, size_t length);
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
    //! Gets the n-th smallest sector size in bytes
    uint32_t SectorMask(uint32_t n = 0) const { return (1 << sector[n].bits) - 1; }
    //! Gets the n-th smallest sector size in bits
    uint32_t SectorSizeBits(uint32_t n = 0) const { return sector[n].bits; }
    //! Gets the address of the beginning of the sector of the specified size
    uint32_t SectorAddress(uint32_t addr, uint32_t n = 0) const { return addr >> sector[n].bits << sector[n].bits; }
    //! Gets the address of the beginning of the page
    uint32_t PageAddress(uint32_t addr) const { return addr & ~PAGE_MASK; }
    //! Checks if the two addresses are in the same sector of the specified size
    bool IsSameSector(uint32_t addr1, uint32_t addr2, uint32_t n = 0) { return !((addr1 ^ addr2) >> sector[n].bits); }
    //! Checks if the two addresses are on the same programmable page
    constexpr bool IsSamePage(uint32_t addr1, uint32_t addr2) { return !((addr1 ^ addr2) >> PAGE_BITS); }
    //! Calculates the remaining bytes from the specified address in the sector of the specified size
    size_t SectorRemaining(uint32_t addr, uint32_t n = 0) { return (~addr & SectorMask(n)) + 1; }
    //! Calculates the remaining bytes from the specified address in the sector of the specified size
    constexpr size_t PageRemaining(uint32_t addr) { return (~addr & PAGE_MASK) + 1; }

private:
    enum
    {
        OP_STATUS = 0x05,

        OP_WREN = 0x06,
        OP_PROGRAM = 0x02,

        OP_READ = 0x03,
        OP_READ_SFDP = 0x5A,

        OP_CHIP_ERASE = 0x60,

        OP_RDID = 0x9F,

        PAGE_BITS = 8,
        PAGE_SIZE = 1 << PAGE_BITS,
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

    async(ReadSFDP, uint32_t addr, Buffer buffer) { return async_forward(ReadSFDP, addr, buffer.Pointer(), buffer.Length()); }
    async(ReadSFDP, uint32_t addr, char* buffer, size_t length);
    async(ReadID);
    async(ReadImpl, uint32_t addr, char* buffer, size_t length);
    async(WriteImpl, uint32_t addr, const char* buffer, size_t length);
    async(SyncAndAcquire);

    void AddSectorType(SectorType sec);
};

}
