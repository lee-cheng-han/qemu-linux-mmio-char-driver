/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Mailbox MMIO Device
 *
 * Minimal memory-mapped mailbox/FIFO-style device model used for an
 * embedded Linux character driver project.
 */

#ifndef HW_MISC_QEMU_MBOX_H
#define HW_MISC_QEMU_MBOX_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_QEMU_MBOX "qemu-mbox"
OBJECT_DECLARE_SIMPLE_TYPE(QemuMboxState, QEMU_MBOX)

#define QEMU_MBOX_MMIO_SIZE       0x1000
#define QEMU_MBOX_FIFO_DEPTH      16

#define QEMU_MBOX_ID_VALUE        0x514d424fU  /* "QMBO" */
#define QEMU_MBOX_VERSION_VALUE   0x00010000U  /* v1.0 */

/* Register offsets */
#define QEMU_MBOX_REG_ID          0x00
#define QEMU_MBOX_REG_VERSION     0x04
#define QEMU_MBOX_REG_CONTROL     0x08
#define QEMU_MBOX_REG_STATUS      0x0c
#define QEMU_MBOX_REG_TX_DATA     0x10
#define QEMU_MBOX_REG_RX_DATA     0x14
#define QEMU_MBOX_REG_IRQ_STATUS  0x18
#define QEMU_MBOX_REG_IRQ_ENABLE  0x1c
#define QEMU_MBOX_REG_TX_COUNT    0x20
#define QEMU_MBOX_REG_RX_COUNT    0x24
#define QEMU_MBOX_REG_FIFO_DEPTH  0x28
#define QEMU_MBOX_REG_RESET       0x2c

/* CONTROL bits */
#define QEMU_MBOX_CTRL_ENABLE     (1U << 0)
#define QEMU_MBOX_CTRL_RESET      (1U << 1)
#define QEMU_MBOX_CTRL_LOOPBACK   (1U << 2)
#define QEMU_MBOX_CTRL_IRQ_ENABLE (1U << 3)

#define QEMU_MBOX_CTRL_VALID_MASK \
    (QEMU_MBOX_CTRL_ENABLE |      \
     QEMU_MBOX_CTRL_RESET |       \
     QEMU_MBOX_CTRL_LOOPBACK |    \
     QEMU_MBOX_CTRL_IRQ_ENABLE)

/* STATUS bits */
#define QEMU_MBOX_STATUS_BUSY     (1U << 0)
#define QEMU_MBOX_STATUS_RX_READY (1U << 1)
#define QEMU_MBOX_STATUS_TX_FULL  (1U << 2)
#define QEMU_MBOX_STATUS_RX_FULL  (1U << 3)
#define QEMU_MBOX_STATUS_ERROR    (1U << 4)

/* IRQ bits */
#define QEMU_MBOX_IRQ_RX_READY    (1U << 0)
#define QEMU_MBOX_IRQ_TX_SPACE    (1U << 1)
#define QEMU_MBOX_IRQ_ERROR       (1U << 2)
#define QEMU_MBOX_IRQ_DONE        (1U << 3)

#define QEMU_MBOX_IRQ_VALID_MASK  \
    (QEMU_MBOX_IRQ_RX_READY |     \
     QEMU_MBOX_IRQ_TX_SPACE |     \
     QEMU_MBOX_IRQ_ERROR |        \
     QEMU_MBOX_IRQ_DONE)

struct QemuMboxState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t control;
    uint32_t status;
    uint32_t irq_status;
    uint32_t irq_enable;

    /*
     * Milestone 2 has no real FIFO yet.
     * These fields provide deterministic TX/RX register behavior.
     */
    uint32_t last_tx_data;
    uint32_t last_rx_data;
    uint32_t last_tx_valid;

    uint64_t mmio_read_count;
    uint64_t mmio_write_count;
};

#endif /* HW_MISC_QEMU_MBOX_H */
