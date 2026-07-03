/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu_mbox QTest skeleton.
 *
 * This file is intended to be copied into QEMU as tests/qtest/qemu_mbox-test.c
 * after the qemu-mbox device is wired into a machine or QTest harness.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define QEMU_MBOX_TEST_MACHINE "qemu-mbox-test-machine"
#define QEMU_MBOX_TEST_BASE    0x10000000U

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
#define QEMU_MBOX_STATUS_RX_READY (1U << 1)
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

static void test_qemu_mbox_temporary_tx_rx(void)
{
    qemu_mbox_reset();

    qemu_mbox_writel(QEMU_MBOX_REG_TX_DATA, 'h');

    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_TX_COUNT), ==, 1);
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
    g_assert_cmpuint(qemu_mbox_readl(QEMU_MBOX_REG_RX_DATA), ==, 0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine " QEMU_MBOX_TEST_MACHINE);

    qtest_add_func("/qemu_mbox/id_version", test_qemu_mbox_id_version);
    qtest_add_func("/qemu_mbox/reset_state", test_qemu_mbox_reset_state);
    qtest_add_func("/qemu_mbox/temporary_tx_rx",
                   test_qemu_mbox_temporary_tx_rx);
    qtest_add_func("/qemu_mbox/zero_byte_tx_count",
                   test_qemu_mbox_zero_byte_tx_count);

    ret = g_test_run();
    qtest_end();

    return ret;
}
