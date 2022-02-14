/*
 * Copyright (c) 2022 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/tests/journal/JournalStorage.cpp
 */

#include <testrunner/TestCase.h>

#include <base/ID.h>

#include <storage/JournalStorage.h>
#include <storage/SimpleVariableJournalFormat.h>
#include <storage/TestByteStorage.h>

using namespace storage;

namespace
{

TEST_CASE("01 Simple Writes")
async_test : JournalStorage
{
    TestByteStorage store;
    SimpleVariableJournalFormat format;

    async_test_init(JournalStorage(store, format), store(8192), format(ID("TEST")));

    SectorEnumerator se;
    RecordEnumerator re;

    int i;
    int rec;

    async(Run)
    async_def()
    {
        await(Scan);

        for (i = 0; i < 500; i++)
        {
            await(Write, i);
        }

        i = 0;

        EnumerateSectors(se);
        while (await(NextSector, se))
        {
            EnumerateRecords(re, se);
            while (await(NextRecord, re, rec))
            {
                AssertEqual(i, rec);
                i++;
            }
        }

        AssertEqual(i, 500);
    }
    async_end
}
async_test_end

TEST_CASE("02 Variable Writes")
async_test : JournalStorage
{
    TestByteStorage store;
    SimpleVariableJournalFormat format;

    async_test_init(JournalStorage(store, format), store(8192), format(ID("TEST")));

    SectorEnumerator se;
    RecordEnumerator re;
    RecordWriter rw;

    int i;
    int rec;

    async(Run)
    async_def()
    {
        await(Scan);

        for (i = 0; i < 119; i++)
        {
            await(BeginWrite, rw, sizeof(i) + i);
            await(rw.Write, 0, i);
            await(EndWrite, rw);
        }

        i = 0;

        EnumerateSectors(se);
        while (await(NextSector, se))
        {
            EnumerateRecords(re, se);
            while (await(NextRecord, re, rec))
            {
                AssertEqual(i, rec);
                i++;
            }
        }

        AssertEqual(i, 119);
    }
    async_end
}
async_test_end

TEST_CASE("03 Bad Writes")
async_test : JournalStorage
{
    TestByteStorage store;
    SimpleVariableJournalFormat format;

    async_test_init(JournalStorage(store, format), store(8192), format(ID("TEST")));

    SectorEnumerator se;
    RecordEnumerator re;
    RecordWriter rw;

    int i;
    int rec;

    async(Run)
    async_def()
    {
        await(Scan);

        for (i = 0; i < 119; i++)
        {
            await(BeginWrite, rw, sizeof(i) + i);
            await(rw.Write, 0, i);
            if (i & 1)   // complete every other write
            {
                await(EndWrite, rw);
            }
        }

        i = 1;

        EnumerateSectors(se);
        while (await(NextSector, se))
        {
            EnumerateRecords(re, se);
            while (await(NextRecord, re, rec))
            {
                AssertEqual(i, rec);
                i += 2;
            }
        }

        AssertEqual(i, 119);
    }
    async_end
}
async_test_end

TEST_CASE("03 Oversize Writes")
async_test : JournalStorage
{
    TestByteStorage store;
    SimpleVariableJournalFormat format;

    async_test_init(JournalStorage(store, format), store(8192), format(ID("TEST")));

    SectorEnumerator se;
    RecordEnumerator re;
    RecordWriter rw;

    int i;
    int rec;
    int numSectors;

    async(Run)
    async_def()
    {
        await(Scan);

        // oversize writes
        numSectors = storage.Size() / storage.SectorSize();

        for (i = 0; i < numSectors * 2; i++)
        {
            await(BeginWrite, rw, storage.SectorSize());
            AssertLessThan(rw.Size(), storage.SectorSize());
            await(rw.Write, 0, i);
            await(EndWrite, rw);
        }

        i = numSectors;

        EnumerateSectors(se);
        while (await(NextSector, se))
        {
            EnumerateRecords(re, se);
            while (await(NextRecord, re, rec))
            {
                AssertEqual(i, rec);
                i++;
            }
        }

        AssertEqual(i, numSectors * 2);
    }
    async_end
}
async_test_end

}
