/*
 * Copyright (c) 2020 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SPIFlashStorage.h
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/ByteStorage.h>
#include <storage/SPIFlash.h>

namespace storage
{

class SPIFlashStorage : public ByteStorage
{
public:
    SPIFlashStorage(SPIFlash& flash)
        : flash(flash) {}

    async(Init, uint32_t start, size_t length = 0);

private:
    SPIFlash& flash;
    uint32_t start;

    async(ReadImpl, uint32_t addr, void* buffer, size_t length) final override;
    async(WriteImpl, uint32_t addr, const void* buffer, size_t length) final override;

public:
    async(ReadToRegister, uint32_t addr, volatile void* reg, size_t length) final override;
    async(ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout) final override;

    async(WriteFromPipe, io::PipeReader pipe, uint32_t addr, size_t length, Timeout timeout) final override;
    async(Fill, uint32_t addr, uint8_t value, size_t length) final override;

    async(IsAll, uint32_t addr, uint8_t value, size_t length) final override;
    async(Erase, uint32_t addr, uint32_t length) final override;
    async(EraseFirst, uint32_t addr, uint32_t length) final override;
    async(Sync) final override { return async_forward(flash.Sync); }
};

}
