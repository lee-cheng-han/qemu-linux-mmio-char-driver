# Bring-Up Guide

This guide tracks the expected path from project source files to a running
QEMU-backed Linux driver stack.

## Current State

The repository contains project-owned QEMU source files, docs, QEMU integration
fragments, and QTest skeletons. It is not a complete QEMU or Linux checkout.

Completed repository milestones:

- Step 0: project contract and planning
- Step 1: minimal QEMU MMIO device source alignment
- Step 2: QEMU integration package
- Step 3: QTest skeleton

Before implementing Step 4 FIFO code, the roadmap and design docs now include
review-readiness requirements for concurrency, lifetime safety, UAPI
correctness, devicetree rigor, observability, testing, and CI.

## Step 1: Validate The Repository

From the repository root:

```sh
make check
```

This runs local hygiene checks that mirror the first CI workflow.

## Step 2: Integrate The QEMU Device

Planned external QEMU work:

- copy `qemu/hw/misc/qemu_mbox.c` into a QEMU source tree
- copy `qemu/include/hw/misc/qemu_mbox.h` into the matching include path
- add the Meson entry from `qemu/patches/meson.build.fragment`
- add the Kconfig entry from `qemu/patches/Kconfig.fragment`
- optionally enable local aarch64 builds with
  `qemu/patches/aarch64-softmmu.default.mak.fragment`
- instantiate the device from a machine, command-line helper, or test harness

The integration notes live in `qemu/patches/README.md`.

Expected early smoke checks:

- ID reads as `0x514d424f`
- VERSION reads as `0x00010000`
- FIFO_DEPTH reads as `16`
- RESET clears CONTROL, STATUS, IRQ_STATUS, and IRQ_ENABLE

## Step 3: Add QTest

QTest coverage starts under:

```text
qemu/tests/qtest/qemu_mbox-test.c
```

The skeleton validates:

- ID
- VERSION
- FIFO_DEPTH
- RESET
- FIFO-backed TX/RX behavior
- byte `0x00` TX count handling

The skeleton becomes runnable after a QEMU machine or QTest harness maps
`virt-mbox` at the test base address.

## Step 4: Real FIFO In QEMU

Goal: replace temporary one-byte behavior with real TX/RX FIFO state.

Build:

- 16-byte TX FIFO
- 16-byte RX FIFO
- FIFO head, tail, and count fields
- FIFO push/pop helpers
- `TX_COUNT`
- `RX_COUNT`
- `TX_FULL`
- `RX_READY`
- `RX_FULL`
- reset clears both FIFOs

## Step 4.5: FIFO Helper Convention And Concurrency Design

Goal: lock down the QEMU FIFO helper convention and Linux driver locking model
before adding real driver I/O.

The design notes live in:

```text
docs/concurrency.md
```

Build:

- QEMU FIFO helper caller assumptions
- QEMU execution-context documentation
- explicit rationale for no QEMU pthread lock in the current model
- Linux driver spinlock plan
- IRQ handler design
- wait queue condition rules
- race-avoidance rules

## Step 5: Processing Timer

Goal: make device behavior asynchronous and realistic.

Build:

- QEMU timer
- `BUSY` status bit
- delayed processing path
- one TX byte processed per timer tick
- result moved into RX FIFO
- timer continues until TX FIFO is empty

Status: implemented in the project-owned QEMU source. External QEMU compile
and runnable QTest verification still require applying the integration package
to a real QEMU checkout.

## Step 6: IRQ Support In QEMU

Goal: raise interrupts when device state changes.

Build:

- QEMU IRQ line
- `sysbus_init_irq`
- IRQ update helper
- `IRQ_STATUS`
- `IRQ_ENABLE`
- write-one-to-clear behavior
- raise IRQ on RX ready
- raise IRQ on TX space
- raise IRQ on done/error

Also update QEMU migration/snapshot support:

- keep `VMStateDescription`
- migrate FIFO contents
- migrate FIFO head/tail/count fields
- migrate BUSY/processing state
- migrate IRQ status and enable state
- document any intentionally non-migrated debug-only fields

## Step 7: QTest Expansion

Goal: test the real QEMU hardware model.

Add tests for:

- TX FIFO count
- RX FIFO count
- FIFO full
- FIFO empty
- reset clears FIFOs
- RX read pops data
- status bits
- IRQ status bits
- IRQ enable and masking behavior
- savevm/loadvm or migration-state smoke coverage if practical

## Step 8: Linux Driver Skeleton

Goal: Linux can probe the device.

Planned files:

- `kernel/drivers/misc/vmbox.c`
- `kernel/include/uapi/linux/vmbox.h`
- `kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml`

Probe should:

- match `virt,mbox`
- map MMIO
- validate ID and VERSION
- validate FIFO depth
- reset hardware
- request IRQ
- allocate driver state
- add `CONFIG_VMBOX` Kconfig integration
- add Makefile integration
- add SPDX identifiers to new source, header, and binding files

Validate the binding schema through the kernel devicetree binding and
dt-schema flow.

## Step 8.5: Device Lifetime And Remove Safety

Goal: make the driver safe if the device is removed while userspace still has
`/dev/vmbox0` open.

Build:

- reference model
- `device_gone` flag
- open/release lifetime handling
- safe remove path
- wake blocked readers and writers during remove
- `-ENODEV` behavior after removal
- single-open policy with `-EBUSY` for a second opener

## Step 9: Character Device Registration

Goal: create `/dev/vmbox0`.

Build:

- `alloc_chrdev_region`
- `cdev_init`
- `cdev_add`
- `class_create`
- `device_create`
- cleanup paths
- `.owner = THIS_MODULE`
- single-open enforcement

## Step 9.5: Module Unload Safety

Goal: ensure the module can be inserted, removed, and reinserted cleanly.

Build:

- module metadata
- `.owner = THIS_MODULE`
- correct init/exit cleanup ordering
- common teardown helper shared by platform `.remove()` and module exit paths
- repeated insmod/rmmod test plan

## Step 10: read() And write()

Goal: userspace can send and receive bytes.

Build:

- `.read`
- `.write`
- `copy_to_user()`
- `copy_from_user()`
- MMIO data movement through `readl()` and `writel()`
- short read/write behavior
- error paths

## Step 11: Blocking And Non-Blocking I/O

Goal: real Linux blocking behavior.

Build:

- wait queue for RX data
- wait queue for TX space
- `O_NONBLOCK` handling
- `-EAGAIN`
- `-ERESTARTSYS`
- IRQ-driven wakeups
- `device_gone` handling in wait conditions

## Step 12: poll() Support

Goal: event-driven userspace support.

Return:

- `POLLIN | POLLRDNORM` when RX data exists
- `POLLOUT | POLLWRNORM` when TX space exists
- `POLLERR` when device error is set

## Step 13: ioctl() UAPI

Goal: controlled driver commands.

Build:

- `VMBOX_IOC_RESET`
- `VMBOX_IOC_GET_STATUS`
- `VMBOX_IOC_GET_STATS`
- `VMBOX_IOC_SET_MODE`
- `.compat_ioctl` under `CONFIG_COMPAT`
- compile-time UAPI struct size guards
- validation for bad magic, unknown command, bad pointer, invalid mode, and
  nonzero reserved fields

The documented size constants are placeholders until the real UAPI structs are
defined. Step 13 should finalize the struct layouts first, then add guards for
those exact sizes.

## Step 14: sysfs, debugfs, And Observability

Goal: observability.

Expose stable sysfs attributes:

- platform-device `status`
- platform-device `fifo_depth`

Expose:

- `/sys/kernel/debug/vmbox0/stats`
- `/sys/kernel/debug/vmbox0/regs`
- `/sys/kernel/debug/vmbox0/fifo`

Logging requirements:

- use `dev_err()`, `dev_warn()`, `dev_info()`, and `dev_dbg()`
- avoid raw `printk()`
- log probe/remove errors clearly
- keep normal read/write paths quiet

## Step 15: Userspace Test Suite

Goal: test `/dev/vmbox0`.

Add tests for:

- open/close
- basic read/write
- non-blocking read
- non-blocking write
- blocking read wakeup
- blocking write wakeup
- poll wakeup
- ioctl reset
- ioctl status
- ioctl stats
- invalid ioctl
- FIFO full
- FIFO empty
- stress 1000 messages
- udev or device-class permission setup for non-root test users

## Step 15.5: Robustness And Negative Testing

Goal: test teardown paths, compat ioctl, ioctl fuzzing, and blocked I/O failure
cases.

Build:

- remove while open test
- blocked read during remove test
- blocked write during remove test
- repeated module reload test
- 32-bit compat ioctl test if supported
- ioctl fuzz test
- dmesg scan for kernel warnings and errors

## Step 16: CI Expansion

Goal: automate confidence.

Stages:

- CI v1: repo hygiene
- CI v2: userspace build
- CI v2.5: static analysis and style checks
- CI v3: kernel module build
- CI v4: QEMU device compile
- CI v5: QTest execution
- CI v6: boot QEMU and run userspace tests
- CI v6.5: sanitizer QEMU boot test
- CI v7: end-to-end demo validation

Style and static analysis should include `.clang-format`,
`checkpatch.pl --strict`, `sparse`, and optional `smatch`.

## Step 16.5: Runtime Sanitizer CI

Goal: catch memory safety issues during real driver execution.

Build:

- KASAN-enabled QEMU kernel boot if practical
- kmemleak-enabled boot if practical
- userspace regression tests
- ioctl fuzz tests
- remove/unbind tests
- dmesg and debugfs report scanning

## Step 17: End-To-End Bring-Up

Goal: run the whole stack.

Final flow:

```text
QEMU boots Linux
QEMU exposes virt-mbox MMIO device
Linux probes vmbox driver
/dev/vmbox0 appears
userspace test suite runs
all tests pass
```

## Step 18: Final Demo And Polish

Goal: make it portfolio-ready.

Add:

- final README demo section
- architecture diagram
- QEMU command line
- kernel build notes
- test output
- debugfs sample output
- known limitations
- future work
- MAINTAINERS entry
