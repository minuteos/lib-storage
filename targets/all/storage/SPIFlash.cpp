/*
 * Copyright (c) 2020 triaxis s.r.o.
 * Licensed under the MIT license. See LICENSE.txt file in the repository root
 * for full license information.
 *
 * storage/SPIFlash.cpp
 */

#include "SPIFlash.h"

#include <base/ID.h>

#define MYDBG(...)  DBGCL("SPIFlash", __VA_ARGS__)

//#define SPI_FLASH_DIAG    1

#if SPI_FLASH_DIAG
#define MYDIAG(...)	MYDBG(__VA_ARGS__)
#else
#define MYDIAG(...)
#endif

namespace storage
{

async(SPIFlash::Init)
async_def(
    int i;
    uint32_t jedecAddr, jedecSize;
    union
    {
        SFDPHeader sfdpHeader;
        SFDPTable sfdpTable;
        SFDPJEDEC sfdp;
    };
)
{
    init = false;

    await(ReadSFDP, 0, f.sfdpHeader);
    if (f.sfdpHeader.sig != ID("SFDP"))
    {
        MYDBG("bad SFDP signature: %H", Span(f.sfdpHeader.sig));
        async_return(false);
    }

    MYDBG("SFDP header v%d.%d, %d tables", f.sfdpHeader.maj, f.sfdpHeader.min, f.sfdpHeader.cnt + 1);

    for (f.i = f.sfdpHeader.cnt; f.i >= 0; f.i--)
    {
        await(ReadSFDP, sizeof(SFDPHeader) + f.i * sizeof(SFDPTable), f.sfdpTable);
        MYDBG("SFDP table %d: ID %02X v%d.%d, %d words @ %X", f.i, f.sfdpTable.id, f.sfdpTable.maj, f.sfdpTable.min, f.sfdpTable.words, f.sfdpTable.addr);

        if (f.sfdpTable.id == 0)
            break;
    }

    if (f.sfdpTable.id)
    {
        MYDBG("SFDP JEDEC table not found");
        async_return(false);
    }

    f.jedecAddr = f.sfdpTable.addr;
    f.jedecSize = f.sfdpTable.words << 2;
    if (f.jedecSize > sizeof(SFDPJEDEC))
    {
        f.jedecSize = sizeof(SFDPJEDEC);
    }
    else if (f.jedecSize < sizeof(SFDPJEDEC))
    {
        memset((char*)&f.sfdp + f.jedecSize, 0, sizeof(SFDPJEDEC) - f.jedecSize);
    }
    await(ReadSFDP, f.jedecAddr, Buffer(&f.sfdp, f.jedecSize));

    size = (f.sfdp.density + 1) / 8;
    sectorTypeCount = 0;
    MYDBG("%d MB FLASH detected", size / 1024 / 1024);

    for (auto& sec: f.sfdp.sec)
    {
        if (sec.bits)
            AddSectorType(sec);
    }

    if (!f.sfdp.noErase4k)
    {
        AddSectorType({ 12, f.sfdp.opErase4k });
    }

    for (unsigned i = 0; i < sectorTypeCount; i++)
    {
        MYDBG("%d KB ERASE OP = %02X", (1 << sector[i].bits) / 1024, sector[i].op);
    }

    init = true;

    // make sure the device is not completing some previous operation
    deviceBusy = true;
    await(Sync);
    async_return(true);
}
async_end

void SPIFlash::AddSectorType(SectorType st)
{
    unsigned pos = 0;
    while (pos < sectorTypeCount && sector[pos].bits < st.bits)
        pos++;

    if (pos < sectorTypeCount && sector[pos].bits == st.bits)
    {
        if (sector[pos].op != st.op)
        {
            MYDBG("multiple erase opcodes for sector size %d, using %02X", 1 << st.bits, sector[pos].bits);
        }
        return;
    }

    if (pos < countof(sector))
    {
        for (unsigned i = countof(sector) - 1; i > pos; i--)
            sector[i] = sector[i - 1];
        sector[pos] = st;
        sectorTypeCount++;
    }
}

async(SPIFlash::ReadSFDP, uint32_t addr, Buffer buffer)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
        uint8_t dummy;
    } req;
    bus::SPI::Descriptor tx[2];
)
{
    await(spi.Acquire, cs);
    f.req = { OP_READ_SFDP, TO_BE24(addr) };
    f.tx[0].Transmit(f.req);
    f.tx[1].Receive(buffer);
    await(spi.Transfer, f.tx);
    spi.Release();
}
async_end

async(SPIFlash::Read, uint32_t addr, Buffer buffer)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    bus::SPI::Descriptor tx[2];
)
{
    if (deviceBusy)
    {
        await(Sync);
    }

    await(spi.Acquire, cs);
    f.req = { OP_READ, TO_BE24(addr) };
    f.tx[0].Transmit(f.req);
    f.tx[1].Receive(buffer);
    await(spi.Transfer, f.tx);
    spi.Release();

    MYDIAG("Read %d bytes @ %X : %H", data.Length(), addr, data);
}
async_end

async(SPIFlash::Write, uint32_t addr, Span data)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    bus::SPI::Descriptor tx[2];
)
{
    if (deviceBusy)
    {
        await(Sync);
    }

    await(spi.Acquire, cs);
    f.req.op = OP_WREN;
    f.tx[0].Transmit(f.req.op);
    await(spi.Transfer, f.tx[0]);

    f.req = { OP_PROGRAM, TO_BE24(addr) };
    f.tx[0].Transmit(f.req);
    f.tx[1].Transmit(data);
    await(spi.Transfer, f.tx);
    spi.Release();

    deviceBusy = true;

    MYDIAG("Written %d bytes @ %X : %H", data.Length(), addr, data);
}
async_end

async(SPIFlash::Erase, uint32_t addr, uint32_t len)
async_def(
    uint32_t start, end;
)
{
    uint32_t mask = MASK(SectorSizeBits());

    // calculate start/end sector boundaries
    f.start = addr & ~mask;
    f.end = (addr + len + mask) & ~mask;

    while (f.start < f.end)
    {
        uint32_t next = await(EraseFirst, f.start, f.end - f.start);
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

async(SPIFlash::EraseFirst, uint32_t addr, uint32_t len)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    uint8_t wren;
    uint32_t end;
    bus::SPI::Descriptor tx;
)
{
    uint32_t mask = MASK(SectorSizeBits());

    // calculate start/end sector boundaries
    uint32_t start = addr & ~mask;
    uint32_t end = (addr + len + mask) & ~mask;

    // find the largest eraseable size
    for (int i = sectorTypeCount - 1; i >= 0; i--)
    {
        if ((start & MASK(SectorSizeBits(i))) == 0 && (start + SectorSize(i)) <= end)
        {
            MYDBG("erasing %d KB block starting at %X", SectorSize(i) / 1024, start);

            f.req = { sector[i].op, TO_BE24(start) };
            f.end = start + SectorSize(i);

            if (deviceBusy)
            {
                await(Sync);
            }

            await(spi.Acquire, cs);
            f.wren = OP_WREN;
            f.tx.Transmit(f.wren);
            await(spi.Transfer, f.tx);

            f.tx.Transmit(f.req);
            await(spi.Transfer, f.tx);

            deviceBusy = true;
            async_return(f.end);
        }
    }

    MYDBG("invalid erase range %X-%X", start, end);
    async_return(addr);
}
async_end

async(SPIFlash::MassErase)
async_def(
    uint8_t op;
    bus::SPI::Descriptor tx;
)
{
    MYDBG("Starting mass erase");

    if (deviceBusy)
    {
        await(Sync);
    }

    await(spi.Acquire, cs);
    f.op = OP_WREN;
    f.tx.Transmit(f.op);
    await(spi.Transfer, f.tx);
    f.op = OP_CHIP_ERASE;
    await(spi.Transfer, f.tx);
    spi.Release();

    deviceBusy = true;
    await(Sync);

    MYDBG("Mass erase complete");
}
async_end

async(SPIFlash::Sync)
async_def(
    unsigned attempt;
    uint8_t op;
    uint8_t status;
    bus::SPI::Descriptor tx[2];
)
{
    if (!deviceBusy)
    {
        async_return(true);
    }

    f.op = OP_STATUS;
    f.tx[0].Transmit(f.op);
    f.tx[1].Receive(f.status);

    for (f.attempt = 0; ; f.attempt++)
    {
        await(spi.Acquire, cs);
        await(spi.Transfer, f.tx);
        spi.Release();

        if (!GETBIT(f.status, 0))
        {
            if (f.attempt)
                MYDIAG("status polled %d times before operation completed", f.attempt)
            deviceBusy = false;
            break;
        }
        else
        {
            // let other tasks do their work
            async_yield();
        }
    }
}
async_end

}
