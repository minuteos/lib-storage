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

#define DIAG_READ   1
#define DIAG_WRITE  2
#define DIAG_WAIT   4

//#define SPI_FLASH_DIAG    DIAG_WRITE

#if SPI_FLASH_DIAG
#define MYDIAG(mask, ...)	if ((SPI_FLASH_DIAG) & (mask)) { MYDBG(__VA_ARGS__); }
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

    if (size == 0)
    {
        MYDBG("Density missing in SFDP, using RDID");
        int id = await(ReadID);
        MYDBG("RDID: mfg = %02X, type = %02X, capacity = %02X",
            uint8_t(id), uint8_t(id >> 8), uint8_t(id >> 16));
        size = 1 << uint8_t(id >> 16);
    }

    MYDBG("%d MB FLASH detected", size / 1024 / 1024);

    init = true;

    // make sure the device is not completing some previous operation
    deviceBusy = true;
    await(SyncAndAcquire);
    spi.Release();

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

async(SPIFlash::ReadSFDP, uint32_t addr, char* buffer, size_t length)
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
    f.tx[1].Receive(Buffer(buffer, length));
    await(spi.Transfer, f.tx);
    spi.Release();
}
async_end

async(SPIFlash::ReadID)
async_def(
    uint8_t op;
    uint8_t id[3];
    bus::SPI::Descriptor tx[2];
)
{
    await(spi.Acquire, cs);
    f.op = OP_RDID;
    f.tx[0].Transmit(f.op);
    f.tx[1].Receive(f.id);
    await(spi.Transfer, f.tx);
    spi.Release();
    async_return(f.id[0] | f.id[1] << 8 | f.id[2] << 16);
}
async_end

async(SPIFlash::ReadImpl, uint32_t addr, char* buffer, size_t length)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    bus::SPI::Descriptor tx[2];
)
{
    await(SyncAndAcquire);
    f.req = { OP_READ, TO_BE24(addr) };
    f.tx[0].Transmit(f.req);
    f.tx[1].Receive(Buffer(buffer, length));
    await(spi.Transfer, f.tx);
    spi.Release();

    MYDIAG(DIAG_READ, "%X==%H", addr, Span(buffer, length));
}
async_end

async(SPIFlash::ReadToPipe, io::PipeWriter pipe, uint32_t addr, size_t length, Timeout timeout)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    bus::SPI::Descriptor tx[2];
    size_t read;
    Timeout timeout;
)
{
    f.timeout = timeout.MakeAbsolute();
    f.req.op = OP_READ;
    f.tx[0].Transmit(f.req);

    while (f.read < length)
    {
        if (!pipe.Available() && !await(pipe.Allocate, length - f.read, f.timeout))
        {
            break;
        }

        auto buf = pipe.GetBuffer();
        f.tx[1].Receive(buf.Left(spi.MaximumTransferSize()).Left(length - f.read));
        f.req.addrBE = TO_BE24(addr + f.read);

        await(spi.Acquire, cs);
        await(spi.Transfer, f.tx);
        spi.Release();

        pipe.Advance(f.tx[1].Length());
        f.read += f.tx[1].Length();

        if (f.timeout.Elapsed())
        {
            break;
        }
    }

    async_return(f.read);
}
async_end

async(SPIFlash::WriteImpl, uint32_t addr, const char* data, size_t length)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    size_t written, len;
    bus::SPI::Descriptor tx[2];
)
{
    while (f.written < length)
    {
        await(SyncAndAcquire);

        f.len = std::min(PageRemaining(addr + f.written), length - f.written);
        MYDIAG(DIAG_WRITE, "%X=%H", addr + f.written, Span(data + f.written, f.len));

        f.req.op = OP_WREN;
        f.tx[0].Transmit(f.req.op);
        await(spi.Transfer, f.tx[0]);

        f.req = { OP_PROGRAM, TO_BE24(addr + f.written) };
        f.tx[0].Transmit(f.req);
        f.tx[1].Transmit(Span(data + f.written, f.len));
        await(spi.Transfer, f.tx);

        deviceBusy = true;
        spi.Release();

        f.written += f.len;
    }
}
async_end

async(SPIFlash::Fill, uint32_t addr, uint8_t value, size_t length)
async_def(
    uint8_t value;
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    size_t written, len;
    bus::SPI::Descriptor tx[2];
)
{
    f.value = value;
    while (f.written < length)
    {
        await(SyncAndAcquire);

        f.len = std::min(PageRemaining(addr + f.written), length - f.written);
        MYDIAG(DIAG_WRITE, "%X=%d*%02X", addr + f.written, f.len, f.value);

        f.req.op = OP_WREN;
        f.tx[0].Transmit(f.req.op);
        await(spi.Transfer, f.tx[0]);

        f.req = { OP_PROGRAM, TO_BE24(addr + f.written) };
        f.tx[0].Transmit(f.req);
        f.tx[1].TransmitSame(&f.value, f.len);
        await(spi.Transfer, f.tx);

        deviceBusy = true;
        spi.Release();

        f.written += f.len;
    }
}
async_end

async(SPIFlash::IsEmpty, uint32_t addr, size_t length)
async_def(
    PACKED_UNALIGNED_STRUCT
    {
        uint8_t op;
        uint32_t addrBE : 24;
    } req;
    uint32_t buf[4];
    size_t checked;
    bus::SPI::Descriptor tx[2];
)
{
    if (!length)
    {
        async_return(true);
    }

    await(SyncAndAcquire);
    f.tx[0].Transmit(f.req);
    f.req.op = OP_READ;
    Buffer(f.buf).Fill(0xFF);

    while (f.checked < length)
    {
        f.req.addrBE = TO_BE24(addr + f.checked);
        f.tx[1].Receive(Buffer(f.buf, std::min(sizeof(f.buf), length - f.checked)));
        await(spi.Transfer, f.tx);

        if (f.buf[0] != ~0u || f.buf[1] != ~0u || f.buf[2] != ~0u || f.buf[3] != ~0u)
        {
            MYDIAG(DIAG_READ, "%X!=empty: %H", addr + f.checked, Span(f.buf));
            spi.Release();
            async_return(false);
        }
        f.checked += sizeof(f.buf);
    }
    spi.Release();
    async_return(true);
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

            await(SyncAndAcquire);
            MYDIAG(DIAG_WRITE, "%X...", FROM_BE24(f.req.addrBE));

            f.wren = OP_WREN;
            f.tx.Transmit(f.wren);
            await(spi.Transfer, f.tx);

            f.tx.Transmit(f.req);
            await(spi.Transfer, f.tx);

            deviceBusy = true;
            spi.Release();

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

    await(SyncAndAcquire);

    f.op = OP_WREN;
    f.tx.Transmit(f.op);
    await(spi.Transfer, f.tx);
    f.op = OP_CHIP_ERASE;
    await(spi.Transfer, f.tx);

    deviceBusy = true;
    await(SyncAndAcquire);
    spi.Release();

    MYDBG("Mass erase complete");
}
async_end

async(SPIFlash::SyncAndAcquire)
async_def(
    unsigned attempt;
    uint8_t op;
    uint8_t status;
    bus::SPI::Descriptor tx[2];
)
{
    await(spi.Acquire, cs);

    if (!deviceBusy)
    {
        async_return(true);
    }

    f.op = OP_STATUS;
    f.tx[0].Transmit(f.op);
    f.tx[1].Receive(f.status);

    for (f.attempt = 0; ; f.attempt++)
    {
        await(spi.Transfer, f.tx);

        if (!GETBIT(f.status, 0))
        {
            if (f.attempt)
            {
                MYDIAG(DIAG_WAIT, "...%d", f.attempt);
            }
            deviceBusy = false;
            break;
        }
        else
        {
            // let other tasks do their work
            spi.Release();
            async_yield();
            await(spi.Acquire, cs);
        }
    }
}
async_end

}
