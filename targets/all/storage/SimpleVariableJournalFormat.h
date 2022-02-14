/*
 * Copyright (c) 2021 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SimpleVariableJournalFormat.h
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/JournalFormat.h>

namespace storage
{

class SimpleVariableJournalFormat : public JournalFormat
{
public:
    SimpleVariableJournalFormat(uint32_t magic)
        : magic(magic) {}

private:
    uint32_t magic;

    struct PageHeader
    {
        uint32_t magic;
        uint32_t sequence;
    };

    struct RecordHeader
    {
        uint16_t size;

        constexpr bool IsEmpty() const { return size == 0xFFFF; }
        constexpr bool IsBad() const { return size & 0x8000; }
        constexpr size_t Size() const { return size & 0x7FFF; }
    };

    virtual async(ScanSector, const ByteStorageSpan& sector, SectorInfo& info, const SectorInfo* following) const final override;
    virtual async(ScanRecord, const ByteStorageSpan& sectorRemaining, const SectorInfo& sectorInfo, RecordInfo& info) const final override;
    virtual async(InitSector, const ByteStorageSpan& sector, SectorInfo& info) final override;
    virtual async(InitRecord, const ByteStorageSpan& sectorRemaining, RecordInfo& info, size_t payload) final override;
    virtual async(CommitRecord, const ByteStorageSpan& payload) final override;
};

}
