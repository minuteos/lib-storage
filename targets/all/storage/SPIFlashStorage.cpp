/*
 * Copyright (c) 2020 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SPIFlashStorage.cpp
 */

#include "SPIFlashStorage.h"

namespace storage
{

async(SPIFlashStorage::Init, uint32_t start, size_t length)
async_def()
{
    if (!flash.init)
    {
        await(flash.Init);
    }
    this->start = start;
    Initialize(length ? start + length : flash.Size() - start, flash.SectorSize());
    ASSERT(start <= flash.Size());
    ASSERT(start + Size() <= flash.Size());
}
async_end

async(SPIFlashStorage::ReadImpl, uint32_t addr, void* buffer, size_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.Read, start + addr, Buffer(buffer, length));
}

async(SPIFlashStorage::ReadToRegister, uint32_t addr, volatile void* reg, size_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.ReadToRegister, start + addr, reg, length);
}

async(SPIFlashStorage::ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.ReadToPipe, pipe, start + addr, length, timeout);
}


async(SPIFlashStorage::WriteImpl, uint32_t addr, const void* buffer, size_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.Write, start + addr, Span(buffer, length));
}

async(SPIFlashStorage::WriteFromPipe, io::PipeReader pipe, uint32_t addr, size_t length, Timeout timeout)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.WriteFromPipe, pipe, start + addr, length, timeout);
}

async(SPIFlashStorage::Fill, uint32_t addr, uint8_t value, size_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.Fill, start + addr, value, length);
}


async(SPIFlashStorage::IsEmpty, uint32_t addr, size_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.IsEmpty, start + addr, length);
}

async(SPIFlashStorage::Erase, uint32_t addr, uint32_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.Erase, start + addr, length);
}

async(SPIFlashStorage::EraseFirst, uint32_t addr, uint32_t length)
{
    ASSERT(addr <= Size());
    ASSERT(addr + length <= Size());
    return async_forward(flash.EraseFirst, start + addr, length);
}

}
