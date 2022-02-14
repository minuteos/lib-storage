/*
 * Copyright (c) 2021 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/JournalFormat.h
 */

#pragma once

#include <kernel/kernel.h>

#include <storage/ByteStorageSpan.h>

namespace storage
{

//! Represents the format used by @ref JournalStorage
class JournalFormat
{
protected:
    enum struct SectorState
    {
        Bad,
        Empty,
        Valid,
        ValidPreceding,
    };

    struct SectorInfo
    {
        uint32_t sequence;
        uint16_t firstRecord;
        uint8_t fixedRecordSize;
        SectorState state;

        constexpr bool IsBad() const { return state == SectorState::Bad; }
        constexpr bool IsEmpty() const { return state == SectorState::Empty; }
        constexpr bool IsValid() const { return state >= SectorState::Valid; }
        constexpr bool IsPreceding() const { return state == SectorState::ValidPreceding; }
    };

    enum struct RecordState
    {
        Bad,
        Empty,
        Valid,
    };

    struct RecordInfo
    {
        uint16_t payload;
        uint16_t nextRecord;
        RecordState state;

        constexpr bool IsBad() const { return state == RecordState::Bad; }
        constexpr bool IsEmpty() const { return state == RecordState::Empty; }
        constexpr bool IsValid() const { return state == RecordState::Valid; }

        constexpr size_t PayloadLength() const { return payload; }
        constexpr size_t NextRecordOffset() const { return nextRecord; }
    };

    //! Scans a sector, determining if it's valid/empty/bad
    /*!
     * Provided input:
     *  - @param sector is a ByteStorageSpan representing the entire sector
     *  - @param info is a preallocated SectorInfo structure expected to receive the sector scan results
     *  - @param following is an optional parameter ByteStorageSpan representing the entire sector
     * Expected output:
     *  - @param info.state is set to:
     *    - @ref SectorState::Empty if the sector is likely empty
     *    - @ref SectorState::ValidPreceding if @param following is specified and the sector is immediately preceding the other sector
     *    - @ref SectorState::Valid if the data in the sector is valid
     *    - @ref SectorState::Bad otherwise
     *  - if the sector is Valid(Preceding), the following fields must be initialized as well
     *    - @param info.firstRecord is the first record offset
     *    - @param info.fixedRecordSize is set to either 0, or the record size if the sector contains fixed-size records
     *    - @param info.sequence is the relative sector sequence number
     *  - @return value is ignored, @param info is used instead
     */
    virtual async(ScanSector, const ByteStorageSpan& sector, SectorInfo& info, const SectorInfo* following = NULL) const = 0;

    //! Scans a record, determining if it's valid/empty/bad
    /*!
     * Provided input:
     *  - @param sectorRemaining is a ByteStorageSpan representing the rest of the sector, starting at record position
     *  - @param sectorInfo must be the SectorInfo returned from a ScanSector operation on the sector
     *  - @param info is a preallocated RecordInfo structure expected to receive the sector scan results
     * Expected output:
     *  - @param info.state is set to:
     *    - @ref RecordState::Empty if the rest of the sector is empty
     *    - @ref RecordState::Bad if the record is corrupted
     *    - @ref RecordState::Valid otherwise
     *  - @param info.payload is set to the actual record payload length if the record is valid
     *  - @param info.nextRecord is set to the offset of the next record *from the start of @param sectorRemaining*
     *      must be set when state is Valid, can be set when record is Bad
     *      and it is possible to skip the bad record
     *  - @returns the offset of the payload *from the start of @param sectorRemaining*
     */
    virtual async(ScanRecord, const ByteStorageSpan& sectorRemaining, const SectorInfo& sectorInfo, RecordInfo& info) const = 0;

    //! Initializes a new sector
    /*!
     * Provided input:
     *  - @param sector is a pre-erased ByteStorageSpan representing the entire sector
     *  - @param info is a SectorInfo returned from the last ScanSector operation on the sector,
     *    and also expected to contain the new state of the sector
     * Expected output:
     *  - @param info.state is set to:
     *    - @ref SectorState::Bad if the method failed to initialize the sector
     *    - @ref SectorState::Valid if the sector is initialized successfully
     *  - if the sector is successfully initialized, the following fields must be initialized as well
     *    - @param info.firstRecord is the first record offset
     *    - @param info.fixedRecordSize is set to either 0, or the record size if the sector contains fixed-size records
     *    - @param info.sequence is the relative sector sequence number
     */
    virtual async(InitSector, const ByteStorageSpan& sector, SectorInfo& info) = 0;

    //! Allocates a new record
    /*!
     * Provided input:
     *  - @param sectorRemaining is a ByteStorageSpan representing the rest of the sector, starting at record position
     *  - @param info is a preallocated RecordInfo structure expected to receive the state of the new record
     *  - @param payload is the requested number of record payload bytes
     * Expected output:
     * Expected output:
     *  - @param info.state is set to
     *    - @ref RecordState::Bad if the record could not be allocated
     *    - @ref RecordState::Valid otherwise
     *  - @param info.payload is set to the requested record payload length
     *  - @param info.nextRecord is set to the offset of the next record *from the start of @param sectorRemaining*
     *      must be set when state is Valid, can be set when record is Bad
     *      and it is possible to skip the bad record
     *  - @returns the offset of the payload *from the start of @param sectorRemaining*
     */
    virtual async(InitRecord, const ByteStorageSpan& sectorRemaining, RecordInfo& info, size_t payload) = 0;

    //! Marks a previously allocated record as valid
    /*!
     * Provided input:
     *  - @param payload is a ByteStorageSpan representing the payload of the record,
     *    which is calculated from the offset returned by @ref InitRecord and the
     *    requested payload length
     */
    virtual async(CommitRecord, const ByteStorageSpan& payload) = 0;

private:
    friend class JournalStorage;
};

}
