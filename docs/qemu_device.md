# QEMU Device Model

## Device name

The emulated hardware device is named:

    virt-mbox

The Linux-facing driver/device name will later be:

    vmbox

The eventual character device node will be:

    /dev/vmbox0

## Current QEMU Device Scope

The QEMU model implements FIFO-backed MMIO registers and timer-backed
processing.

Implemented:

- QOM/qdev device type
- SysBusDevice parent
- MMIO region
- 32-bit register read/write callbacks
- ID register
- VERSION register
- CONTROL register
- STATUS register
- TX_DATA register
- RX_DATA register
- real TX FIFO
- real RX FIFO
- TX_COUNT register
- RX_COUNT register
- FIFO_DEPTH register
- processing timer
- BUSY status bit
- IRQ_STATUS storage
- IRQ_ENABLE storage
- RESET register
- VMState migration fields for FIFO contents and indexes

Not implemented yet:

- interrupt line
- QOM debug properties
- Linux driver

## Device responsibilities

The device model owns the hardware-visible state. The Linux driver should not
need hidden knowledge beyond the documented register map.

Required device guarantees:

- ID and VERSION are stable across reset.
- RESET returns the device to a deterministic state.
- unsupported register bits are ignored.
- unsupported register accesses are logged as guest errors.
- writable IRQ status bits use write-one-to-clear semantics.
- FIFO state and STATUS bits remain internally consistent.

## FIFO and timer behavior

Writes to TX_DATA push one byte into the TX FIFO. If processing is possible,
the device schedules a QEMU virtual-clock timer and sets STATUS.BUSY. Each timer
tick processes one TX byte and pushes the result into RX if RX has space.

The current processing rule is:

    'a' - 'z' -> 'A' - 'Z'
    other bytes unchanged

Example:

    write TX_DATA = 'h'
    TX_COUNT      = 1
    STATUS.BUSY   = 1
    timer fires
    TX_COUNT      = 0
    RX_COUNT      = 1
    read RX_DATA  = 'H'

Reading RX_DATA again before another TX_DATA write returns zero because
STATUS.RX_READY has been cleared.

When RX is full, processing pauses and additional TX_DATA writes accumulate in
the TX FIFO. If TX is also full, additional TX_DATA writes are rejected and
STATUS.ERROR is set. Reading RX_DATA creates RX space and allows processing to
resume if TX still contains data.

## Final FIFO and timer model

The final IRQ-capable model should use the same separate TX and RX FIFOs with a
fixed depth of 16 bytes each.

Processing flow:

1. Guest writes a byte to TX_DATA.
2. Device pushes the byte into the TX FIFO.
3. Device schedules processing if it is idle.
4. Timer callback pops one TX byte.
5. Timer callback transforms the byte.
6. Device pushes the result into the RX FIFO.
7. Device updates STATUS and IRQ_STATUS.
8. Device asserts the IRQ line if enabled pending bits exist.

The timer is important because it makes blocking reads, blocking writes, and
`poll()` behavior meaningful in the Linux driver.

## Migration state

The device should preserve guest-visible hardware state across QEMU migration
or snapshot operations.

Final migration state should include:

- CONTROL
- STATUS-derived state
- IRQ_STATUS
- IRQ_ENABLE
- TX FIFO contents and indexes
- RX FIFO contents and indexes
- processing timer state where practical
- debug counters that are useful and stable enough to migrate

If a debug-only field is not migrated, the code should document why.

## Integration model

The files in this repository are project-owned source files.

They are intended to become a patch against a real QEMU source tree:

    qemu/hw/misc/qemu_mbox.c
    qemu/include/hw/misc/qemu_mbox.h
    qemu/patches/README.md
    qemu/patches/meson.build.fragment
    qemu/patches/Kconfig.fragment
    qemu/patches/aarch64-softmmu.default.mak.fragment

The first integration target is build-system wiring. Runtime instantiation will
follow after the minimal build path is documented and testable.

The source filenames still use `qemu_mbox` from the initial milestones. The
Linux-facing product name is `vmbox`, and the QEMU type string is planned as
`virt-mbox`.
