/*
 * Copyright (c) 2021 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/JournalStorage.h
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/ByteStorage.h>
#include <storage/JournalFormat.h>

#define STORAGE_JOURNAL_TRACE 1

namespace storage
{

//! Represents a simple ring journal implemented on top of @ref ByteStorage
class JournalStorage
{
public:
    JournalStorage(ByteStorage& storage, JournalFormat& format)
        : storage(storage), format(format) {}

#if TRACE
    virtual const char* DebugComponent() const { return "JournalStorage"; }
    void _DebugHeader() const { DBG("%s: ", DebugComponent()); }
    template<typename... Args> void MYDBG(Args... args) const { _DebugHeader(); _DBG(args...); _DBGCHAR('\n'); }
#else
    template<typename... Args> void MYDBG(Args...) const {}
#endif

#if TRACE && STORAGE_JOURNAL_TRACE
    template<typename... Args> void MYTRACE(int level, Args... args) const { if (level > STORAGE_JOURNAL_TRACE) { return; } _DebugHeader(); _DBG(args...); _DBGCHAR('\n'); }
#else
    template<typename... Args> void MYTRACE(int level, Args...) const {}
#endif

    //! Sector address
    class Sector
    {
    public:
        Sector() {}

        constexpr bool IsValid() const { return addr != ~0u; }
        constexpr operator bool() const { return IsValid(); }

    private:
        Sector(uint32_t addr) : addr(addr) {}

        uint32_t addr;

        friend class JournalStorage;
    };

    class SectorEnumerator
    {
    public:
        SectorEnumerator() : s(~0u) {}

        operator Sector() const { return s; }

        constexpr bool IsValid() const { return s.IsValid(); }
        constexpr operator bool() const { return IsValid(); }

    private:
        Sector s;

        friend class JournalStorage;
    };

    //! Record address
    class Record
    {
        Record(uint32_t addr = 0)
            : addr(addr) {}

        uint32_t addr;

        friend class JournalStorage;
    };

    class RecordEnumerator
    {
    public:
        RecordEnumerator() {}

        constexpr bool IsEmpty() const { return r.addr == rNext.addr; }
        constexpr uint32_t Address() const { return r.addr; }
        constexpr uint32_t Length() const { return len; }

    private:
        RecordEnumerator(const Sector& s)
            : r(s.addr), rNext(s.addr), si({}) {}

        Record r;
        Record rNext;
        uint32_t len;
        JournalFormat::SectorInfo si;

        friend class JournalStorage;
    };

    class RecordWriter : public ByteStorageSpan
    {
    private:
        void Init(const ByteStorageSpan& other)
        {
            *(ByteStorageSpan*)this = other;
        }

        friend class JournalStorage;
    };

    //! Scans the journal storage, determining the first, last, and next valid record
    async(Scan);
    //! Begins writing a new record allocating a span of the requested length
    async(BeginWrite, RecordWriter& writer, size_t length);
    //! Finishes writing a record, marking it as valid
    async(EndWrite, RecordWriter& writer) { return async_forward(format.CommitRecord, writer); }
    //! Writes a new record to the journal
    async(Write, Span record);
    //! Gets the maximum record size in the current sector
    size_t MaximumRecord() const { return maxRecord; }
    //! Closes the current sector and starts writing a new one
    async(CloseSector);

    //! Enumerates all sectors with records
    void EnumerateSectors(SectorEnumerator& e) { e = SectorEnumerator(); }
    //! Moves the enumerator to the next valid sector
    async(NextSector, SectorEnumerator& e);
    //! Moves the enumerator to the previous valid sector
    async(PreviousSector, SectorEnumerator& e);
    //! Reads part of the sector header from the specified sector enumerator
    async(ReadSectorHeader, const SectorEnumerator& e, const Buffer& buf, size_t offset = 0);

    //! Enumerates the records in the specified sector
    void EnumerateRecords(RecordEnumerator& e, Sector sector) { e = RecordEnumerator(sector); }
    //! Moves the enumerator to the next valid record
    async(NextRecord, RecordEnumerator& e);
    //! Reads part of the current record from the specified enumerator
    async(ReadRecord, const RecordEnumerator& e, const Buffer& buf, size_t offset = 0);

    ByteStorage& storage;
    JournalFormat& format;

    //! Returns the last written sector information
    const uint32_t LastSectorAddress() const { return lastSector; }
    //! Returns the last written sector information
    const JournalFormat::SectorInfo& LastSector() const { return last; }

private:
    JournalFormat::SectorInfo last = {};
    uint32_t firstSector = 0, lastSector = 0;
    uint32_t freeOffset = 0, maxRecord = 0;

    //! Advances lastSector to a new sector, adjusting firstSector as necessary
    async(AdvanceSector);
    //! Allocates a new sector
    async(NewSector);
    //! Gets the address of the previous sector in a ring
    uint32_t PreviousSector(uint32_t addr) const { return nonzero(addr, storage.Size()) - storage.SectorSize(); }
    //! Gets the address of the next sector in a ring
    uint32_t NextSector(uint32_t addr) const { addr += storage.SectorSize(); return addr == storage.Size() ? 0 : addr; }
};

}
