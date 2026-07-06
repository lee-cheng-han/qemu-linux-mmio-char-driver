<!-- SPDX-License-Identifier: MIT -->

# Concurrency And Locking

This document records the execution-context assumptions for the virtual mailbox
device model and the planned Linux `vmbox` driver. The goal is to keep FIFO,
timer, IRQ, and blocking I/O behavior reviewable as the project grows.

## QEMU Device Model

The QEMU-side FIFO state belongs entirely to the emulated `virt-mbox` device.
The current implementation executes FIFO operations from the device MMIO access
path and from the processing timer callback.

Current assumptions:

- FIFO helpers run in the QEMU device context.
- Timer processing runs in the same QEMU main-loop context.
- No pthread mutex is needed for the current model.
- No guest-visible FIFO state is shared with another host thread.
- No IOThread currently owns the device.

This is a deliberate design decision. Adding host-side locks before there is a
second host execution context would make the device harder to review without
providing useful protection.

If the device is later moved to an IOThread or another concurrent execution
context, the design must be revisited. At that point, FIFO helpers need either a
clear single-thread ownership rule or host-side locking around shared device
state.

## QEMU FIFO Helper Rules

FIFO helpers should remain small and side-effect limited:

- `fifo_push` updates only FIFO storage, tail, and count.
- `fifo_pop` updates only FIFO storage, head, count, and output byte.
- status bit derivation stays in a separate status-update helper.
- IRQ line changes stay in a separate IRQ-update helper once IRQ support exists.
- timer scheduling stays outside generic FIFO push/pop helpers.

This separation keeps state transitions readable and makes QTest failures easier
to diagnose.

Writes to TX_DATA push one byte into TX and schedule a QEMU virtual-clock
timer. Each timer tick moves at most one byte from TX to RX while RX has space.
If RX is full, processing pauses until software reads RX_DATA and frees space.

## Linux Driver Locking Plan

The Linux driver should use one per-device state object. Driver-owned state that
is touched from both IRQ context and process context should be protected by a
`spinlock_t`.

Protect with the spinlock:

- cached status
- IRQ status snapshots
- IRQ-related flags
- statistics counters
- cached wait-queue condition state, if added
- `device_gone`
- single-open state

Use `spin_lock_irqsave()` and `spin_unlock_irqrestore()` when a code path shares
state with the IRQ handler.

Do not hold the spinlock while copying data to or from userspace. Do not sleep
while holding the spinlock.

## IRQ Handler Rules

The IRQ handler should be short:

1. read IRQ status
2. acknowledge IRQ status
3. update driver-owned cached state and counters
4. wake wait queues with `wake_up_interruptible()`

The IRQ handler should not do heavy data movement. `read()` and `write()` should
perform MMIO data movement from process context.

## Wait Queue Rules

Blocking wait conditions must be written so they are safe after spurious wakeups
and device removal.

Read waits should wake when:

- RX data is available
- an error condition is visible
- `device_gone` is set

Write waits should wake when:

- TX space is available
- an error condition is visible
- `device_gone` is set

After wakeup, file operations must re-check the condition before touching MMIO.
If `device_gone` is set, they should return `-ENODEV`.

## Race-Avoidance Rules

- Register wait queues before checking readiness in `poll()`.
- Re-check hardware or cached state after every blocking wait.
- Keep MMIO access in process context for read/write paths.
- Use `READ_ONCE()` or lock-protected reads for lifetime flags if the final
  driver design reads them locklessly.
- Use one common teardown helper for platform `.remove()` and module unload
  paths so cleanup ordering does not drift.
