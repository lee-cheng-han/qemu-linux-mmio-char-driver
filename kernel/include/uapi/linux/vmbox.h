/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_VMBOX_H
#define _UAPI_LINUX_VMBOX_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VMBOX_IOC_MAGIC 'v'

#define VMBOX_MODE_FLAGS_MASK 0

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

#define VMBOX_IOC_RESET      _IO(VMBOX_IOC_MAGIC, 0x00)
#define VMBOX_IOC_GET_STATUS _IOR(VMBOX_IOC_MAGIC, 0x01, struct vmbox_status)
#define VMBOX_IOC_GET_STATS  _IOR(VMBOX_IOC_MAGIC, 0x02, struct vmbox_stats)
#define VMBOX_IOC_SET_MODE   _IOW(VMBOX_IOC_MAGIC, 0x03, struct vmbox_mode)

#endif /* _UAPI_LINUX_VMBOX_H */
