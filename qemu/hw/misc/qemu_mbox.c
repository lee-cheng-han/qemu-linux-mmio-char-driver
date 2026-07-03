/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Mailbox MMIO Device
 *
 * Minimal MMIO register model for qemu_mbox.
 *
 * Milestone 2 scope:
 * - expose ID/VERSION registers
 * - expose CONTROL/STATUS registers
 * - support basic TX_DATA/RX_DATA behavior
 * - support RESET register
 * - support IRQ_STATUS/IRQ_ENABLE storage only
 *
 * Not implemented yet:
 * - real TX/RX FIFO
 * - processing timer
 * - interrupt line
 * - QOM debug properties
 * - qtest file
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/qemu_mbox.h"

static uint32_t qemu_mbox_process_byte(uint32_t value)
{
    uint8_t byte = value & 0xff;

    /*
     * Temporary Milestone 2 behavior:
     * lowercase ASCII input is converted to uppercase.
     *
     * Later milestones will move this into delayed FIFO processing.
     */
    if (byte >= 'a' && byte <= 'z') {
        byte = byte - 'a' + 'A';
    }

    return byte;
}

static void qemu_mbox_reset_regs(QemuMboxState *s)
{
    s->control = 0;
    s->status = 0;
    s->irq_status = 0;
    s->irq_enable = 0;
    s->last_tx_data = 0;
    s->last_rx_data = 0;
    s->last_tx_valid = 0;
}

static uint64_t qemu_mbox_read(void *opaque, hwaddr offset, unsigned size)
{
    QemuMboxState *s = opaque;

    s->mmio_read_count++;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read size %u at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_QEMU_MBOX, size, offset);
        return 0;
    }

    switch (offset) {
    case QEMU_MBOX_REG_ID:
        return QEMU_MBOX_ID_VALUE;

    case QEMU_MBOX_REG_VERSION:
        return QEMU_MBOX_VERSION_VALUE;

    case QEMU_MBOX_REG_CONTROL:
        return s->control;

    case QEMU_MBOX_REG_STATUS:
        return s->status;

    case QEMU_MBOX_REG_RX_DATA:
        /*
         * Milestone 2 has no RX FIFO yet.
         * Reading RX_DATA returns the last processed byte and clears RX_READY.
         */
        if (!(s->status & QEMU_MBOX_STATUS_RX_READY)) {
            return 0;
        }

        s->status &= ~QEMU_MBOX_STATUS_RX_READY;
        return s->last_rx_data;

    case QEMU_MBOX_REG_IRQ_STATUS:
        return s->irq_status;

    case QEMU_MBOX_REG_IRQ_ENABLE:
        return s->irq_enable;

    case QEMU_MBOX_REG_TX_COUNT:
        return s->last_tx_valid ? 1 : 0;

    case QEMU_MBOX_REG_RX_COUNT:
        return (s->status & QEMU_MBOX_STATUS_RX_READY) ? 1 : 0;

    case QEMU_MBOX_REG_FIFO_DEPTH:
        return QEMU_MBOX_FIFO_DEPTH;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_QEMU_MBOX, offset);
        return 0;
    }
}

static void qemu_mbox_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    QemuMboxState *s = opaque;

    s->mmio_write_count++;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write size %u at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_QEMU_MBOX, size, offset);
        return;
    }

    switch (offset) {
    case QEMU_MBOX_REG_CONTROL:
        /*
         * RESET is self-clearing.
         * Other supported control bits are stored.
         */
        if (value & QEMU_MBOX_CTRL_RESET) {
            qemu_mbox_reset_regs(s);
        }

        s->control = value & (QEMU_MBOX_CTRL_ENABLE |
                              QEMU_MBOX_CTRL_LOOPBACK |
                              QEMU_MBOX_CTRL_IRQ_ENABLE);
        break;

    case QEMU_MBOX_REG_TX_DATA:
        /*
         * Milestone 2 has no real TX/RX FIFO yet.
         * Store one byte, process it immediately, and mark RX ready.
         */
        s->last_tx_data = value & 0xff;
        s->last_tx_valid = 1;
        s->last_rx_data = qemu_mbox_process_byte(s->last_tx_data);
        s->status |= QEMU_MBOX_STATUS_RX_READY;
        break;

    case QEMU_MBOX_REG_IRQ_STATUS:
        /*
         * Write-one-to-clear behavior.
         * Real IRQ line handling is added in a later milestone.
         */
        s->irq_status &= ~(value & QEMU_MBOX_IRQ_VALID_MASK);
        break;

    case QEMU_MBOX_REG_IRQ_ENABLE:
        s->irq_enable = value & QEMU_MBOX_IRQ_VALID_MASK;
        break;

    case QEMU_MBOX_REG_RESET:
        if (value) {
            qemu_mbox_reset_regs(s);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write at offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      TYPE_QEMU_MBOX, offset, value);
        break;
    }
}

static const MemoryRegionOps qemu_mbox_ops = {
    .read = qemu_mbox_read,
    .write = qemu_mbox_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qemu_mbox_reset(DeviceState *dev)
{
    QemuMboxState *s = QEMU_MBOX(dev);

    qemu_mbox_reset_regs(s);
}

static const VMStateDescription vmstate_qemu_mbox = {
    .name = TYPE_QEMU_MBOX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, QemuMboxState),
        VMSTATE_UINT32(status, QemuMboxState),
        VMSTATE_UINT32(irq_status, QemuMboxState),
        VMSTATE_UINT32(irq_enable, QemuMboxState),
        VMSTATE_UINT32(last_tx_data, QemuMboxState),
        VMSTATE_UINT32(last_rx_data, QemuMboxState),
        VMSTATE_UINT32(last_tx_valid, QemuMboxState),
        VMSTATE_UINT64(mmio_read_count, QemuMboxState),
        VMSTATE_UINT64(mmio_write_count, QemuMboxState),
        VMSTATE_END_OF_LIST()
    }
};

static void qemu_mbox_init(Object *obj)
{
    QemuMboxState *s = QEMU_MBOX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio,
                          obj,
                          &qemu_mbox_ops,
                          s,
                          TYPE_QEMU_MBOX,
                          QEMU_MBOX_MMIO_SIZE);

    sysbus_init_mmio(sbd, &s->mmio);

    qemu_mbox_reset_regs(s);
}

static void qemu_mbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "QEMU mailbox MMIO device";
    dc->vmsd = &vmstate_qemu_mbox;

    device_class_set_legacy_reset(dc, qemu_mbox_reset);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo qemu_mbox_info = {
    .name = TYPE_QEMU_MBOX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QemuMboxState),
    .instance_init = qemu_mbox_init,
    .class_init = qemu_mbox_class_init,
};

static void qemu_mbox_register_types(void)
{
    type_register_static(&qemu_mbox_info);
}

type_init(qemu_mbox_register_types)
