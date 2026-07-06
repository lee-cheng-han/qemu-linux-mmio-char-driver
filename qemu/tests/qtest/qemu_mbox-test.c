/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * virt-mbox QTest skeleton.
 *
 * This file is intended to be copied into QEMU as tests/qtest/qemu_mbox-test.c
 * after the virt-mbox device is wired into a machine or QTest harness.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define QEMU_MBOX_TEST_MACHINE "virt-mbox-test-machine"
#define QEMU_MBOX_TEST_BASE    0x10000000U
#define QEMU_MBOX_PROCESS_DELAY_NS 1000000ULL

#define QEMU_MBOX_ID_VALUE        0x514d424fU
#define QEMU_MBOX_VERSION_VALUE   0x00010000U
#define QEMU_MBOX_FIFO_DEPTH      16U

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

#define QEMU_MBOX_CTRL_ENABLE     (1U << 0)
#define QEMU_MBOX_CTRL_IRQ_ENABLE (1U << 3)
#define QEMU_MBOX_STATUS_BUSY     (1U << 0)
#define QEMU_MBOX_STATUS_RX_READY (1U << 1)
#define QEMU_MBOX_STATUS_TX_FULL  (1U << 2)
#define QEMU_MBOX_STATUS_RX_FULL  (1U << 3)
#define QEMU_MBOX_STATUS_ERROR    (1U << 4)
#define QEMU_MBOX_IRQ_RX_READY    (1U << 0)

static uint32_t qemu_mbox_readl(uint32_t reg)
{
    return readl(QEMU_MBOX_TEST_BASE + reg);
}

static void qemu_mbox_writel(uint32_t reg, uint32_t value)
{
    writel(QEMU_MBOX_TEST_BASE + reg, value);
}

static void qemu_mbox_reset(void)
{
    qemu_mbox_writel(QEMU_MBOX_REG_RESET, 1);
}

static void qemu_mbox_step_processing(void)
{
    clock_step(QEMU_MBOX_PROCESS_DELAY_NS);
}

static void test_qemu_mbox_id_version(void)
{
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_ID), ==,
                    QEMU_MBOX_ID_VALUE);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_VERSION), ==,
                    QEMU_MBOX_VERSION_VALUE);
}

static void test_qemu_mbox_reset_state(void)
{
    qemu_mbox_reset();

    qemu_mbox_writel(QEMU_MBOX_REG_CONTROL,
                     QEMU_MBOX_CTRL_ENABLE | QEMU_MBOX_CTRL_IRQ_ENABLE);
    qemu_mbox_writel(QEMU_MBOX_REG_IRQ_ENABLE, QEMU_MBOX_IRQ_RX_READY);
    qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 'x');

    qemu_mbox_writel(QEMU_MBOX_REG_RESET, 1);

    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_CONTROL), ==, 0);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_STATUS), ==, 0);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_IRQ_STATUS), ==, 0);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_IRQ_ENABLE), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_FIFO_DEPTH), ==,
                     QEMU_MBOX_FIFO_DEPTH);
}

static void test_qemu_mbox_fifo_tx_rx(void)
{
    qemu_mbox_reset();

    qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 'h');

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 1);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==, 0);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_STATUS) &
                    QEMU_MBOX_STATUS_BUSY, ==,
                    QEMU_MBOX_STATUS_BUSY);

    qemu_mbox_step_processing();

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==, 1);
    g_assert_cmphex(qemu_mbox_readl(QEMU_MBOX_REG_STATUS) &
                    QEMU_MBOX_STATUS_RX_READY, ==,
                    QEMU_MBOX_STATUS_RX_READY);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_DATA), ==, 'H');
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_DATA), ==, 0);
}

static void test_qemu_mbox_zero_byte_tx_count(void)
{
    qemu_mbox_reset();

    qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 0);

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 1);
    qemu_mbox_step_processing();
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 0);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==, 1);
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_DATA), ==, 0);
}

static void test_qemu_mbox_fifo_full_status(void)
{
    uint32_t status;
    int i;

    qemu_mbox_reset();

    for (i = 0; i < QEMU_MBOX_FIFO_DEPTH; i++) {
        qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 'a' + i);
        qemu_mbox_step_processing();
    }

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_COUNT), ==,
                     QEMU_MBOX_FIFO_DEPTH);
    status = qemu_mbox_readl(QEMU_MBOX_REG_STATUS);
    g_assert_cmphex(status & QEMU_MBOX_STATUS_RX_FULL, ==,
                    QEMU_MBOX_STATUS_RX_FULL);

    for (i = 0; i < QEMU_MBOX_FIFO_DEPTH; i++) {
        qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 'A' + i);
    }

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==,
                     QEMU_MBOX_FIFO_DEPTH);
    status = qemu_mbox_readl(QEMU_MBOX_REG_STATUS);
    g_assert_cmphex(status & QEMU_MBOX_STATUS_TX_FULL, ==,
                    QEMU_MBOX_STATUS_TX_FULL);

    qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, '!');
    status = qemu_mbox_readl(QEMU_MBOX_REG_STATUS);
    g_assert_cmphex(status & QEMU_MBOX_STATUS_ERROR, ==,
                    QEMU_MBOX_STATUS_ERROR);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine " QEMU_MBOX_TEST_MACHINE);

    qtest_add_func("/vmbox/id_version", test_qemu_mbox_id_version);
    qtest_add_func("/vmbox/reset_state", test_qemu_mbox_reset_state);
    qtest_add_func("/vmbox/fifo_tx_rx", test_qemu_mbox_fifo_tx_rx);
    qtest_add_func("/vmbox/zero_byte_tx_count",
                   test_qemu_mbox_zero_byte_tx_count);
    qtest_add_func("/vmbox/fifo_full_status",
                   test_qemu_mbox_fifo_full_status);

    ret = g_test_run();
    qtest_end();

    return ret;
}
