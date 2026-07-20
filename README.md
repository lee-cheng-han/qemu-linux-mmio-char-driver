# QEMU Linux MMIO Character Driver

An end-to-end embedded Linux driver project built around a custom
QEMU-emulated MMIO mailbox device.

The project implements both sides of the stack: a QEMU `SysBusDevice` named
`virt-mbox` and a Linux platform character driver named `vmbox`. The driver
exposes `/dev/vmbox0` and supports FIFO-backed read/write, interrupt-driven
blocking I/O, `poll()`, ioctl status/statistics, sysfs attributes, and debugfs
diagnostics.

## What This Demonstrates

This repository is intended to look and behave like a real kernel/QEMU bring-up
project, It includes:

- a documented MMIO register contract
- a QEMU device model with FIFOs, IRQ state, timer-backed processing, and
  VMState migration support
- a Linux platform driver using `readl()`/`writel()`, IRQ handling, wait queues,
  character-device registration, and safe remove/unload lifetime rules
- stable userspace UAPI structs with ABI size guards
- sysfs attributes for stable observability and debugfs files for developer
  diagnostics
- a single-open `/dev/vmbox0` policy with documented semantics
- QEMU/Linux integration helper scripts
- userspace regression and robustness test scaffolding
- a working ARM64 QEMU boot demo with captured output

## Architecture

```text
guest demo test / userspace tools
        |
        | open(), read(), write(), poll(), ioctl()
        v
/dev/vmbox0
        |
        | Linux vmbox platform character driver
        | sysfs + debugfs observability
        v
MMIO registers + interrupt
        |
        | readl(), writel()
        v
QEMU virt-mbox SysBusDevice
        |
        v
TX FIFO -> timer processing -> RX FIFO
```

The demo writes `hello` to `/dev/vmbox0`. The QEMU device processes bytes
through its TX/RX FIFO path and returns `HELLO`, proving the userspace, Linux
driver, MMIO register map, IRQ path, and QEMU device model are working together.

## How It Works

The hardware/software contract is a small MMIO register map. The guest driver
discovers the device through a devicetree node with `compatible = "virt,mbox"`,
maps the register window, validates the device ID/version registers, and enables
interrupts.

Data flows through two FIFOs:

- userspace writes bytes to `/dev/vmbox0`
- the Linux driver writes each byte to the QEMU device `TX_DATA` register
- QEMU queues bytes in its TX FIFO and schedules timer-backed processing
- processed bytes move into the QEMU RX FIFO
- QEMU raises an interrupt when RX data is ready
- the Linux IRQ handler wakes readers and poll waiters
- userspace reads the processed bytes from `/dev/vmbox0`

The demo processing transform is intentionally simple and deterministic:
lowercase ASCII bytes are returned as uppercase bytes. That makes the end-to-end
test easy to inspect: writing `hello` should read back `HELLO`.

The driver also exposes ioctl status/statistics, stable sysfs attributes, and
debugfs diagnostic files so the runtime state can be inspected without
instrumenting the data path.

## Repository Layout

```text
qemu/                 QEMU device source, headers, and integration fragments
kernel/               Linux driver, UAPI header, Kconfig/Makefile fragments
docs/                 architecture, register map, driver API, demo logs
tests/                userspace tests, robustness scripts, demo init program
scripts/              apply/build/demo helper scripts
.github/workflows/   repository hygiene and userspace build CI
```

This repository is not a full QEMU or Linux checkout. It contains project-owned
source files and helper scripts that apply those files to external source trees.

## Quick Check

Run the local repository checks:

```sh
make check
```

This validates expected files, SPDX markers, whitespace hygiene, and builds the
userspace regression binary.

## Running The ARM64 Demo

Expected external checkouts:

```text
~/qemu        upstream QEMU checkout
~/work/linux  upstream Linux checkout
```

Required host package on Ubuntu/WSL:

```sh
sudo apt-get install -y gcc-aarch64-linux-gnu
```

Run:

```sh
cd ~/qemu-linux-mmio-char-driver
scripts/run-arm64-demo.sh ~/qemu ~/work/linux
```

The script:

1. applies the QEMU payload to the QEMU checkout
2. applies temporary ARM `virt` machine wiring for the local demo
3. applies the Linux payload to the Linux checkout
4. builds the ARM64 guest kernel pieces
5. builds `drivers/misc/vmbox.ko`
6. builds a static ARM64 `/init` demo program
7. creates a tiny initramfs
8. boots `qemu-system-aarch64 -machine virt`
9. loads `vmbox.ko` and validates `/dev/vmbox0`

The first run can take a while because it builds an ARM64 kernel. Later runs are
incremental and much faster.

## Manual Integration Helpers

```sh
scripts/apply-qemu.sh ~/qemu
scripts/apply-linux.sh ~/work/linux
scripts/build-userspace.sh /tmp/vmbox_test
scripts/run-e2e-checklist.sh
```

The apply scripts copy this repository's source payload into external checkouts
and append the relevant build fragments if markers are not already present.

## Documentation

- [Architecture](docs/architecture.md)
- [Register Reference](docs/REGISTERS.md)
- [Driver API](docs/driver_api.md)
- [Concurrency And Lifetime](docs/concurrency.md)
- [QEMU Device Model](docs/qemu_device.md)
- [Testing Plan](docs/testing.md)
- [Bring-Up Guide](docs/bringup.md)
- [Demo Guide](docs/demo.md)
- [End-To-End Bring-Up](docs/e2e.md)
- [Captured Demo Output](docs/demo-output.txt)

## Current Status

The stack has been validated locally with:

- a current upstream QEMU checkout
- a current upstream Linux checkout
- an ARM64 Linux guest kernel
- the `vmbox.ko` platform driver module
- a minimal initramfs-based guest smoke test

Captured passing output is stored in [docs/demo-output.txt](docs/demo-output.txt).

Key demo result:

```text
[PASS] loaded vmbox.ko
[PASS] /dev/vmbox0 appeared
[PASS] open /dev/vmbox0
[PASS] write message
[PASS] poll readable
[PASS] read transformed data: HELLO
[PASS] ioctl status fifo_depth=16 tx=0 rx=0
[PASS] ioctl stats bytes_written=5 bytes_read=5 irqs=5
vmbox demo passed
```

### Limitations

- The ARM64 demo uses `KBUILD_MODPOST_WARN=1` for the single-module build so it
  does not require a full clean module-symbol pass before every demo run.
- QTest coverage is scaffolded, but runnable QTest integration remains future
  hardening.
- Automated boot-demo CI is not enabled yet.
- The driver intentionally supports one device instance and one opener.
- Runtime PM, suspend/resume, DMA, and multi-instance support are future work.

## License

Project documentation and scripts use the MIT license. QEMU-facing and
kernel-facing source files carry GPL-compatible SPDX identifiers appropriate for
their intended upstream trees.
