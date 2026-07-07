// SPDX-License-Identifier: GPL-2.0-only
/*
 * QEMU virtual mailbox platform driver.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#define VMBOX_DRV_NAME "vmbox"

#define VMBOX_MMIO_SIZE       0x1000
#define VMBOX_FIFO_DEPTH      16

#define VMBOX_ID_VALUE        0x514d424fU
#define VMBOX_VERSION_VALUE   0x00010000U

#define VMBOX_REG_ID          0x00
#define VMBOX_REG_VERSION     0x04
#define VMBOX_REG_CONTROL     0x08
#define VMBOX_REG_STATUS      0x0c
#define VMBOX_REG_IRQ_STATUS  0x18
#define VMBOX_REG_IRQ_ENABLE  0x1c
#define VMBOX_REG_FIFO_DEPTH  0x28
#define VMBOX_REG_RESET       0x2c

#define VMBOX_CTRL_ENABLE     BIT(0)
#define VMBOX_CTRL_IRQ_ENABLE BIT(3)

#define VMBOX_IRQ_RX_READY    BIT(0)
#define VMBOX_IRQ_TX_SPACE    BIT(1)
#define VMBOX_IRQ_ERROR       BIT(2)
#define VMBOX_IRQ_DONE        BIT(3)
#define VMBOX_IRQ_VALID_MASK  (VMBOX_IRQ_RX_READY | \
			       VMBOX_IRQ_TX_SPACE | \
			       VMBOX_IRQ_ERROR | \
			       VMBOX_IRQ_DONE)

struct vmbox_dev {
	struct device *dev;
	void __iomem *regs;
	int irq;
	u32 fifo_depth;
};

static u32 vmbox_readl(struct vmbox_dev *vmbox, u32 reg)
{
	return readl(vmbox->regs + reg);
}

static void vmbox_writel(struct vmbox_dev *vmbox, u32 reg, u32 value)
{
	writel(value, vmbox->regs + reg);
}

static void vmbox_hw_reset(struct vmbox_dev *vmbox)
{
	vmbox_writel(vmbox, VMBOX_REG_RESET, 1);
}

static int vmbox_hw_validate(struct vmbox_dev *vmbox)
{
	u32 id;
	u32 version;
	u32 fifo_depth;

	id = vmbox_readl(vmbox, VMBOX_REG_ID);
	if (id != VMBOX_ID_VALUE) {
		dev_err(vmbox->dev, "unexpected device id 0x%08x\n", id);
		return -ENODEV;
	}

	version = vmbox_readl(vmbox, VMBOX_REG_VERSION);
	if (version != VMBOX_VERSION_VALUE) {
		dev_err(vmbox->dev, "unsupported version 0x%08x\n", version);
		return -ENODEV;
	}

	fifo_depth = vmbox_readl(vmbox, VMBOX_REG_FIFO_DEPTH);
	if (fifo_depth != VMBOX_FIFO_DEPTH) {
		dev_err(vmbox->dev, "unsupported fifo depth %u\n", fifo_depth);
		return -ENODEV;
	}

	vmbox->fifo_depth = fifo_depth;

	return 0;
}

static irqreturn_t vmbox_irq(int irq, void *data)
{
	struct vmbox_dev *vmbox = data;
	u32 irq_status;

	irq_status = vmbox_readl(vmbox, VMBOX_REG_IRQ_STATUS);
	irq_status &= VMBOX_IRQ_VALID_MASK;
	if (!irq_status)
		return IRQ_NONE;

	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, irq_status);

	return IRQ_HANDLED;
}

static int vmbox_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct vmbox_dev *vmbox;
	int ret;

	vmbox = devm_kzalloc(&pdev->dev, sizeof(*vmbox), GFP_KERNEL);
	if (!vmbox)
		return -ENOMEM;

	vmbox->dev = &pdev->dev;
	platform_set_drvdata(pdev, vmbox);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (resource_size(res) < VMBOX_MMIO_SIZE) {
		dev_err(&pdev->dev, "MMIO resource too small: %pr\n", res);
		return -ENODEV;
	}

	vmbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vmbox->regs))
		return PTR_ERR(vmbox->regs);

	vmbox->irq = platform_get_irq(pdev, 0);
	if (vmbox->irq < 0)
		return vmbox->irq;

	ret = vmbox_hw_validate(vmbox);
	if (ret)
		return ret;

	vmbox_hw_reset(vmbox);

	ret = devm_request_irq(&pdev->dev, vmbox->irq, vmbox_irq, 0,
			       dev_name(&pdev->dev), vmbox);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d: %d\n",
			vmbox->irq, ret);
		return ret;
	}

	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, VMBOX_IRQ_VALID_MASK);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_ENABLE, VMBOX_IRQ_VALID_MASK);
	vmbox_writel(vmbox, VMBOX_REG_CONTROL,
		     VMBOX_CTRL_ENABLE | VMBOX_CTRL_IRQ_ENABLE);

	dev_info(&pdev->dev, "probed fifo_depth=%u irq=%d\n",
		 vmbox->fifo_depth, vmbox->irq);

	return 0;
}

static void vmbox_remove(struct platform_device *pdev)
{
	struct vmbox_dev *vmbox = platform_get_drvdata(pdev);

	vmbox_writel(vmbox, VMBOX_REG_CONTROL, 0);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_ENABLE, 0);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, VMBOX_IRQ_VALID_MASK);
}

static const struct of_device_id vmbox_of_match[] = {
	{ .compatible = "virt,mbox" },
	{ }
};
MODULE_DEVICE_TABLE(of, vmbox_of_match);

static struct platform_driver vmbox_driver = {
	.probe = vmbox_probe,
	.remove = vmbox_remove,
	.driver = {
		.name = VMBOX_DRV_NAME,
		.of_match_table = vmbox_of_match,
	},
};
module_platform_driver(vmbox_driver);

MODULE_AUTHOR("Lee Cheng Han <lee-cheng-han@users.noreply.github.com>");
MODULE_DESCRIPTION("QEMU virtual mailbox platform driver");
MODULE_LICENSE("GPL");
