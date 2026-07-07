<!-- SPDX-License-Identifier: MIT -->

# Virtual Mailbox Register Reference

This is the canonical register reference for the virtual mailbox hardware
contract. QEMU, the Linux `vmbox` driver, QTest, and userspace tests should stay
aligned with this table.

MMIO access requirements:

- Region size: 0x1000 bytes
- Access size: 32-bit only
- Register alignment: 32-bit aligned offsets only
- Linux accessors: `readl()` and `writel()`
- Invalid reads: log a QEMU guest error and return zero
- Invalid writes: log a QEMU guest error and ignore the write

## Register Table

| Offset | Register | Access | Reset | Bits | Description | Side Effects |
|---:|---|---|---:|---|---|---|
| 0x00 | ID | R | 0x514d424f | 31:0 device ID | Stable mailbox identifier | None |
| 0x04 | VERSION | R | 0x00010000 | 31:16 major, 15:0 minor | Hardware interface version | None |
| 0x08 | CONTROL | R/W | 0x00000000 | bit 0 ENABLE, bit 1 RESET, bit 2 LOOPBACK, bit 3 IRQ_ENABLE | Device control bits | RESET is self-clearing and resets device state |
| 0x0c | STATUS | R | 0x00000000 | bit 0 BUSY, bit 1 RX_READY, bit 2 TX_FULL, bit 3 RX_FULL, bit 4 ERROR | Device status | Derived from internal state |
| 0x10 | TX_DATA | W | 0x00000000 | 7:0 TX byte | Push one byte into TX FIFO | May update TX_COUNT, STATUS, IRQ_STATUS, and schedule processing |
| 0x14 | RX_DATA | R | 0x00000000 | 7:0 RX byte | Pop one byte from RX path | Clears RX_READY when RX becomes empty; may resume processing if TX is pending |
| 0x18 | IRQ_STATUS | W1C | 0x00000000 | bit 0 RX_READY, bit 1 TX_SPACE, bit 2 ERROR, bit 3 DONE | Sticky pending interrupt bits | Write one to clear pending bits and update IRQ line |
| 0x1c | IRQ_ENABLE | R/W | 0x00000000 | bits match IRQ_STATUS | Per-source interrupt enable mask | May change IRQ line assertion |
| 0x20 | TX_COUNT | R | 0x00000000 | 7:0 count | TX FIFO occupancy | None |
| 0x24 | RX_COUNT | R | 0x00000000 | 7:0 count | RX FIFO occupancy | None |
| 0x28 | FIFO_DEPTH | R | 0x00000010 | 31:0 depth | FIFO depth in bytes | None |
| 0x2c | RESET | W | 0x00000000 | bit 0 reset command | Soft reset command alias | Nonzero write resets device state |

## CONTROL Bits

| Bit | Name | Reset | Description |
|---:|---|---:|---|
| 0 | ENABLE | 0 | Enables normal device processing |
| 1 | RESET | 0 | Self-clearing reset request |
| 2 | LOOPBACK | 0 | Planned self-test mode; TX bytes are echoed to RX |
| 3 | IRQ_ENABLE | 0 | Global interrupt enable |

`LOOPBACK` is planned to decouple stress tests from timer timing. It can be
implemented after the baseline FIFO and IRQ behavior are stable.

## STATUS Bits

| Bit | Name | Reset | Description |
|---:|---|---:|---|
| 0 | BUSY | 0 | Timer-backed processing is active |
| 1 | RX_READY | 0 | RX FIFO contains at least one byte |
| 2 | TX_FULL | 0 | TX FIFO occupancy equals FIFO depth |
| 3 | RX_FULL | 0 | RX FIFO occupancy equals FIFO depth |
| 4 | ERROR | 0 | Device detected a protocol or FIFO error |

## IRQ Bits

| Bit | Name | Reset | Description |
|---:|---|---:|---|
| 0 | RX_READY | 0 | RX data became available |
| 1 | TX_SPACE | 0 | TX FIFO space became available |
| 2 | ERROR | 0 | Device error occurred |
| 3 | DONE | 0 | Processing completed |

`IRQ_STATUS` bits are sticky until software clears them by writing one to the
corresponding bit. The IRQ line is asserted only when `CONTROL.IRQ_ENABLE` is
set and at least one pending `IRQ_STATUS` bit is also enabled in `IRQ_ENABLE`.

Current event sources:

- `RX_READY`: set when timer processing pushes a byte into RX.
- `TX_SPACE`: set when processing frees space from a previously full TX FIFO.
- `ERROR`: set when a TX_DATA write is rejected because TX is full.
- `DONE`: set when timer processing drains the last pending TX byte.

## Processing Note

The device uses real TX/RX FIFO state and timer-backed processing. TX_DATA
writes are pushed into TX, STATUS.BUSY is set when processing is scheduled, and
one TX byte is transformed into RX per timer tick while RX has free space.
