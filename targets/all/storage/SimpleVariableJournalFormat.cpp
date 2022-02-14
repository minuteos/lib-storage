/*
 * Copyright (c) 2022 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SimpleVariableJournalFormat.cpp
 */

#include "SimpleVariableJournalFormat.h"

using namespace storage;

async(SimpleVariableJournalFormat::ScanSector, const ByteStorageSpan& sector, SectorInfo& info, const SectorInfo* following) const
async_def(
    PageHeader ph;
)
{
    await(sector.Read, 0, f.ph);
    info.firstRecord = sizeof(PageHeader);
    info.sequence = f.ph.sequence;
    if (Span(f.ph).IsAllOnes())
    {
        info.state = SectorState::Empty;
    }
    else if (f.ph.magic != magic)
    {
        info.state = SectorState::Bad;
    }
    else if (following != NULL && f.ph.sequence + 1 == following->sequence)
    {
        info.state = SectorState::ValidPreceding;
    }
    else
    {
        info.state = SectorState::Valid;
    }
}
async_end

async(SimpleVariableJournalFormat::ScanRecord, const ByteStorageSpan& sectorRemaining, const SectorInfo& sectorInfo, RecordInfo& info) const
async_def(
    RecordHeader hdr;
)
{
    await(sectorRemaining.Read, 0, f.hdr);
    info.payload = f.hdr.Size();
    info.nextRecord = info.payload + sizeof(RecordHeader);
    if (f.hdr.IsEmpty())
    {
        info.state = RecordState::Empty;
    }
    else if (f.hdr.IsBad())
    {
        info.state = RecordState::Bad;
    }
    else
    {
        info.state = RecordState::Valid;
    }
    async_return(sizeof(RecordHeader));
}
async_end

async(SimpleVariableJournalFormat::InitSector, const ByteStorageSpan& sector, SectorInfo& info)
async_def()
{
    info.sequence = (info.IsValid() ? info.sequence : 0) + 1;
    await(sector.Write, offsetof(PageHeader, sequence), info.sequence);
    await(sector.Write, offsetof(PageHeader, magic), magic);
    info.firstRecord = sizeof(PageHeader);
    info.state = SectorState::Valid;
}
async_end

async(SimpleVariableJournalFormat::InitRecord, const ByteStorageSpan& sectorRemaining, RecordInfo& info, size_t payload)
async_def(
    RecordHeader hdr;
)
{
    // limit the payload to theoretical maximum
    f.hdr.size = std::min(payload, size_t(0x7FFF));

    if ((sectorRemaining.Offset() & sectorRemaining.Storage().SectorMask()) == sizeof(PageHeader))
    {
        // further limit the payload to sector maximum
        f.hdr.size = std::min(size_t(f.hdr.size), sectorRemaining.Size() - sizeof(RecordHeader));
    }

    if (sizeof(RecordHeader) + f.hdr.size > sectorRemaining.Size())
    {
        // sector is full, record won't fit
        info.state = RecordState::Bad;
        async_return(0);
    }

    f.hdr.size |= 0x8000;   // mark as unfinished
    await(sectorRemaining.Write, 0, f.hdr);
    info.payload = f.hdr.Size();
    info.nextRecord = sizeof(RecordHeader) + info.payload;
    info.state = RecordState::Valid;
    async_return(sizeof(RecordHeader));
}
async_end

async(SimpleVariableJournalFormat::CommitRecord, const ByteStorageSpan& payload)
async_def()
{
    ASSERT(payload.Storage().IsSameSector(payload.Offset(), payload.Offset() - 2));
    // just clear the top bit in the length field
    await(payload.Storage().Write, payload.Offset() - 2, (const uint16_t[]){0x7FFF});
}
async_end
