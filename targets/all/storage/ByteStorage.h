/*
 * Copyright (c) 2020 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/BlockStorage.h
 */

#pragma once

#include <kernel/kernel.h>

#include <io/PipeReader.h>
#include <io/PipeWriter.h>

namespace storage
{

class ByteStorageSpan;

//! Represents external byte-addressable storage that can be
//! erased only by sectors (e.g. NOR Flash)
class ByteStorage
{
public:
    //! Reads data from the storage into the specified buffer
    async(Read, uint32_t addr, Buffer data) { return async_forward(ReadImpl, addr, data.Pointer(), data.Length()); }
    //! Reads data from the storage into the specified memory location (e.g. hardware register)
    virtual async(ReadToRegister, uint32_t addr, volatile void* reg, size_t length) = 0;
    //! Reads data from the storage directly into the specified I/O pipe
    virtual async(ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout = Timeout::Infinite) = 0;
    //! Writes data to the storage
    async(Write, uint32_t addr, Span data) { return async_forward(WriteImpl, addr, data.Pointer(), data.Length()); }
    //! Writes data to the storage directly from the specified I/O pipe
    virtual async(WriteFromPipe, io::PipeReader pipe, uint32_t addr, size_t length, Timeout timeout = Timeout::Infinite) = 0;
    //! Fills a range of the storage with the specified value
    virtual async(Fill, uint32_t addr, uint8_t value, size_t length) = 0;
    //! Checks if a range of the storage is empty
    async(IsEmpty, uint32_t addr, size_t length) { return async_forward(IsAll, addr, 0xFF, length); }
    //! Checks if a range of the storage is filled with the specified value
    virtual async(IsAll, uint32_t addr, uint8_t value, size_t length) = 0;
    //! Erases at least the specified range of the storage, depending on sector size
    virtual async(Erase, uint32_t addr, uint32_t length) = 0;
    //! Erases the first block of the specified range of the storage, depending on sector size
    //! @returns the address of the next block to be erased
    virtual async(EraseFirst, uint32_t addr, uint32_t length) = 0;
    //! Erases the entire storage
    async(EraseAll) { return async_forward(Erase, 0, size); }
    //! Makes sure all write operations have completed
    virtual async(Sync) = 0;

    //! Size of the storage in bytes
    constexpr size_t Size() const { return size; }
    //! Gets the sector size in bytes
    constexpr size_t SectorSize() const { return sectorMask + 1; }
    //! Gets the number of bits covered by sector
    constexpr size_t SectorSizeBits() const { return 32 - __builtin_clz(sectorMask); }
    //! Gets the sector mask
    constexpr uint32_t SectorMask() const { return sectorMask; }
    //! Gets the address of the beginning of the sector
    constexpr uint32_t SectorAddress(uint32_t addr) const { return addr & ~sectorMask; }
    //! Checks if the two addresses are in the same sector
    constexpr bool IsSameSector(uint32_t addr1, uint32_t addr2) { return !((addr1 ^ addr2) & ~sectorMask); }
    //! Calculates the remaining bytes from the specified address to the end of the sector
    size_t SectorRemaining(uint32_t addr) { return (~addr & sectorMask) + 1; }

    //! Gets the specified sub-span of the entire storage
    ByteStorageSpan GetSpan(uint32_t addr, size_t length);
    //! Gets the span representing the specified sector
    ByteStorageSpan SectorSpan(uint32_t addr);
    //! Gets the span representing the rest of the specified sector (from addr to end)
    ByteStorageSpan RestOfSectorSpan(uint32_t addr);

protected:
    void Initialize(size_t size, size_t sectorSize)
    {
        ASSERT(__builtin_clz(sectorSize) + __builtin_ctz(sectorSize) == 31);
        ASSERT(!(size & sectorSize));
        this->size = size;
        this->sectorMask = sectorSize - 1;
    }

    //! Reads data from the storage into the specified buffer - implementation
    virtual async(ReadImpl, uint32_t addr, void* buffer, size_t length) = 0;
    //! Writes data to the storage - implementation
    virtual async(WriteImpl, uint32_t addr, const void* buffer, size_t length) = 0;

private:
    size_t size = 0, sectorMask = 0;
};

}

#include <storage/ByteStorageSpan.h>
