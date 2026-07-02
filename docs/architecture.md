# Architecture

`qemu_mbox` is a small mailbox-style peripheral used to exercise the same
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

The hardware contract is defined in [register_map.md](register_map.md). The
contract is intentionally simple:

- A fixed 0x1000-byte MMIO window.
- 32-bit register accesses only.
- Stable ID and VERSION registers.
- CONTROL, STATUS, IRQ_STATUS, and IRQ_ENABLE control plane registers.
- TX_DATA and RX_DATA data path registers.
- FIFO occupancy registers.
- A soft reset command.

The current QEMU model implements the register surface and a temporary
single-byte data path. Later milestones replace that temporary path with real TX
and RX FIFOs, a timer-backed processing delay, and interrupt generation.

## QEMU Device Model

The QEMU device is a `SysBusDevice` named `qemu-mbox`. It owns one MMIO memory
region and later will own one IRQ line.

The final QEMU model should provide:

- deterministic reset state
- bounded FIFO behavior
- write-one-to-clear IRQ status bits
- delayed data processing through a QEMU timer
- debug-visible counters and state
- migration state for guest-visible registers and FIFO contents

## Linux Driver

The Linux driver will be a platform character driver matching a device tree
compatible string such as `hank,qemu-mbox`.

The expected driver responsibilities are:

- map the MMIO resource with managed kernel APIs
- validate ID, VERSION, and FIFO_DEPTH at probe time
- request an IRQ
- register a character device as `/dev/qemu_mbox0`
- implement blocking and non-blocking `read()` and `write()`
- implement `poll()` wakeups from the IRQ path
- expose ioctl commands for reset, status, stats, and mode
- expose debugfs state for development and troubleshooting

## Concurrency Model

The driver will need one per-device state object. That object should contain the
mapped register base, IRQ number, cdev state, wait queues, locks, and software
stats.

Expected rules:

- Register access stays serialized where state transitions matter.
- Wait queues are used for blocking reads and writes.
- The hard IRQ handler does only minimal acknowledgement.
- A threaded IRQ handler performs wakeups and stats updates.
- File operations validate user pointers and handle `O_NONBLOCK` correctly.

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

- opening `/dev/qemu_mbox0`
- choosing blocking or non-blocking mode
- using `poll()` for event-driven I/O
- interpreting ioctl results

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
