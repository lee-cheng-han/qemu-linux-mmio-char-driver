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
- Linux driver API and ioctl UAPI structures
- Driver/module naming: `vmbox`
- Character device node: `/dev/vmbox0`
- Devicetree compatible string: `virt,mbox`
- Canonical register reference in `docs/REGISTERS.md`
- Devicetree binding schema draft for `virt,mbox`
- Testing, bring-up, and demo plans
- Initial QEMU MMIO device source files
- QEMU integration notes and Meson/Kconfig/device-enable fragments
- QTest skeleton for MMIO, FIFO, timer, and IRQ register behavior
- ID, VERSION, CONTROL, STATUS, TX_DATA, RX_DATA, IRQ_STATUS, IRQ_ENABLE,
  TX_COUNT, RX_COUNT, FIFO_DEPTH, and RESET register definitions
- Real TX/RX FIFO state in the QEMU device model
- Timer-backed QEMU processing path with BUSY status
- QEMU IRQ line support with sticky IRQ_STATUS and IRQ_ENABLE masking
- Linux `vmbox` platform driver skeleton with MMIO probe validation and IRQ
  request
- Initial Linux driver lifetime state for safe remove/unbind handling
- `/dev/vmbox0` registration with single-open `open()`/`release()` policy
- Basic `/dev/vmbox0` read/write data movement over MMIO
- Blocking/non-blocking read/write behavior, `poll()`, and ioctl UAPI
- Stable sysfs attributes and debugfs diagnostics for the Linux driver
- Userspace regression test and robustness smoke-test scripts
- CI jobs for repo hygiene, userspace build, and static-style scaffolding
- End-to-end bring-up and final demo documentation
- Module init/exit cleanup for char-device and platform-driver state
- Initial repository hygiene CI
- Concurrency, lifetime-safety, compat ioctl, observability, and robustness
  testing roadmap updates
- QEMU VMState/migration requirements documented
- sysfs attributes alongside debugfs
- UAPI struct size guards
- Single-open `/dev/vmbox0` policy
- `.clang-format` and `MAINTAINERS` project hygiene files

External validation still left:

- QEMU runtime instantiation in a machine or test harness
- runnable QTest coverage inside a QEMU checkout
- external Linux kernel module build
- full QEMU boot/runtime CI

## Completed Repository Milestones

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

The in-repository implementation milestones are complete. The remaining work is
external validation: applying the QEMU pieces to a real QEMU checkout, applying
the Linux driver pieces to a real kernel checkout, building both, booting the
stack, and running the guest tests.

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
- [End-to-end bring-up](docs/e2e.md)

## Quick Local Checks

```sh
make check
```

This validates repository hygiene and builds the userspace regression test. Full
runtime validation still requires applying the QEMU and Linux fragments to real
source trees and booting the stack.

## External Integration Helpers

```sh
scripts/apply-qemu.sh ~/work/qemu
scripts/apply-linux.sh ~/work/linux
scripts/build-userspace.sh /tmp/vmbox_test
scripts/run-e2e-checklist.sh
```

The apply scripts copy this repository's QEMU/Linux payload into external
source checkouts and append the local build fragments if they are not already
present.

## License

This repository uses the MIT license for project documentation and scripts.
QEMU-facing source files carry GPL-compatible SPDX identifiers because they are
intended to become part of a QEMU patch.

## Final Project Summary

This repository implements a QEMU-emulated MMIO mailbox peripheral and Linux platform
character driver exposing `/dev/vmbox0`, with FIFO-backed data paths, explicit
concurrency and locking design, safe remove/unbind lifetime handling, module
unload safety, portable MMIO access using `readl()`/`writel()`,
interrupt-driven blocking I/O, `poll()`/`ioctl()`/`compat_ioctl` support,
debugfs instrumentation, production-style error counters, devicetree binding
schema, QTest hardware-model coverage, userspace regression tests, ioctl
fuzzing, static-analysis CI scaffolding, sanitizer runtime-test planning, and
end-to-end QEMU boot validation docs.
