/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Virtual Mailbox MMIO Device
 *
 * FIFO-backed MMIO register model for a small virtual mailbox device.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/misc/qemu_mbox.h"

static void qemu_mbox_schedule_processing(QemuMboxState *s);
static void qemu_mbox_update_irq(QemuMboxState *s);

static uint8_t qemu_mbox_process_byte(uint8_t value)
{
    /*
     * Deterministic test transform: lowercase ASCII input is returned as
     * uppercase, while all other byte values are preserved.
     */
    if (value >= 'a' && value <= 'z') {
        value = value - 'a' + 'A';
    }

    return value;
}

static void qemu_mbox_update_status(QemuMboxState *s)
{
    s->status &= ~(QEMU_MBOX_STATUS_RX_READY |
                   QEMU_MBOX_STATUS_TX_FULL |
                   QEMU_MBOX_STATUS_RX_FULL);

    if (s->rx_count > 0) {
        s->status |= QEMU_MBOX_STATUS_RX_READY;
    }

    if (s->tx_count == QEMU_MBOX_FIFO_DEPTH) {
        s->status |= QEMU_MBOX_STATUS_TX_FULL;
    }

    if (s->rx_count == QEMU_MBOX_FIFO_DEPTH) {
        s->status |= QEMU_MBOX_STATUS_RX_FULL;
    }
}

static void qemu_mbox_update_irq(QemuMboxState *s)
{
    bool level = (s->control & QEMU_MBOX_CTRL_IRQ_ENABLE) &&
                 (s->irq_status & s->irq_enable);

    if (s->irq) {
        qemu_set_irq(s->irq, level);
    }
}

static void qemu_mbox_raise_irq(QemuMboxState *s, uint32_t events)
{
    s->irq_status |= events & QEMU_MBOX_IRQ_VALID_MASK;
    qemu_mbox_update_irq(s);
}

/*
 * FIFO helper convention:
 *
 * - called from the QEMU device context
 * - safe to call from a QEMU timer callback in the same main-loop context
 * - no pthread locking is needed unless the device moves to an
 *   IOThread or another concurrent host execution context
 * - helpers update only FIFO storage/index/count state; status, IRQ, and timer
 *   decisions stay in separate helpers
 */
static bool qemu_mbox_fifo_push(uint8_t *fifo, uint32_t *tail,
                                uint32_t *count, uint8_t value)
{
    if (*count == QEMU_MBOX_FIFO_DEPTH) {
        return false;
    }

    fifo[*tail] = value;
    *tail = (*tail + 1) % QEMU_MBOX_FIFO_DEPTH;
    (*count)++;

    return true;
}

static bool qemu_mbox_fifo_pop(uint8_t *fifo, uint32_t *head,
                               uint32_t *count, uint8_t *value)
{
    if (*count == 0) {
        return false;
    }

    *value = fifo[*head];
    *head = (*head + 1) % QEMU_MBOX_FIFO_DEPTH;
    (*count)--;

    return true;
}

static void qemu_mbox_process_one(QemuMboxState *s)
{
    uint32_t events = 0;
    bool was_tx_full;
    uint8_t value;

    if (s->tx_count == 0 || s->rx_count == QEMU_MBOX_FIFO_DEPTH) {
        return;
    }

    was_tx_full = s->tx_count == QEMU_MBOX_FIFO_DEPTH;
    qemu_mbox_fifo_pop(s->tx_fifo, &s->tx_head, &s->tx_count, &value);
    value = qemu_mbox_process_byte(value);
    qemu_mbox_fifo_push(s->rx_fifo, &s->rx_tail, &s->rx_count, value);

    events |= QEMU_MBOX_IRQ_RX_READY;

    if (was_tx_full) {
        events |= QEMU_MBOX_IRQ_TX_SPACE;
    }

    if (s->tx_count == 0) {
        events |= QEMU_MBOX_IRQ_DONE;
    }

    qemu_mbox_raise_irq(s, events);
}

static void qemu_mbox_process_timer(void *opaque)
{
    QemuMboxState *s = opaque;

    s->processing = false;
    qemu_mbox_process_one(s);
    qemu_mbox_schedule_processing(s);
}

static void qemu_mbox_schedule_processing(QemuMboxState *s)
{
    if (s->tx_count == 0 || s->rx_count == QEMU_MBOX_FIFO_DEPTH) {
        s->processing = false;
        s->status &= ~QEMU_MBOX_STATUS_BUSY;
        qemu_mbox_update_status(s);
        return;
    }

    if (!s->process_timer || s->processing) {
        qemu_mbox_update_status(s);
        return;
    }

    s->processing = true;
    s->status |= QEMU_MBOX_STATUS_BUSY;
    timer_mod(s->process_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              QEMU_MBOX_PROCESS_DELAY_NS);
    qemu_mbox_update_status(s);
}

static void qemu_mbox_reset_regs(QemuMboxState *s)
{
    if (s->process_timer) {
        timer_del(s->process_timer);
    }

    s->control = 0;
    s->status = 0;
    s->irq_status = 0;
    s->irq_enable = 0;
    memset(s->tx_fifo, 0, sizeof(s->tx_fifo));
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
    s->tx_head = 0;
    s->tx_tail = 0;
    s->tx_count = 0;
    s->rx_head = 0;
    s->rx_tail = 0;
    s->rx_count = 0;
    s->processing = false;
    qemu_mbox_update_irq(s);
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
        qemu_mbox_update_status(s);
        return s->status;

    case QEMU_MBOX_REG_RX_DATA:
    {
        uint8_t value;

        if (!qemu_mbox_fifo_pop(s->rx_fifo, &s->rx_head, &s->rx_count,
                                &value)) {
            qemu_mbox_update_status(s);
            return 0;
        }

        qemu_mbox_schedule_processing(s);
        return value;
    }

    case QEMU_MBOX_REG_IRQ_STATUS:
        return s->irq_status;

    case QEMU_MBOX_REG_IRQ_ENABLE:
        return s->irq_enable;

    case QEMU_MBOX_REG_TX_COUNT:
        return s->tx_count;

    case QEMU_MBOX_REG_RX_COUNT:
        return s->rx_count;

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
        qemu_mbox_update_irq(s);
        break;

    case QEMU_MBOX_REG_TX_DATA:
        if (!qemu_mbox_fifo_push(s->tx_fifo, &s->tx_tail, &s->tx_count,
                                 value & 0xff)) {
            s->status |= QEMU_MBOX_STATUS_ERROR;
            qemu_mbox_raise_irq(s, QEMU_MBOX_IRQ_ERROR);
            qemu_mbox_update_status(s);
            break;
        }

        qemu_mbox_schedule_processing(s);
        break;

    case QEMU_MBOX_REG_IRQ_STATUS:
        /* Write-one-to-clear behavior. */
        s->irq_status &= ~(value & QEMU_MBOX_IRQ_VALID_MASK);
        qemu_mbox_update_irq(s);
        break;

    case QEMU_MBOX_REG_IRQ_ENABLE:
        s->irq_enable = value & QEMU_MBOX_IRQ_VALID_MASK;
        qemu_mbox_update_irq(s);
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

static int qemu_mbox_post_load(void *opaque, int version_id)
{
    QemuMboxState *s = opaque;

    qemu_mbox_update_status(s);
    qemu_mbox_update_irq(s);

    return 0;
}

static const VMStateDescription vmstate_qemu_mbox = {
    .name = TYPE_QEMU_MBOX,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = qemu_mbox_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, QemuMboxState),
        VMSTATE_UINT32(status, QemuMboxState),
        VMSTATE_UINT32(irq_status, QemuMboxState),
        VMSTATE_UINT32(irq_enable, QemuMboxState),
        VMSTATE_UINT8_ARRAY(tx_fifo, QemuMboxState, QEMU_MBOX_FIFO_DEPTH),
        VMSTATE_UINT8_ARRAY(rx_fifo, QemuMboxState, QEMU_MBOX_FIFO_DEPTH),
        VMSTATE_UINT32(tx_head, QemuMboxState),
        VMSTATE_UINT32(tx_tail, QemuMboxState),
        VMSTATE_UINT32(tx_count, QemuMboxState),
        VMSTATE_UINT32(rx_head, QemuMboxState),
        VMSTATE_UINT32(rx_tail, QemuMboxState),
        VMSTATE_UINT32(rx_count, QemuMboxState),
        VMSTATE_TIMER_PTR(process_timer, QemuMboxState),
        VMSTATE_BOOL(processing, QemuMboxState),
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
    sysbus_init_irq(sbd, &s->irq);

    s->process_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    qemu_mbox_process_timer,
                                    s);
    qemu_mbox_reset_regs(s);
}

static void qemu_mbox_finalize(Object *obj)
{
    QemuMboxState *s = QEMU_MBOX(obj);

    if (s->process_timer) {
        timer_free(s->process_timer);
        s->process_timer = NULL;
    }
}

static void qemu_mbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Virtual mailbox MMIO device";
    dc->vmsd = &vmstate_qemu_mbox;

    device_class_set_legacy_reset(dc, qemu_mbox_reset);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo qemu_mbox_info = {
    .name = TYPE_QEMU_MBOX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QemuMboxState),
    .instance_init = qemu_mbox_init,
    .instance_finalize = qemu_mbox_finalize,
    .class_init = qemu_mbox_class_init,
};

static void qemu_mbox_register_types(void)
{
    type_register_static(&qemu_mbox_info);
}

type_init(qemu_mbox_register_types)
