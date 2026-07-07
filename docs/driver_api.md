# Driver API

The final Linux driver will expose one character device:

```text
/dev/vmbox0
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

The initial implementation should allow only one open file at a time. A second
open should fail with `-EBUSY`. This is the documented policy for the first
version because the device exposes one shared FIFO pair.

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

The UAPI header lives under:

```text
kernel/include/uapi/linux/vmbox.h
```

Implemented commands:

| Command | Direction | Purpose |
|---|---:|---|
| `VMBOX_IOC_RESET` | none | Reset hardware and driver stats. |
| `VMBOX_IOC_GET_STATUS` | read | Return current hardware status. |
| `VMBOX_IOC_GET_STATS` | read | Return driver-visible counters. |
| `VMBOX_IOC_SET_MODE` | write | Configure supported driver mode bits. |

UAPI shapes:

```c
#define VMBOX_IOC_MAGIC 'v'

struct vmbox_status {
    __u32 control;
    __u32 status;
    __u32 irq_status;
    __u32 irq_enable;
    __u32 tx_count;
    __u32 rx_count;
    __u32 fifo_depth;
    __u32 reserved;
};

struct vmbox_stats {
    __u64 bytes_read;
    __u64 bytes_written;
    __u64 irqs;
    __u64 errors;
    __u64 errors_irq_spurious;
    __u64 errors_fifo_overrun;
    __u64 errors_fifo_underrun;
    __u64 tx_full_events;
    __u64 rx_empty_events;
};

struct vmbox_mode {
    __u32 flags;
    __u32 reserved;
};
```

The driver includes compile-time ABI size guards so accidental struct growth
cannot silently break userspace ABI expectations:

```c
static_assert(sizeof(struct vmbox_status) == 32);
static_assert(sizeof(struct vmbox_stats) == 72);
static_assert(sizeof(struct vmbox_mode) == 8);
```

These numeric sizes are now tied to the implemented UAPI layout.

Validation requirements:

- reject unknown commands with `-ENOTTY`
- reject bad ioctl magic with `-ENOTTY`
- reject invalid mode values with `-EINVAL`
- reject invalid userspace pointers with `-EFAULT`
- reject nonzero reserved fields with `-EINVAL`
- reject invalid struct sizes if size-dependent commands are added
- keep UAPI structs fixed-width and naturally aligned
- reserve fields for future extension

The driver implements `.compat_ioctl` under `CONFIG_COMPAT` by reusing the
native ioctl handler because all UAPI structs use fixed-width integer fields and
contain no embedded userspace pointers.

Source comment:

```c
/*
 * Safe to reuse the native ioctl handler: all UAPI structs use
 * fixed-width integer fields and contain no embedded userspace pointers.
 */
```

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
- `device_gone` lifetime flag
- reference object such as `kref` or `refcount_t`

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
/sys/kernel/debug/vmbox0/
```

Planned entries:

- `stats`
- `regs`
- `fifo`

Debugfs is diagnostic only and must not be required for normal operation.

## Sysfs Plan

The driver should also expose a minimal stable sysfs attribute group under the
platform device. Unlike debugfs, sysfs is a stable user-visible ABI and should
only expose simple, documented values that are safe to support long term.

Planned read-only attributes:

- `status`
- `fifo_depth`

Sysfs is for stable inspection. Debugfs is for developer diagnostics such as raw
register dumps, FIFO internals, and verbose counters.

The `VMBOX_IOC_GET_STATS` ioctl and debugfs `stats` file should both expose:

- `bytes_read`
- `bytes_written`
- `irqs`
- `errors`
- `errors_irq_spurious`
- `errors_fifo_overrun`
- `errors_fifo_underrun`
- `tx_full_events`
- `rx_empty_events`

## Access And Permissions

Tests should not rely on running as root. The planned udev rule is:

```text
scripts/udev/99-vmbox.rules
```

Expected local setup:

```sh
sudo install -m 0644 scripts/udev/99-vmbox.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The rule should grant group access to `/dev/vmbox0`; the documented test user
must belong to that group.
