/*
 * Copyright (c) 2021 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/ByteStorageSpan.h
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/ByteStorage.h>

namespace storage
{

//! Represents a span of a @ref ByteStorage
class ByteStorageSpan
{
public:
    //! Reads data from the storage into the specified buffer
    async(Read, size_t offset, Buffer data) const
        { return async_forward(storage->Read, addr + offset, Buffer(data.Pointer(), LimitLength(offset, data.Length()))); }
    //! Reads data from the storage into the specified memory location (e.g. hardware register)
    async(ReadToRegister, size_t offset, volatile void* reg, size_t length) const
        { return async_forward(storage->ReadToRegister, addr + offset, reg, LimitLength(offset, length)); }
    //! Reads data from the storage directly into the specified I/O pipe
    async(ReadToPipe, io::PipeWriter pipe, size_t offset, size_t length, Timeout timeout = Timeout::Infinite) const
        { return async_forward(storage->ReadToPipe, pipe, addr + offset, LimitLength(offset, length), timeout); }

    //! Writes data to the storage
    async(Write, size_t offset, Span data) const
        { return async_forward(storage->Write, addr + offset, Span(data.Pointer(), LimitLength(offset, data.Length()))); }
    //! Writes data to the storage directly from the specified I/O pipe
    async(WriteFromPipe, io::PipeReader pipe, size_t offset, size_t length, Timeout timeout = Timeout::Infinite) const
        { return async_forward(storage->WriteFromPipe, pipe, addr + offset, LimitLength(offset, length), timeout); }
    //! Fills a range of the storage with the specified value
    async(Fill, size_t offset, uint8_t value, size_t length) const
        { return async_forward(storage->Fill, addr + offset, value, LimitLength(offset, length)); }

    //! Size of the storage span in bytes
    constexpr size_t Size() const { return length; }
    //! Offset of the storage span in the ByteStorage
    constexpr size_t Offset() const { return addr; }
    //! Gets the ByteStorage in which this span is located
    constexpr ByteStorage& Storage() const { return *storage; }

protected:
    ByteStorageSpan()
        : storage(NULL), addr(0), length(0) {}

private:
    ByteStorageSpan(ByteStorage& storage, uint32_t addr, size_t length)
        : storage(&storage), addr(addr), length(length) {}

    ByteStorage* storage;
    uint32_t addr;
    size_t length;

    constexpr size_t LimitLength(size_t offset, size_t length) const
        { return std::max(0, std::min(int(this->length - offset), int(length))); }

    friend class ByteStorage;
};

inline ByteStorageSpan ByteStorage::GetSpan(uint32_t addr, size_t length)
{
    ASSERT(addr <= size && addr + length <= size);
    return ByteStorageSpan(*this, addr, length);
}

inline ByteStorageSpan ByteStorage::SectorSpan(uint32_t addr)
{
    ASSERT(addr < size);
    return ByteStorageSpan(*this, SectorAddress(addr), SectorSize());
}

inline ByteStorageSpan ByteStorage::RestOfSectorSpan(uint32_t addr)
{
    ASSERT(addr < size);
    return ByteStorageSpan(*this, addr, SectorRemaining(addr));
}

}
