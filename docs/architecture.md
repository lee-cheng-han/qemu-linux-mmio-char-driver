# Architecture

`vmbox` is a small mailbox-style peripheral used to exercise the same
interfaces an embedded Linux driver would use against real MMIO hardware.

The project is split into four layers:

```text
tests and demo applications
        |
Linux character-device API
        |
Linux platform driver
        |
QEMU MMIO device model
```

## Hardware Contract

The hardware contract is defined in [REGISTERS.md](REGISTERS.md). The contract
is intentionally simple:

- A fixed 0x1000-byte MMIO window.
- 32-bit register accesses only.
- Stable ID and VERSION registers.
- CONTROL, STATUS, IRQ_STATUS, and IRQ_ENABLE control plane registers.
- TX_DATA and RX_DATA data path registers.
- FIFO occupancy registers.
- A soft reset command.

The current QEMU model implements the register surface, real TX/RX FIFO state,
and timer-backed processing delay. Later milestones add interrupt generation.

## QEMU Device Model

The QEMU device is a `SysBusDevice` named `virt-mbox`. It owns one MMIO memory
region and one processing timer, and later will own one IRQ line.

The final QEMU model should provide:

- deterministic reset state
- bounded FIFO behavior
- write-one-to-clear IRQ status bits
- delayed data processing through a QEMU timer
- debug-visible counters and state
- migration state for guest-visible registers and FIFO contents

## QEMU Migration And Snapshot State

The QEMU device must maintain a `VMStateDescription`. Live migration is not the
main practical goal of this project, but QEMU reviewers expect real device
models to describe the state needed by `savevm`, `loadvm`, and live migration.

Final migrated state should include:

- guest-visible control registers
- IRQ status and enable state
- TX FIFO contents
- TX FIFO head, tail, and count
- RX FIFO contents
- RX FIFO head, tail, and count
- BUSY/processing state
- timer state where practical
- QOM/debug counters only if they are treated as guest-relevant enough to keep

If any field is deliberately excluded, the code should explain why. Omitting
`VMStateDescription` entirely is acceptable only for an explicitly documented
temporary prototype, not for the final device model.

## Linux Driver

The Linux driver source will be `kernel/drivers/misc/vmbox.c`, with module name
`vmbox`. It will be a platform character driver matching the device tree
compatible string `virt,mbox` through an `of_device_id` table. The compatible
string names the current virtual hardware variant; the driver should remain
platform-agnostic where possible.

The expected driver responsibilities are:

- map the MMIO resource with managed kernel APIs
- validate ID, VERSION, and FIFO_DEPTH at probe time
- request an IRQ
- register a character device as `/dev/vmbox0`
- implement blocking and non-blocking `read()` and `write()`
- implement `poll()` wakeups from the IRQ path
- expose ioctl commands for reset, status, stats, and mode
- expose debugfs state for development and troubleshooting

## Hardware Access Correctness

The Linux driver must access device registers with `readl()` and `writel()`.
Raw pointer dereferences are not acceptable for MMIO registers.

`readl()` and `writel()` provide the expected MMIO access semantics and ordering
for portable Linux driver code. Register offsets should stay in one canonical
place, either a shared header or [REGISTERS.md](REGISTERS.md), so QEMU, the
kernel driver, and tests do not drift.

No extra `mb()` or `wmb()` barriers are planned for the initial driver because
the register protocol is simple and `readl()`/`writel()` provide the required
MMIO ordering for normal command/status interactions. If later code adds shared
memory, DMA, or lockless producer/consumer state outside the MMIO register
interface, the design must re-evaluate whether additional barriers are needed
and document the exact ordering pair.

## Concurrency And Locking

Detailed rules are maintained in [concurrency.md](concurrency.md).

The driver will need one per-device state object. That object should contain the
mapped register base, IRQ number, cdev state, wait queues, locks, and software
stats.

QEMU-side design:

- FIFO state belongs to the QEMU emulated hardware model.
- FIFO helpers should document their execution-context assumptions.
- FIFO helpers are called from QEMU device and timer context.
- Do not add pthread locking unless the device is later moved to an IOThread or
  another concurrent execution context.
- This is a deliberate design choice, not an omission.
- The QEMU timer may process one TX byte per tick and push the result into RX.

Linux-side design:

- Use `spinlock_t` for Linux driver-owned state touched from both IRQ context
  and process context.
- Protect cached status, IRQ-related flags, statistics counters, cached wait
  conditions, and lifetime flags such as `device_gone`.
- Use `spin_lock_irqsave()` and `spin_unlock_irqrestore()` for state shared with
  the IRQ handler.
- Wait queues are used for blocking reads and writes.
- Keep the IRQ handler small: read IRQ status, acknowledge IRQ status, update
  driver-owned state and stats, then wake wait queues with
  `wake_up_interruptible()`.
- Do not do heavy work in IRQ context.
- `read()` and `write()` perform MMIO data movement from process context.
- Blocking waits must safely re-check conditions after wakeup.
- Wait conditions must also account for device removal.
- File operations validate user pointers and handle `O_NONBLOCK` correctly.

## Open Policy

The initial `/dev/vmbox0` policy is single-open. Only one userspace opener may
hold the character device at a time.

Rationale:

- the hardware interface is a single FIFO pair
- single-open avoids two processes racing on the same byte stream
- it keeps blocking, poll, reset, and ioctl semantics reviewable for the first
  production-quality version

If multi-open support is added later, it must define ownership, interleaving,
reset semantics, and whether stats are per-file or per-device.

## Device Lifetime And Remove/Unbind Safety

Userspace may keep `/dev/vmbox0` open while the platform device is removed or
while module unload begins. The driver must not free driver state while file
operations may still access it.

Design requirements:

- Use `kref`, `refcount_t`, or a clearly documented equivalent lifetime model.
- `open()` acquires a reference to device state.
- `release()` drops the reference.
- `.remove()` marks the device gone and prevents new opens.
- `.remove()` disables device interrupt generation.
- `.remove()` synchronizes and frees the IRQ safely.
- `.remove()` wakes all wait queues so blocked readers and writers exit.
- `.remove()` removes `/dev/vmbox0`, deletes the `cdev`, and removes debugfs.
- Driver state is freed only after the final open file releases its reference.
- `read()`, `write()`, `poll()`, and `ioctl()` check `device_gone`.
- After removal begins, file operations return `-ENODEV`.
- Blocking reads and writes wake and return `-ENODEV` if the device disappears.

Expected behavior:

- no use-after-free if a file descriptor remains open after device removal
- new opens fail after removal begins
- blocked reads and writes wake cleanly during removal
- driver state is freed only after the final reference is dropped

## Module Unload Safety

The module must be insertable, removable, and reinsertable without leaks or
stale device state.

Required cleanup order:

1. mark device gone
2. prevent new opens
3. disable device interrupt generation
4. synchronize and free IRQ safely
5. wake wait queues
6. remove `/dev/vmbox0`
7. delete `cdev`
8. destroy device and class objects if owned by the module
9. remove debugfs entries
10. release MMIO/device resources
11. drop final references only when no file operations can access freed memory

Platform `.remove()` and module unload should share common teardown helpers for
the overlapping device cleanup path. The module exit path may additionally
destroy module-owned global class/major state, but the ordering rules for a
single device should not be duplicated in two independent implementations.

## Ownership Boundaries

The QEMU device owns hardware behavior:

- register reset values
- FIFO occupancy
- STATUS bits
- IRQ pending bits
- processing latency

The Linux driver owns operating-system behavior:

- character-device registration
- sleeping and wakeup policy
- userspace copy validation
- ioctl validation
- debugfs formatting
- software stats

Userspace owns integration behavior:

- opening `/dev/vmbox0`
- choosing blocking or non-blocking mode
- using `poll()` for event-driven I/O
- interpreting ioctl results

## Source-Tree Hygiene

Planned kernel integration files:

- `kernel/drivers/misc/vmbox.c`
- `kernel/include/uapi/linux/vmbox.h`
- `kernel/Documentation/devicetree/bindings/misc/virt,mbox.yaml`
- `kernel/drivers/misc/Kconfig.fragment`
- `kernel/drivers/misc/Makefile.fragment`
- `.clang-format`
- `MAINTAINERS`

Kernel driver requirements:

- SPDX identifier on every new source file
- `MODULE_LICENSE("GPL")`
- `MODULE_AUTHOR`
- `MODULE_DESCRIPTION`
- `.owner = THIS_MODULE` in `file_operations`
- Kconfig symbol `VMBOX`
- Makefile integration through `obj-$(CONFIG_VMBOX) += vmbox.o`
- logging with `dev_err()`, `dev_warn()`, `dev_info()`, and `dev_dbg()`
- no raw `printk()` for normal driver logging
- style checked with `checkpatch.pl --strict`
- formatting kept consistent with `.clang-format`

Keeping these boundaries explicit makes bugs easier to isolate.

## Step 0 Completion Criteria

Step 0 is complete when the repository has:

- a top-level README explaining the project and status
- an architecture document
- a register map with reset, FIFO, IRQ, and invalid-access semantics
- a QEMU device-model document
- a Linux driver API document with planned UAPI shapes
- a testing plan
- a bring-up guide
- a demo plan
- local and CI repository hygiene checks

## Build Model

The repository is project-owned source, not a full QEMU or kernel checkout. The
QEMU files are intended to become a patch against a real QEMU source tree, while
the kernel driver will be built against a configured Linux kernel tree.

Early CI checks repository hygiene. Build and runtime CI should be added only as
the relevant components become real.
