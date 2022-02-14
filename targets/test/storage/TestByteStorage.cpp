/*
 * Copyright (c) 2022 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * test/storage/TestByteStorage.cpp
 */

#include "TestByteStorage.h"

#define MYDBG(...)  DBGCL("TestStorage", __VA_ARGS__)

#define DIAG_READ   1
#define DIAG_WRITE  2
#define DIAG_WAIT   4

#ifndef TEST_FLASH_DIAG
#define TEST_FLASH_DIAG    DIAG_READ | DIAG_WRITE
#endif

#if TEST_FLASH_DIAG
#define MYDIAG(mask, ...)	if ((TEST_FLASH_DIAG) & (mask)) { MYDBG(__VA_ARGS__); }
#else
#define MYDIAG(...)
#endif

namespace storage
{

TestByteStorage::TestByteStorage(size_t size, size_t sectorSize)
    : data(new uint8_t[size])
{
    memset(data, 255, size);
    Initialize(size, sectorSize);
}

TestByteStorage::~TestByteStorage()
{
    delete[] data;
}

async(TestByteStorage::Wait, int min, int max)
async_def(
    int n;
)
{
    f.n = min + rand() % (max - min + 1);
    while (f.n-- > 0)
    {
        async_yield();
    }
}
async_end

async(TestByteStorage::ReadImpl, uint32_t addr, void* buffer, size_t length)
async_def(
    size_t read;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    while (f.read < length)
    {
        await(Wait, tRmin, tRmax);
        size_t blk = std::min(length - f.read, pageSize);
        memcpy((uint8_t*)buffer + f.read, data + addr, blk);
        f.read += blk;
    }

    MYDIAG(DIAG_READ, "%X==%H", addr, Span(buffer, length));
}
async_end

async(TestByteStorage::ReadToRegister, uint32_t addr, volatile void* reg, size_t length)
async_def(
    size_t read;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    while (f.read < length)
    {
        await(Wait, tRmin, tRmax);
        size_t blk = std::min(length - f.read, pageSize);
        while (blk--)
        {
            *(uint8_t*)reg = data[addr + f.read++];
        }
    }

    MYDIAG(DIAG_READ, "%X=%d=>%p", addr, length, reg);
}
async_end

async(TestByteStorage::ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout)
async_def(
    size_t read;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    while (f.read < length)
    {
        if (!pipe.Available() && !await(pipe.Allocate, length - f.read, timeout))
        {
            break;
        }

        await(Wait, tRmin, tRmax);

        auto buf = pipe.GetBuffer().Left(pageSize).Left(length - f.read);
        memcpy(buf.Pointer(), data + addr + f.read, buf.Length());
        pipe.Advance(buf.Length());
        f.read += buf.Length();

        MYDIAG(DIAG_READ, "%X==%H", addr, buf);
    }

    async_return(f.read);
}
async_end

async(TestByteStorage::WriteImpl, uint32_t addr, const void* buffer, size_t length)
async_def(
    size_t written, len;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    while (f.written < length)
    {
        f.len = std::min(PageRemaining(addr + f.written), length - f.written);

        await(Wait, tWmin, tWmax);
        auto pd = data + addr + f.written;
        auto ps = (const uint8_t*)buffer + f.written;
        MYDIAG(DIAG_WRITE, "%X=%H", addr + f.written, Span(ps, f.len));
        for (size_t i = 0; i < f.len; i++)
        {
            *pd++ &= *ps++;
        }
        f.written += f.len;
    }
}
async_end

async(TestByteStorage::WriteFromPipe, io::PipeReader pipe, uint32_t addr, size_t length, Timeout timeout)
async_def(
    size_t written;
    Span span;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    while (f.written < length)
    {
        if (!pipe.Available() && !await(pipe.Require, 1, timeout))
        {
            break;
        }

        f.span = pipe.GetSpan().Left(length - f.written);
        await(Write, addr + f.written, f.span);
        pipe.Advance(f.span.Length());
        f.written += f.span.Length();
    }

    async_return(f.written);
}
async_end

async(TestByteStorage::Fill, uint32_t addr, uint8_t value, size_t length)
async_def(
    size_t written, len;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    while (f.written < length)
    {
        f.len = std::min(PageRemaining(addr + f.written), length - f.written);
        MYDIAG(DIAG_WRITE, "%X=%d*%02X", addr + f.written, f.len, value);

        await(Wait, tWmin, tWmax);
        auto pd = data + addr + f.written;
        for (size_t i = 0; i < f.len; i++)
        {
            *pd++ &= value;
        }
        f.written += f.len;
    }
}
async_end

async(TestByteStorage::IsAll, uint32_t addr, uint8_t value, size_t length)
async_def(
    size_t checked;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    if (!length)
    {
        async_return(true);
    }

    while (f.checked < length)
    {
        await(Wait, tRmin, tRmax);
        size_t blk = std::min(length - f.checked, pageSize);
        for (size_t i = 0; i < blk; i++)
        {
            if (data[addr + f.checked + i] != value)
            {
                MYDIAG(DIAG_READ, "%X!=%X: %X", addr + f.checked + i, value, data[addr + f.checked + i]);
                async_return(false);
            }
        }
        f.checked += blk;
    }

    async_return(true);
}
async_end

async(TestByteStorage::Erase, uint32_t addr, uint32_t length)
async_def(
    uint32_t start, end;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    {
        uint32_t mask = SectorMask();

        // calculate start/end sector boundaries
        f.start = addr & ~mask;
        f.end = (addr + length + mask) & ~mask;
    }

    while (f.start < f.end)
    {
        uint32_t next;
        next = await(EraseFirst, f.start, f.end - f.start);
        if (next == f.start)
        {
            // failed to erase anything
            async_return(false);
        }
        f.start = next;
    }

    async_return(true);
}
async_end

async(TestByteStorage::EraseFirst, uint32_t addr, uint32_t length)
async_def(
    uint32_t start, end;
)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());

    {
        // calculate start/end sector boundaries
        uint32_t mask = SectorMask();
        f.start = addr & ~mask;
        f.end = (addr + length + mask) & ~mask;
    }

    if (f.start + SectorSize() <= f.end)
    {
        f.end = f.start + SectorSize();

        MYDBG("erasing %d KB block starting at %X", SectorSize(), f.start);
        MYDIAG(DIAG_WRITE, "%X...", f.start);
        await(Wait, tEPmin, tEPmax);
        memset(data + f.start, 0xFF, SectorSize());
        async_return(f.end);
    }

    MYDBG("invalid erase range %X-%X", f.start, f.end);
    async_return(addr);
}
async_end

}
