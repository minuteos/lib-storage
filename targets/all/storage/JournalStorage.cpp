/*
 * Copyright (c) 2021 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/JournalStorage.cpp
 */

#include "JournalStorage.h"

namespace storage
{

async(JournalStorage::Scan)
async_def(
    uint32_t addr;
    uint32_t badSectors, freeSectors, baseSeq;
    JournalFormat::SectorInfo si;
    JournalFormat::SectorInfo siFirst, siLast;
    SectorEnumerator se;
    RecordEnumerator re;
)
{
    MYDBG("Scanning flash sectors");
    lastSector = ~0u;

    // first search for the last written sector (by sequence)
    // the first sector found is used to disambiguate the situation
    // if sequence overflows multiple times (due to corruption or a bug)
    for (f.addr = 0; f.addr < storage.Size(); f.addr += storage.SectorSize())
    {
        await(format.ScanSector, storage.SectorSpan(f.addr), f.si);
        async_yield();
        if (f.si.IsEmpty())
        {
            MYTRACE(3, "Scanning %X - EMPTY", f.addr);
            f.freeSectors++;
            continue;
        }

        if (!f.si.IsValid())
        {
            // bad sector
            MYTRACE(1, "Scanning %X - BAD", f.addr);
            f.badSectors++;
            continue;
        }

        MYTRACE(2, "Scanning %X - VALID, seq %d", f.addr, f.si.sequence);
        if (lastSector == ~0u)
        {
            f.baseSeq = f.si.sequence;
        }
        else if (!(OVF_GT(f.si.sequence, f.siLast.sequence) && OVF_GT(f.si.sequence, f.baseSeq)))
        {
            // older than what we already have
            continue;
        }

        // store the last sector
        lastSector = f.addr;
        f.siLast = f.si;
    }

    MYDBG("Found %d free sectors out of %d (%d bad sectors)", f.freeSectors, (storage.Size() >> storage.SectorSizeBits()) - f.badSectors, f.badSectors);

    if (lastSector == ~0u)
    {
        MYDBG("Storage is empty");
        firstSector = 0;
        lastSector = 0;
        freeOffset = 0;
        last = {};
    }
    else
    {
        MYDBG("Highest sequence sector found @ %X, seq %d", lastSector, f.siLast.sequence);

        EnumerateRecords(f.re, lastSector);
        while (await(NextRecord, f.re))
        {
        }

        if (f.re.IsEmpty())
        {
            MYDBG("Last sector still has free space @ %X, will be used for new records", f.re.r);
            freeOffset = f.re.r.addr - lastSector;
        }
        else
        {
            MYDBG("Last sector is full or corrupted @ %X", f.re.r);
            freeOffset = 0;
        }

        // now move back as far as the sequence numbers are contiguous
        f.siFirst = f.siLast;
        firstSector = lastSector;
        for (f.addr = PreviousSector(lastSector); f.addr != lastSector; f.addr = PreviousSector(f.addr))
        {
            await(format.ScanSector, storage.SectorSpan(f.addr), f.si, &f.siFirst);
            async_yield();
            if (f.si.IsPreceding())
            {
                firstSector = f.addr;
                f.siFirst = f.si;
            }
            else if (f.si.IsValid())
            {
                MYDBG("Found unexpected sector sequence @ %X - %d", f.addr, f.si.sequence);
                break;
            }
        }

        MYDBG("Stored sequence %d - %d in sectors %X - %X", f.siFirst.sequence, f.siLast.sequence, firstSector, lastSector);
        last = f.siLast;
    }
}
async_end

async(JournalStorage::PreviousSector, SectorEnumerator& se)
async_def(
    JournalFormat::SectorInfo si;
)
{
    for (;;)
    {
        if (se.s.addr == firstSector)
        {
            se = SectorEnumerator();
            async_return(false);
        }

        if (!se)
        {
            se.s = lastSector;
        }
        else
        {
            se.s.addr = PreviousSector(se.s.addr);
        }

        await(format.ScanSector, storage.SectorSpan(se.s.addr), f.si);
        if (f.si.IsValid())
        {
            async_return(true);
        }
    }
}
async_end

async(JournalStorage::NextSector, SectorEnumerator& se)
async_def(
    JournalFormat::SectorInfo si;
)
{
    for (;;)
    {
        if (se.s.addr == lastSector)
        {
            se = SectorEnumerator();
            async_return(false);
        }

        if (!se)
        {
            se.s = firstSector;
        }
        else
        {
            se.s.addr = NextSector(se.s.addr);
        }

        await(format.ScanSector, storage.SectorSpan(se.s.addr), f.si);
        if (f.si.IsValid())
        {
            async_return(true);
        }
    }
}
async_end

async(JournalStorage::ReadSectorHeader, const SectorEnumerator& se, const Buffer& buf, size_t offset)
async_def(
    Buffer buf;
)
{
    if (se.IsValid() && offset < storage.SectorSize())
    {
        f.buf = buf.Left(storage.SectorSize() - offset);
        await(storage.Read, se.s.addr + offset, f.buf);
        async_return(f.buf.Length());
    }
    async_return(0);
}
async_end

async(JournalStorage::NextRecord, RecordEnumerator& re)
async_def(
    JournalFormat::RecordInfo ri;
)
{
    if (re.r.addr == re.rNext.addr && re.si.IsBad())
    {
        // we need the sector header before enumerating
        await(format.ScanSector, storage.SectorSpan(re.r.addr), re.si);
        re.rNext = re.r.addr + re.si.firstRecord;
    }

    if (!re.si.IsValid())
    {
        async_return(0);
    }

    while (storage.IsSameSector(re.r.addr, re.rNext.addr))
    {
        re.r = re.rNext;
        intptr_t payloadOffset;
        payloadOffset = await(format.ScanRecord, storage.RestOfSectorSpan(re.r.addr), re.si, f.ri);
        if (f.ri.IsEmpty())
        {
            async_return(0);
        }
        re.rNext = re.r.addr + f.ri.NextRecordOffset();
        if (f.ri.IsBad())
        {
            if (re.rNext.addr != re.r.addr)
            {
                // skip over the bad record
                continue;
            }
            // cannot continue, unable to skip
            re.rNext.addr--;    // mark bad
            async_return(0);
        }

        // move address to payload, return payload length
        re.r.addr += payloadOffset;
        re.len = f.ri.PayloadLength();
        async_return(f.ri.PayloadLength());
    }

    if (re.rNext.addr > storage.SectorAddress(re.r.addr) + storage.SectorSize())
    {
        MYDBG("Next record pointer went beyond sector end: %X", re.rNext);
    }
    async_return(0);
}
async_end

async(JournalStorage::ReadRecord, const RecordEnumerator& re, const Buffer& buf, size_t offset)
async_def(
    Buffer buf;
)
{
    if (re.si.IsValid() && offset < re.len)
    {
        f.buf = buf.Left(re.len - offset);
        await(storage.Read, re.r.addr + offset, f.buf);
        async_return(f.buf.Length());
    }
    async_return(0);
}
async_end

async(JournalStorage::BeginWrite, RecordWriter& writer, size_t length)
async_def(
    JournalFormat::RecordInfo ri;
)
{
    for (;;)
    {
        if (freeOffset == 0 || freeOffset >= storage.SectorSize())
        {
            await(NewSector);
            ASSERT(freeOffset > 0 && freeOffset < storage.SectorSize());
        }

        intptr_t payloadOffset;
        payloadOffset = await(format.InitRecord, storage.RestOfSectorSpan(lastSector + freeOffset), f.ri, length);
        freeOffset += f.ri.nextRecord;
        maxRecord = std::max(0, int(storage.SectorSize() - freeOffset - payloadOffset));
        if (f.ri.IsValid())
        {
            writer.Init(storage.GetSpan(lastSector + freeOffset - f.ri.nextRecord + payloadOffset, f.ri.payload));
            async_return(true);
        }

        if (!(f.ri.IsBad() && f.ri.nextRecord))
        {
            // cannot try next record, have to move to next sector
            freeOffset = storage.SectorSize();
        }
    }
}
async_end

async(JournalStorage::AdvanceSector)
async_def(
    uint32_t addr;
    JournalFormat::SectorInfo si;
)
{
    lastSector = NextSector(lastSector);
    freeOffset = 0;
    MYTRACE(1, "Advancing to sector %X", lastSector);

    if (lastSector != firstSector)
    {
        async_return(true);
    }

    // first sector is about to be overwritten
    for (f.addr = NextSector(firstSector); f.addr != lastSector; f.addr = NextSector(f.addr))
    {
        await(format.ScanSector, storage.SectorSpan(f.addr), f.si);
        async_yield();
        if (f.si.IsValid())
        {
            firstSector = f.addr;
            MYDBG("Moved first sector to %X - %d, it is going to be overwritten", f.addr, f.si.sequence);
            async_return(true);
        }
    }

    // if we didn't find a new firstSector, just keep firstSector == lastSector
    MYTRACE(1, "No valid first sector, keeping at %X", firstSector);
}
async_end

async(JournalStorage::NewSector)
async_def()
{
    if (freeOffset)
    {
        await(AdvanceSector);
    }

    for (;;)
    {
        if (!await(storage.IsEmpty, lastSector, storage.SectorSize()))
        {
            MYTRACE(1, "Erasing sector @ %X", lastSector);
            await(storage.Erase, lastSector, storage.SectorSize());
        }

        await(format.InitSector, storage.SectorSpan(lastSector), last);
        if (!last.IsValid())
        {
            MYDBG("failed to initialize sector %X", lastSector);
            await(AdvanceSector);
        }
        else
        {
            freeOffset = last.firstRecord;
            MYTRACE(1, "Successfully initialized new sector @ %X - %d", lastSector, last.sequence);
            async_return(true);
        }
    }
}
async_end

async(JournalStorage::Write, Span data)
async_def(
    RecordWriter rw;
)
{
    if (!await(BeginWrite, f.rw, data.Length()))
    {
        async_return(false);
    }

    await(f.rw.Write, 0, data);
    await(EndWrite, f.rw);
    async_return(true);
}
async_end


async(JournalStorage::CloseSector)
async_def()
{
    if (freeOffset)
    {
        await(AdvanceSector);
    }
}
async_end

}
