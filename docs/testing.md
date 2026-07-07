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
`virt-mbox-test-machine` and a device base address of `0x10000000`.

Initial QTest cases:

- read ID register
- read VERSION register
- verify FIFO_DEPTH
- write RESET and verify reset state
- verify timer-backed FIFO TX/RX smoke behavior
- verify byte `0x00` is accepted and returned through RX
- verify BUSY is set while processing is pending
- verify RX full and TX full status
- verify TX overflow sets error status
- verify IRQ_STATUS RX_READY and DONE bits
- verify IRQ_STATUS write-one-to-clear behavior
- verify IRQ_ENABLE masks unsupported bits
- verify TX_SPACE and ERROR interrupt status events
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
- second open fails with `EBUSY` while the first file is open
- sysfs attributes expose stable `status` and `fifo_depth`
- debugfs entries expose developer diagnostics when debugfs is mounted

## Userspace Tests

Userspace regression tests should validate the public `/dev/vmbox0`
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
- non-root access through documented udev or device-class permissions

Robustness tests:

- remove or unbind while `/dev/vmbox0` is open
- blocked read during remove
- blocked write during remove
- repeated module insert/remove
- dmesg scan for WARN, OOPS, BUG, and KASAN reports
- ioctl fuzz test

Compat ioctl tests:

- build a 32-bit userspace test binary if the toolchain supports it
- run the 32-bit binary against the 64-bit kernel
- exercise reset, get status, get stats, set mode, bad magic, invalid command,
  invalid pointer, and invalid reserved fields
- confirm behavior matches the native 64-bit test binary

Blocking tests must include timeouts. If an IRQ never arrives, the test should
fail with a clear timeout instead of hanging indefinitely.

## Ioctl Fuzzing

The bounded manual fuzzer plan lives in:

```text
tests/fuzz/README.md
```

It should randomize command numbers and argument values, test NULL pointers,
invalid pointers, misaligned pointers, valid pointers with invalid data, and
reserved-field violations. Full syzkaller support is future work.

## CI Growth

CI should grow in stages:

1. Repository hygiene.
2. Userspace test compilation.
3. Static analysis and kernel style checks.
4. Kernel module compilation.
5. QEMU device model compilation.
6. QTest execution.
7. Boot QEMU and run Linux-side regression tests.
8. Runtime sanitizer QEMU boot test.
9. End-to-end demo validation.

Each stage should be added only after the corresponding component exists.

Static-analysis and style checks should include:

- `.clang-format` formatting checks where practical
- `checkpatch.pl --strict`
- `sparse` through the kernel-supported `make C=1` flow
- optional `smatch` if available

Runtime sanitizer boot testing should run the userspace suite under a kernel
configured with KASAN and kmemleak if practical, then scan dmesg and debugfs for
use-after-free, out-of-bounds access, invalid free, leaks, WARN, BUG, and OOPS.
