# QEMU Device Model

## Device name

The emulated hardware device is named:

    qemu-mbox

The Linux-facing driver/device name will later be:

    qemu_mbox

The eventual character device node will be:

    /dev/qemu_mbox0

## Milestone 2 scope

Milestone 2 implements the minimal QEMU MMIO device model.

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
- IRQ_STATUS storage
- IRQ_ENABLE storage
- RESET register

Not implemented yet:

- real TX FIFO
- real RX FIFO
- processing timer
- interrupt line
- QOM debug properties
- qtest coverage
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

## Temporary TX/RX behavior

Milestone 2 does not implement a real FIFO yet.

For smoke testing, writes to TX_DATA store one byte, immediately process it, and expose the result through RX_DATA.

The temporary processing rule is:

    'a' - 'z' -> 'A' - 'Z'
    other bytes unchanged

Example:

    write TX_DATA = 'h'
    read RX_DATA  = 'H'

Reading RX_DATA again before another TX_DATA write returns zero because
STATUS.RX_READY has been cleared.

This behavior will later move behind a real TX FIFO, RX FIFO, processing timer, and IRQ path.

## Final FIFO and timer model

The final model should use separate TX and RX FIFOs with a fixed depth of 16
bytes each.

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
    qemu/patches/0001-hw-misc-add-qemu-mbox-device.patch

Later milestones will add the Meson/Kconfig integration needed to compile the device inside QEMU.
