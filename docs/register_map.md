# qemu_mbox Register Map

## MMIO region

Base address: assigned by QEMU machine/device tree
Region size:  0x1000
Access size:  32-bit
Endian:       native QEMU device endian
FIFO depth:   16 bytes

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

## STATUS bits

| Bit | Name | Description |
|---:|---|---|
| 0 | BUSY | Device is processing data |
| 1 | RX_READY | RX data is available |
| 2 | TX_FULL | TX FIFO is full |
| 3 | RX_FULL | RX FIFO is full |
| 4 | ERROR | Device error |

## IRQ bits

| Bit | Name | Description |
|---:|---|---|
| 0 | RX_READY | RX data became available |
| 1 | TX_SPACE | TX FIFO space became available |
| 2 | ERROR | Error occurred |
| 3 | DONE | Processing completed |

## Milestone 2 behavior

Milestone 2 does not implement real FIFO behavior yet.

Temporary behavior:

- TX_DATA stores one byte.
- The byte is immediately processed.
- RX_DATA returns the processed byte.
- Reading RX_DATA clears STATUS.RX_READY.
- TX_COUNT returns 1 if a byte has been written.
- RX_COUNT returns 1 if STATUS.RX_READY is set.
- FIFO_DEPTH always returns 16.

Real FIFO behavior is added in Milestone 3.
