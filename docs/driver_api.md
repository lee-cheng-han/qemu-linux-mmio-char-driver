# Driver API

The final Linux driver will expose one character device:

```text
/dev/qemu_mbox0
```

The device represents a byte-oriented mailbox backed by the QEMU MMIO register
interface.

## File Operations

Planned operations:

| Operation | Behavior |
|---|---|
| `open()` | Attach a file to the device state. |
| `release()` | Drop the file reference. |
| `read()` | Copy bytes from the RX FIFO to userspace. |
| `write()` | Copy bytes from userspace into the TX FIFO. |
| `poll()` | Report RX readiness and TX space. |
| `unlocked_ioctl()` | Handle reset, status, stats, and mode commands. |
| `llseek()` | Use `no_llseek`. |

## Blocking Rules

`read()` behavior:

- If RX data exists, return available bytes.
- If RX is empty and the file is blocking, sleep until IRQ-driven wakeup.
- If RX is empty and `O_NONBLOCK` is set, return `-EAGAIN`.

`write()` behavior:

- If TX space exists, write as many bytes as fit.
- If TX is full and the file is blocking, sleep until IRQ-driven wakeup.
- If TX is full and `O_NONBLOCK` is set, return `-EAGAIN`.

Short reads and writes are acceptable when the hardware FIFO cannot satisfy the
full request immediately.

## Poll Semantics

Planned `poll()` readiness:

| Condition | Mask |
|---|---|
| RX data available | `POLLIN | POLLRDNORM` |
| TX space available | `POLLOUT | POLLWRNORM` |
| Device error | `POLLERR` |

The driver should register wait queues before checking readiness so it cannot
miss a wakeup.

## Ioctl Plan

The UAPI header will live under `kernel/include/uapi/linux/` once the kernel
driver starts.

Planned commands:

| Command | Direction | Purpose |
|---|---:|---|
| `QEMU_MBOX_IOC_RESET` | none | Reset hardware and driver stats. |
| `QEMU_MBOX_IOC_GET_STATUS` | read | Return current hardware status. |
| `QEMU_MBOX_IOC_GET_STATS` | read | Return driver-visible counters. |
| `QEMU_MBOX_IOC_SET_MODE` | write | Configure supported driver mode bits. |

Planned UAPI shapes:

```c
#define QEMU_MBOX_IOC_MAGIC 'q'

struct qemu_mbox_status {
    __u32 control;
    __u32 status;
    __u32 irq_status;
    __u32 irq_enable;
    __u32 tx_count;
    __u32 rx_count;
    __u32 fifo_depth;
    __u32 reserved;
};

struct qemu_mbox_stats {
    __u64 bytes_read;
    __u64 bytes_written;
    __u64 read_wakeups;
    __u64 write_wakeups;
    __u64 irqs;
    __u64 errors;
};

struct qemu_mbox_mode {
    __u32 flags;
    __u32 reserved;
};
```

Validation requirements:

- reject unknown commands with `-ENOTTY`
- reject invalid mode values with `-EINVAL`
- reject invalid userspace pointers with `-EFAULT`
- keep UAPI structs fixed-width and naturally aligned
- reserve fields for future extension

## Driver State

The driver should use one per-device state object. Planned fields:

- `struct device *dev`
- mapped MMIO base pointer
- IRQ number
- `struct cdev`
- device number
- wait queue for RX readiness
- wait queue for TX space
- lock for driver state and stats
- cached FIFO depth
- stats counters

The driver should avoid global mutable state except for class/device-number
allocation needed by the character device framework.

## Error Mapping

Expected userspace-facing errors:

| Condition | Error |
|---|---:|
| RX empty in non-blocking read | `-EAGAIN` |
| TX full in non-blocking write | `-EAGAIN` |
| Interrupted blocking operation | `-ERESTARTSYS` |
| Bad userspace pointer | `-EFAULT` |
| Unknown ioctl | `-ENOTTY` |
| Invalid ioctl argument | `-EINVAL` |
| Device removed or unavailable | `-ENODEV` |

## Debugfs Plan

Development builds should expose debugfs entries under:

```text
/sys/kernel/debug/qemu_mbox0/
```

Planned entries:

- `stats`
- `regs`
- `fifo`

Debugfs is diagnostic only and must not be required for normal operation.
