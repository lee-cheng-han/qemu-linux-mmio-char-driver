# Testing Plan

Testing is split by layer so failures point to the right part of the stack.

## Repository Hygiene

Current CI checks:

- required top-level files
- required documentation files
- required QEMU source files
- SPDX headers in QEMU source files
- trailing whitespace
- tab characters in Markdown and YAML

Run the same checks locally with:

```sh
make check
```

## QEMU Device Tests

The initial QTest skeleton lives at:

```text
qemu/tests/qtest/qemu_mbox-test.c
```

It becomes runnable after the device is instantiated in a QEMU machine or QTest
harness. The current skeleton assumes a placeholder machine named
`qemu-mbox-test-machine` and a device base address of `0x10000000`.

Initial QTest cases:

- read ID register
- read VERSION register
- verify FIFO_DEPTH
- write RESET and verify reset state
- verify temporary TX/RX smoke behavior
- verify byte `0x00` is counted as written TX data
- reject invalid access sizes through guest-error logging where practical in a
  later runnable test

FIFO QTest cases:

- TX_COUNT increments on TX_DATA writes
- RX_COUNT increments after processing
- TX_FULL sets at FIFO depth
- RX_READY clears after RX_DATA drains
- RESET clears FIFOs and IRQ status

IRQ QTest cases:

- RX_READY IRQ is raised when data becomes available
- TX_SPACE IRQ is raised when TX space returns
- write-one-to-clear IRQ_STATUS behavior works
- masked IRQ bits do not raise the line

## Kernel Driver Tests

Kernel-side validation should start with probe and remove paths:

- compatible string matches
- MMIO resource maps
- ID and VERSION validation succeeds
- unsupported ID or FIFO depth fails cleanly
- IRQ request succeeds
- character device is registered and removed cleanly

## Userspace Tests

Userspace regression tests should validate the public `/dev/qemu_mbox0`
interface:

- basic open and close
- basic write then read
- blocking read wakeup
- blocking write wakeup
- non-blocking read returns `EAGAIN`
- non-blocking write returns `EAGAIN` when TX is full
- `poll()` reports read readiness
- `poll()` reports write readiness
- ioctl reset
- ioctl get status
- ioctl get stats
- invalid ioctl handling
- reset recovery
- 1000-message stress test

## CI Growth

CI should grow in stages:

1. Repository hygiene.
2. Userspace test compilation.
3. Kernel module compilation.
4. QEMU device model compilation.
5. QTest execution.
6. Boot QEMU and run Linux-side regression tests.

Each stage should be added only after the corresponding component exists.
