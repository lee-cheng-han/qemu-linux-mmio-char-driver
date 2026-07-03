# Bring-Up Guide

This guide tracks the expected path from project source files to a running
QEMU-backed Linux driver stack.

## Current State

The repository currently contains project-owned QEMU source files, docs, and
CI hygiene checks. It is not yet a complete QEMU checkout, and the QEMU device
is not yet wired into QEMU Meson/Kconfig files.

## Step 1: Validate The Repository

From the repository root:

```sh
make check
```

This runs local hygiene checks that mirror the first CI workflow.

## Step 2: Integrate The QEMU Device

Planned work:

- copy `qemu/hw/misc/qemu_mbox.c` into a QEMU source tree
- copy `qemu/include/hw/misc/qemu_mbox.h` into the matching include path
- add the Meson entry from `qemu/patches/meson.build.fragment`
- add the Kconfig entry from `qemu/patches/Kconfig.fragment`
- optionally enable local aarch64 builds with
  `qemu/patches/aarch64-softmmu.default.mak.fragment`
- instantiate the device from a machine, command-line helper, or test harness

The integration notes live in `qemu/patches/README.md`.

This first QEMU step is compile-oriented. Runtime instantiation is tracked as a
separate task because a sysbus device needs a machine or test harness to map its
MMIO region and connect IRQ lines.

Step 2 is complete for this repository when the QEMU payload files, Meson
fragment, Kconfig fragment, optional target enablement fragment, and integration
instructions are all present and covered by `make check`.

Expected early smoke checks:

- ID reads as `0x514d424f`
- VERSION reads as `0x00010000`
- FIFO_DEPTH reads as `16`
- RESET clears CONTROL, STATUS, IRQ_STATUS, and IRQ_ENABLE

## Step 3: Add QTest

QTest coverage starts under `qemu/tests/qtest`.

The first skeleton file is:

```text
qemu/tests/qtest/qemu_mbox-test.c
```

It validates the stable register contract before FIFO and IRQ behavior are
added:

- ID
- VERSION
- FIFO_DEPTH
- RESET
- temporary TX/RX behavior
- byte `0x00` TX count handling

The skeleton becomes runnable after a QEMU machine or QTest harness maps
`qemu-mbox` at the test base address.

## Step 4: Add FIFO And IRQ Behavior

After the minimal model is tested:

- add 16-byte TX and RX FIFOs
- update TX_COUNT and RX_COUNT from FIFO occupancy
- update STATUS bits from FIFO state
- add timer-backed processing latency
- add IRQ line generation

## Step 5: Bring Up The Linux Driver

The driver should start with probe-only support:

- match the device tree compatible string
- map MMIO
- validate ID and VERSION
- reset hardware
- request IRQ
- register `/dev/qemu_mbox0`

Only after probe/remove are reliable should read, write, poll, ioctl, and
debugfs be added.

## Step 6: Run End-To-End Tests

The final bring-up target is:

```text
QEMU device model -> Linux driver -> /dev/qemu_mbox0 -> userspace tests
```

At that point the test suite should be runnable inside the guest and should
produce a clear pass/fail summary.
