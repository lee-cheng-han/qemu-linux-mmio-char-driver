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
/dev/vmbox0
        |
        | Linux platform character driver
        v
MMIO registers and IRQ
        |
        | readl(), writel()
        v
QEMU virt-mbox SysBusDevice
```

## Current Status

Implemented:

- Repository skeleton and architecture documentation
- Register map contract for `vmbox`, including reset, FIFO, IRQ, and
  invalid-access semantics
- Planned Linux driver API and ioctl UAPI structures
- Planned driver/module naming: `vmbox`
- Planned device node: `/dev/vmbox0`
- Planned devicetree compatible string: `virt,mbox`
- Canonical register reference in `docs/REGISTERS.md`
- Devicetree binding schema draft for `virt,mbox`
- Testing, bring-up, and demo plans
- Initial QEMU MMIO device source files
- QEMU integration notes and Meson/Kconfig/device-enable fragments
- QTest skeleton for the minimal MMIO register contract
- ID, VERSION, CONTROL, STATUS, TX_DATA, RX_DATA, IRQ_STATUS, IRQ_ENABLE,
  TX_COUNT, RX_COUNT, FIFO_DEPTH, and RESET register definitions
- Real TX/RX FIFO state in the QEMU device model
- Timer-backed QEMU processing path with BUSY status
- QEMU IRQ line support with sticky IRQ_STATUS and IRQ_ENABLE masking
- Initial repository hygiene CI
- Concurrency, lifetime-safety, compat ioctl, observability, and robustness
  testing roadmap updates
- QEMU VMState/migration requirements documented
- Planned sysfs attributes alongside debugfs
- UAPI struct size guard requirements
- Single-open `/dev/vmbox0` policy
- `.clang-format` and `MAINTAINERS` project hygiene files

Not implemented yet:

- QEMU runtime instantiation in a machine or test harness
- runnable QTest coverage inside a QEMU checkout
- Linux platform character driver
- `/dev/vmbox0`
- userspace regression tests
- full build and boot CI

## Planned Milestones

1. Lock down architecture, register map, driver API, and testing docs.
2. Integrate the minimal QEMU MMIO device into a QEMU source tree.
3. Add QTest skeleton coverage for ID, VERSION, RESET, and basic register
   behavior.
4. Replace temporary TX/RX behavior with real 16-byte TX and RX FIFOs.
5. Add processing latency and IRQ generation.
6. Implement the Linux `vmbox` platform character driver probe and remove
   paths.
7. Add `/dev/vmbox0` read, write, non-blocking mode, and poll support.
8. Add ioctl commands for reset, status, stats, and mode configuration.
9. Add debugfs observability and stress tests.
10. Expand CI to build QEMU, build the kernel module, run QTest, and run
    Linux-side tests.

Step 0, the project contract and planning milestone, is complete. Step 1, the
source-level minimal QEMU MMIO device alignment, is complete. Step 2, the QEMU
integration package for applying the device to a real QEMU checkout, is complete
for this repository. The next verification task is applying it to an external
QEMU checkout and building `aarch64-softmmu`.

## Documentation

- [Architecture](docs/architecture.md)
- [Register reference](docs/REGISTERS.md)
- [Legacy register map notes](docs/register_map.md)
- [Concurrency and locking](docs/concurrency.md)
- [Driver API](docs/driver_api.md)
- [QEMU device model](docs/qemu_device.md)
- [Testing plan](docs/testing.md)
- [Bring-up guide](docs/bringup.md)
- [Demo plan](docs/demo.md)

## License

This repository uses the MIT license for project documentation and scripts.
QEMU-facing source files carry GPL-compatible SPDX identifiers because they are
intended to become part of a QEMU patch.

## Final Project Summary

Implemented a QEMU-emulated MMIO mailbox peripheral and Linux platform
character driver exposing `/dev/vmbox0`, with FIFO-backed data paths, explicit
concurrency and locking design, safe remove/unbind lifetime handling, module
unload safety, portable MMIO access using `readl()`/`writel()`,
interrupt-driven blocking I/O, `poll()`/`ioctl()`/`compat_ioctl` support,
debugfs instrumentation, production-style error counters, devicetree binding
schema, QTest hardware-model coverage, userspace regression tests, ioctl
fuzzing, static-analysis CI, sanitizer-based runtime testing, and end-to-end
QEMU boot validation.
