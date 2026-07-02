# Embedded Linux Character Driver for a QEMU MMIO Device

This project builds an industry-style embedded Linux driver stack around a
custom QEMU-emulated memory-mapped mailbox device.

The goal is not only to expose a `/dev` node. The finished stack should define
a hardware/software contract, implement a QEMU MMIO device model, provide a
Linux platform character driver, support interrupt-driven blocking I/O, expose
observability hooks, and include tests at the QEMU and Linux userspace layers.

## Target Stack

```text
userspace tests and demo tools
        |
        | open(), read(), write(), poll(), ioctl()
        v
/dev/qemu_mbox0
        |
        | Linux platform character driver
        v
MMIO registers and IRQ
        |
        | readl(), writel()
        v
QEMU qemu-mbox SysBusDevice
```

## Current Status

Implemented:

- Repository skeleton and architecture documentation
- Register map contract for `qemu_mbox`, including reset, FIFO, IRQ, and
  invalid-access semantics
- Planned Linux driver API and ioctl UAPI structures
- Testing, bring-up, and demo plans
- Initial QEMU MMIO device source files
- ID, VERSION, CONTROL, STATUS, TX_DATA, RX_DATA, IRQ_STATUS, IRQ_ENABLE,
  TX_COUNT, RX_COUNT, FIFO_DEPTH, and RESET register definitions
- Temporary single-byte TX/RX behavior for early smoke testing
- Initial repository hygiene CI

Not implemented yet:

- QEMU Meson/Kconfig integration
- QTest coverage
- Real TX/RX FIFO behavior
- Processing timer and interrupt line
- Linux platform character driver
- `/dev/qemu_mbox0`
- userspace regression tests
- full build and boot CI

## Planned Milestones

1. Lock down architecture, register map, driver API, and testing docs.
2. Integrate the minimal QEMU MMIO device into a QEMU source tree.
3. Add QTest coverage for ID, VERSION, RESET, and basic register behavior.
4. Replace temporary TX/RX behavior with real 16-byte TX and RX FIFOs.
5. Add processing latency and IRQ generation.
6. Implement the Linux platform character driver probe and remove paths.
7. Add `/dev/qemu_mbox0` read, write, non-blocking mode, and poll support.
8. Add ioctl commands for reset, status, stats, and mode configuration.
9. Add debugfs observability and stress tests.
10. Expand CI to build QEMU, build the kernel module, run QTest, and run
    Linux-side tests.

Step 0, the project contract and planning milestone, is complete. The next
engineering milestone is QEMU build integration for the minimal MMIO device.

## Documentation

- [Architecture](docs/architecture.md)
- [Register map](docs/register_map.md)
- [Driver API](docs/driver_api.md)
- [QEMU device model](docs/qemu_device.md)
- [Testing plan](docs/testing.md)
- [Bring-up guide](docs/bringup.md)
- [Demo plan](docs/demo.md)

## License

This repository uses the MIT license for project documentation and scripts.
QEMU-facing source files carry GPL-compatible SPDX identifiers because they are
intended to become part of a QEMU patch.
