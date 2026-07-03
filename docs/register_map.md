# qemu_mbox Register Map

## MMIO region

- Base address: assigned by QEMU machine/device tree
- Region size: 0x1000
- Access size: 32-bit only
- Endian: native QEMU device endian
- FIFO depth: 16 bytes

All registers are 32-bit aligned. Unaligned accesses or accesses with a size
other than 32 bits are invalid. The QEMU model should log invalid guest access
and ignore invalid writes. Invalid reads should return zero.

## Registers

| Offset | Register | Access | Description |
|---:|---|---|---|
| 0x00 | ID | RO | Device ID, expected 0x514d424f |
| 0x04 | VERSION | RO | Hardware version, expected 0x00010000 |
| 0x08 | CONTROL | RW | Enable, reset, loopback, IRQ enable |
| 0x0c | STATUS | RO | Busy, RX ready, TX full, RX full, error |
| 0x10 | TX_DATA | WO | Write data into device |
| 0x14 | RX_DATA | RO | Read processed data from device |
| 0x18 | IRQ_STATUS | RW1C | Interrupt status bits |
| 0x1c | IRQ_ENABLE | RW | Interrupt enable mask |
| 0x20 | TX_COUNT | RO | TX FIFO occupancy |
| 0x24 | RX_COUNT | RO | RX FIFO occupancy |
| 0x28 | FIFO_DEPTH | RO | FIFO depth |
| 0x2c | RESET | WO | Soft reset command |

Reserved offsets must read as zero and ignore writes. QEMU should log guest
errors for reserved register accesses during development.

## Device ID

    #define QEMU_MBOX_ID_VALUE 0x514d424fU

ASCII interpretation:

    Q M B O

## Version

    #define QEMU_MBOX_VERSION_VALUE 0x00010000U

Interpretation:

    major = 1
    minor = 0

## CONTROL bits

| Bit | Name | Description |
|---:|---|---|
| 0 | ENABLE | Enable device |
| 1 | RESET | Self-clearing reset bit |
| 2 | LOOPBACK | Reserved for loopback/test mode |
| 3 | IRQ_ENABLE | Global IRQ enable |

Writes to unsupported CONTROL bits are ignored.

`CONTROL.RESET` is self-clearing. A write with the RESET bit set performs a
soft reset, then stores only the remaining valid non-reset control bits.

## STATUS bits

| Bit | Name | Description |
|---:|---|---|
| 0 | BUSY | Device is processing data |
| 1 | RX_READY | RX data is available |
| 2 | TX_FULL | TX FIFO is full |
| 3 | RX_FULL | RX FIFO is full |
| 4 | ERROR | Device error |

STATUS is read-only from the guest point of view. The device derives FIFO state
bits from internal state:

- `BUSY` is set while timer-backed processing is active.
- `RX_READY` is set when RX FIFO occupancy is greater than zero.
- `TX_FULL` is set when TX FIFO occupancy equals FIFO depth.
- `RX_FULL` is set when RX FIFO occupancy equals FIFO depth.
- `ERROR` is set when the device detects overflow or another fatal protocol
  condition.

## IRQ bits

| Bit | Name | Description |
|---:|---|---|
| 0 | RX_READY | RX data became available |
| 1 | TX_SPACE | TX FIFO space became available |
| 2 | ERROR | Error occurred |
| 3 | DONE | Processing completed |

`IRQ_STATUS` is write-one-to-clear. Writing zero has no effect. Writing one to a
set bit clears that bit. Writes to unsupported bits are ignored.

An interrupt line may be asserted only when all of these are true:

- `CONTROL.IRQ_ENABLE` is set.
- The corresponding bit is enabled in `IRQ_ENABLE`.
- The corresponding bit is pending in `IRQ_STATUS`.

The interrupt line should deassert when no enabled pending IRQ bits remain.

## Reset state

After power-on reset or soft reset:

| Field | Value |
|---|---:|
| CONTROL | 0 |
| STATUS | 0 |
| IRQ_STATUS | 0 |
| IRQ_ENABLE | 0 |
| TX_COUNT | 0 |
| RX_COUNT | 0 |
| FIFO_DEPTH | 16 |

Soft reset clears TX FIFO, RX FIFO, processing state, IRQ pending state, and
software-visible temporary data registers. ID and VERSION do not change.

## FIFO contract

Final FIFO behavior:

- TX FIFO depth is 16 bytes.
- RX FIFO depth is 16 bytes.
- TX_DATA accepts the low 8 bits of each 32-bit write.
- RX_DATA returns one byte in the low 8 bits of the 32-bit read.
- TX_DATA writes when TX FIFO is full are rejected and set `STATUS.ERROR`.
- RX_DATA reads when RX FIFO is empty return zero.
- Reading RX_DATA pops one byte from the RX FIFO.
- `TX_COUNT` and `RX_COUNT` report current occupancy.

The initial processing rule is intentionally simple and deterministic:

```text
'a' - 'z' -> 'A' - 'Z'
other bytes unchanged
```

This rule exists to make end-to-end tests easy to inspect. It can later be
replaced by a richer simulated workload without changing the driver API.

## Current Milestone 2 behavior

Milestone 2 does not implement real FIFO behavior yet.

Temporary behavior:

- TX_DATA stores one byte.
- The byte is immediately processed.
- RX_DATA returns the processed byte.
- Reading RX_DATA clears STATUS.RX_READY.
- Reading RX_DATA while STATUS.RX_READY is clear returns zero.
- TX_COUNT returns 1 if a byte has been written.
- RX_COUNT returns 1 if STATUS.RX_READY is set.
- FIFO_DEPTH always returns 16.

Real FIFO behavior is added in Milestone 3.
