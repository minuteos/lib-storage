/*
 * Copyright (c) 2022 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * test/storage/TestByteStorage.cpp
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/ByteStorage.h>

namespace storage
{

class TestByteStorage : public ByteStorage
{
public:
    TestByteStorage(size_t size, size_t sectorSize = 1024);
    ~TestByteStorage();

    // various test timings
    int tRmin = 4, tRmax = 16;      //< min/max page read cycles
    int tWmin = 4, tWmax = 30;      //< min/max page write cycles
    int tEPmin = 100, tEPmax = 200; //< min/max page erase cycles

    TestByteStorage& MakeSync() { tRmin = tRmax = tWmin = tWmax = tEPmin = tEPmax = 0; return *this; }

private:
    uint8_t* data;
    static constexpr size_t pageSize = 256, pageMask = 255;

    async(ReadImpl, uint32_t addr, void* buffer, size_t length) final override;
    async(WriteImpl, uint32_t addr, const void* buffer, size_t length) final override;

    async(Wait, int tMin, int tMax);

    size_t PageRemaining(uint32_t addr) { return (~addr & 0xFF) + 1; }

public:
    async(ReadToRegister, uint32_t addr, volatile void* reg, size_t length) final override;
    async(ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout) final override;

    async(WriteFromPipe, io::PipeReader pipe, uint32_t addr, size_t length, Timeout timeout) final override;
    async(Fill, uint32_t addr, uint8_t value, size_t length) final override;

    async(IsAll, uint32_t addr, uint8_t value, size_t length) final override;
    async(Erase, uint32_t addr, uint32_t length) final override;
    async(EraseFirst, uint32_t addr, uint32_t length) final override;
    async(Sync) final override async_def_return(0);
};

}
