// SPDX-License-Identifier: GPL-2.0-only
/*
 * QEMU virtual mailbox platform driver.
 */

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <uapi/linux/vmbox.h>

#define VMBOX_DRV_NAME "vmbox"
#define VMBOX_DEV_NAME "vmbox0"

#define VMBOX_MMIO_SIZE       0x1000
#define VMBOX_FIFO_DEPTH      16

#define VMBOX_ID_VALUE        0x514d424fU
#define VMBOX_VERSION_VALUE   0x00010000U

#define VMBOX_REG_ID          0x00
#define VMBOX_REG_VERSION     0x04
#define VMBOX_REG_CONTROL     0x08
#define VMBOX_REG_STATUS      0x0c
#define VMBOX_REG_TX_DATA     0x10
#define VMBOX_REG_RX_DATA     0x14
#define VMBOX_REG_IRQ_STATUS  0x18
#define VMBOX_REG_IRQ_ENABLE  0x1c
#define VMBOX_REG_TX_COUNT    0x20
#define VMBOX_REG_RX_COUNT    0x24
#define VMBOX_REG_FIFO_DEPTH  0x28
#define VMBOX_REG_RESET       0x2c

#define VMBOX_CTRL_ENABLE     BIT(0)
#define VMBOX_CTRL_IRQ_ENABLE BIT(3)

#define VMBOX_STATUS_ERROR    BIT(4)

#define VMBOX_IRQ_RX_READY    BIT(0)
#define VMBOX_IRQ_TX_SPACE    BIT(1)
#define VMBOX_IRQ_ERROR       BIT(2)
#define VMBOX_IRQ_DONE        BIT(3)
#define VMBOX_IRQ_VALID_MASK  (VMBOX_IRQ_RX_READY | \
			       VMBOX_IRQ_TX_SPACE | \
			       VMBOX_IRQ_ERROR | \
			       VMBOX_IRQ_DONE)

struct vmbox_dev {
	struct kref refcount;
	struct mutex lock;
	spinlock_t stats_lock;
	wait_queue_head_t readq;
	wait_queue_head_t writeq;
	struct device *dev;
	struct device *chrdev;
	struct cdev cdev;
	dev_t devt;
	void __iomem *regs;
	int irq;
	u32 fifo_depth;
	bool device_gone;
	bool opened;
	bool irq_requested;
	bool cdev_added;
	bool device_created;
	u32 mode_flags;
	struct vmbox_stats stats;
};

static dev_t vmbox_devt;
static struct class *vmbox_class;
static DEFINE_MUTEX(vmbox_minor_lock);
static bool vmbox_minor_in_use;

static_assert(sizeof(struct vmbox_status) == 32);
static_assert(sizeof(struct vmbox_stats) == 72);
static_assert(sizeof(struct vmbox_mode) == 8);

static void vmbox_release(struct kref *ref)
{
	struct vmbox_dev *vmbox = container_of(ref, struct vmbox_dev, refcount);

	kfree(vmbox);
}

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

static void vmbox_hw_enable(struct vmbox_dev *vmbox)
{
	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, VMBOX_IRQ_VALID_MASK);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_ENABLE, VMBOX_IRQ_VALID_MASK);
	vmbox_writel(vmbox, VMBOX_REG_CONTROL,
		     VMBOX_CTRL_ENABLE | VMBOX_CTRL_IRQ_ENABLE);
}

static void vmbox_hw_disable(struct vmbox_dev *vmbox)
{
	vmbox_writel(vmbox, VMBOX_REG_CONTROL, 0);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_ENABLE, 0);
	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, VMBOX_IRQ_VALID_MASK);
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

static bool vmbox_rx_ready(struct vmbox_dev *vmbox)
{
	return READ_ONCE(vmbox->device_gone) ||
	       (vmbox_readl(vmbox, VMBOX_REG_STATUS) & VMBOX_STATUS_ERROR) ||
	       vmbox_readl(vmbox, VMBOX_REG_RX_COUNT) > 0;
}

static bool vmbox_tx_ready(struct vmbox_dev *vmbox)
{
	return READ_ONCE(vmbox->device_gone) ||
	       (vmbox_readl(vmbox, VMBOX_REG_STATUS) & VMBOX_STATUS_ERROR) ||
	       vmbox_readl(vmbox, VMBOX_REG_TX_COUNT) < vmbox->fifo_depth;
}

static void vmbox_stats_add_read(struct vmbox_dev *vmbox, size_t count)
{
	unsigned long flags;

	spin_lock_irqsave(&vmbox->stats_lock, flags);
	vmbox->stats.bytes_read += count;
	spin_unlock_irqrestore(&vmbox->stats_lock, flags);
}

static void vmbox_stats_add_written(struct vmbox_dev *vmbox, size_t count)
{
	unsigned long flags;

	spin_lock_irqsave(&vmbox->stats_lock, flags);
	vmbox->stats.bytes_written += count;
	spin_unlock_irqrestore(&vmbox->stats_lock, flags);
}

static irqreturn_t vmbox_irq(int irq, void *data)
{
	struct vmbox_dev *vmbox = data;
	unsigned long flags;
	u32 irq_status;

	if (READ_ONCE(vmbox->device_gone)) {
		spin_lock_irqsave(&vmbox->stats_lock, flags);
		vmbox->stats.errors_irq_spurious++;
		spin_unlock_irqrestore(&vmbox->stats_lock, flags);
		return IRQ_NONE;
	}

	irq_status = vmbox_readl(vmbox, VMBOX_REG_IRQ_STATUS);
	irq_status &= VMBOX_IRQ_VALID_MASK;
	if (!irq_status) {
		spin_lock_irqsave(&vmbox->stats_lock, flags);
		vmbox->stats.errors_irq_spurious++;
		spin_unlock_irqrestore(&vmbox->stats_lock, flags);
		return IRQ_NONE;
	}

	vmbox_writel(vmbox, VMBOX_REG_IRQ_STATUS, irq_status);
	spin_lock_irqsave(&vmbox->stats_lock, flags);
	vmbox->stats.irqs++;
	if (irq_status & VMBOX_IRQ_ERROR)
		vmbox->stats.errors++;
	if (irq_status & VMBOX_IRQ_TX_SPACE)
		vmbox->stats.tx_full_events++;
	spin_unlock_irqrestore(&vmbox->stats_lock, flags);

	if (irq_status & (VMBOX_IRQ_RX_READY | VMBOX_IRQ_ERROR |
			  VMBOX_IRQ_DONE))
		wake_up_all(&vmbox->readq);
	if (irq_status & (VMBOX_IRQ_TX_SPACE | VMBOX_IRQ_ERROR))
		wake_up_all(&vmbox->writeq);

	return IRQ_HANDLED;
}

static ssize_t vmbox_copy_rx(struct vmbox_dev *vmbox, char __user *buf,
			     size_t count)
{
	size_t copied = 0;
	u8 byte;
	u32 value;

	while (copied < count && vmbox_readl(vmbox, VMBOX_REG_RX_COUNT) > 0) {
		value = vmbox_readl(vmbox, VMBOX_REG_RX_DATA);
		byte = value & 0xff;

		if (copy_to_user(buf + copied, &byte, 1))
			return copied ? copied : -EFAULT;

		copied++;
	}

	if (copied)
		vmbox_stats_add_read(vmbox, copied);

	return copied;
}

static ssize_t vmbox_copy_tx(struct vmbox_dev *vmbox, const char __user *buf,
			     size_t count)
{
	size_t copied = 0;
	u8 byte;

	while (copied < count &&
	       vmbox_readl(vmbox, VMBOX_REG_TX_COUNT) < vmbox->fifo_depth) {
		if (copy_from_user(&byte, buf + copied, 1))
			return copied ? copied : -EFAULT;

		vmbox_writel(vmbox, VMBOX_REG_TX_DATA, byte);
		copied++;
	}

	if (copied)
		vmbox_stats_add_written(vmbox, copied);

	return copied;
}

static ssize_t vmbox_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct vmbox_dev *vmbox = file->private_data;
	unsigned long flags;
	ssize_t ret;

	if (!count)
		return 0;

	for (;;) {
		mutex_lock(&vmbox->lock);
		if (vmbox->device_gone) {
			mutex_unlock(&vmbox->lock);
			return -ENODEV;
		}

		ret = vmbox_copy_rx(vmbox, buf, count);
		if (!ret && (vmbox_readl(vmbox, VMBOX_REG_STATUS) &
			     VMBOX_STATUS_ERROR)) {
			spin_lock_irqsave(&vmbox->stats_lock, flags);
			vmbox->stats.errors_fifo_underrun++;
			spin_unlock_irqrestore(&vmbox->stats_lock, flags);
			ret = -EIO;
		}
		mutex_unlock(&vmbox->lock);
		if (ret)
			return ret;

		if (file->f_flags & O_NONBLOCK) {
			spin_lock_irqsave(&vmbox->stats_lock, flags);
			vmbox->stats.rx_empty_events++;
			spin_unlock_irqrestore(&vmbox->stats_lock, flags);
			return -EAGAIN;
		}

		ret = wait_event_interruptible(vmbox->readq,
					       vmbox_rx_ready(vmbox));
		if (ret)
			return -ERESTARTSYS;
	}
}

static ssize_t vmbox_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct vmbox_dev *vmbox = file->private_data;
	unsigned long flags;
	ssize_t ret;

	if (!count)
		return 0;

	for (;;) {
		mutex_lock(&vmbox->lock);
		if (vmbox->device_gone) {
			mutex_unlock(&vmbox->lock);
			return -ENODEV;
		}

		ret = vmbox_copy_tx(vmbox, buf, count);
		if (!ret && (vmbox_readl(vmbox, VMBOX_REG_STATUS) &
			     VMBOX_STATUS_ERROR)) {
			spin_lock_irqsave(&vmbox->stats_lock, flags);
			vmbox->stats.errors_fifo_overrun++;
			spin_unlock_irqrestore(&vmbox->stats_lock, flags);
			ret = -EIO;
		}
		mutex_unlock(&vmbox->lock);
		if (ret)
			return ret;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(vmbox->writeq,
					       vmbox_tx_ready(vmbox));
		if (ret)
			return -ERESTARTSYS;
	}
}

static __poll_t vmbox_poll(struct file *file, poll_table *wait)
{
	struct vmbox_dev *vmbox = file->private_data;
	__poll_t mask = 0;
	u32 status;

	poll_wait(file, &vmbox->readq, wait);
	poll_wait(file, &vmbox->writeq, wait);

	mutex_lock(&vmbox->lock);
	if (vmbox->device_gone) {
		mutex_unlock(&vmbox->lock);
		return EPOLLERR;
	}

	status = vmbox_readl(vmbox, VMBOX_REG_STATUS);
	if (vmbox_readl(vmbox, VMBOX_REG_RX_COUNT) > 0)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (vmbox_readl(vmbox, VMBOX_REG_TX_COUNT) < vmbox->fifo_depth)
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (status & VMBOX_STATUS_ERROR)
		mask |= EPOLLERR;
	mutex_unlock(&vmbox->lock);

	return mask;
}

static long vmbox_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	struct vmbox_dev *vmbox = file->private_data;
	struct vmbox_status status;
	struct vmbox_stats stats;
	struct vmbox_mode mode;
	unsigned long flags;
	int ret = 0;

	if (_IOC_TYPE(cmd) != VMBOX_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case VMBOX_IOC_RESET:
		mutex_lock(&vmbox->lock);
		if (vmbox->device_gone) {
			ret = -ENODEV;
		} else {
			vmbox_hw_reset(vmbox);
			vmbox_hw_enable(vmbox);
			spin_lock_irqsave(&vmbox->stats_lock, flags);
			memset(&vmbox->stats, 0, sizeof(vmbox->stats));
			spin_unlock_irqrestore(&vmbox->stats_lock, flags);
		}
		mutex_unlock(&vmbox->lock);
		return ret;

	case VMBOX_IOC_GET_STATUS:
		mutex_lock(&vmbox->lock);
		if (vmbox->device_gone) {
			mutex_unlock(&vmbox->lock);
			return -ENODEV;
		}
		status.control = vmbox_readl(vmbox, VMBOX_REG_CONTROL);
		status.status = vmbox_readl(vmbox, VMBOX_REG_STATUS);
		status.irq_status = vmbox_readl(vmbox, VMBOX_REG_IRQ_STATUS);
		status.irq_enable = vmbox_readl(vmbox, VMBOX_REG_IRQ_ENABLE);
		status.tx_count = vmbox_readl(vmbox, VMBOX_REG_TX_COUNT);
		status.rx_count = vmbox_readl(vmbox, VMBOX_REG_RX_COUNT);
		status.fifo_depth = vmbox->fifo_depth;
		status.reserved = 0;
		mutex_unlock(&vmbox->lock);

		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		return 0;

	case VMBOX_IOC_GET_STATS:
		if (READ_ONCE(vmbox->device_gone))
			return -ENODEV;

		spin_lock_irqsave(&vmbox->stats_lock, flags);
		stats = vmbox->stats;
		spin_unlock_irqrestore(&vmbox->stats_lock, flags);

		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;

	case VMBOX_IOC_SET_MODE:
		if (copy_from_user(&mode, (void __user *)arg, sizeof(mode)))
			return -EFAULT;
		if (mode.reserved || (mode.flags & ~VMBOX_MODE_FLAGS_MASK))
			return -EINVAL;

		mutex_lock(&vmbox->lock);
		if (vmbox->device_gone)
			ret = -ENODEV;
		else
			vmbox->mode_flags = mode.flags;
		mutex_unlock(&vmbox->lock);
		return ret;

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
/*
 * Safe to reuse the native ioctl handler: all UAPI structs use fixed-width
 * integer fields and contain no embedded userspace pointers.
 */
static long vmbox_compat_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	return vmbox_ioctl(file, cmd, arg);
}
#endif

static int vmbox_open(struct inode *inode, struct file *file)
{
	struct vmbox_dev *vmbox = container_of(inode->i_cdev, struct vmbox_dev,
					       cdev);
	int ret = 0;

	if (!kref_get_unless_zero(&vmbox->refcount))
		return -ENODEV;

	mutex_lock(&vmbox->lock);
	if (vmbox->device_gone) {
		ret = -ENODEV;
	} else if (vmbox->opened) {
		ret = -EBUSY;
	} else {
		vmbox->opened = true;
		file->private_data = vmbox;
	}
	mutex_unlock(&vmbox->lock);

	if (ret)
		kref_put(&vmbox->refcount, vmbox_release);

	return ret;
}

static int vmbox_release_file(struct inode *inode, struct file *file)
{
	struct vmbox_dev *vmbox = file->private_data;

	mutex_lock(&vmbox->lock);
	vmbox->opened = false;
	mutex_unlock(&vmbox->lock);

	file->private_data = NULL;
	kref_put(&vmbox->refcount, vmbox_release);

	return 0;
}

static const struct file_operations vmbox_fops = {
	.owner = THIS_MODULE,
	.open = vmbox_open,
	.release = vmbox_release_file,
	.read = vmbox_read,
	.write = vmbox_write,
	.poll = vmbox_poll,
	.unlocked_ioctl = vmbox_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vmbox_compat_ioctl,
#endif
	.llseek = no_llseek,
};

static void vmbox_chrdev_unregister(struct vmbox_dev *vmbox)
{
	if (vmbox->device_created) {
		device_destroy(vmbox_class, vmbox->devt);
		vmbox->device_created = false;
	}

	if (vmbox->cdev_added) {
		cdev_del(&vmbox->cdev);
		vmbox->cdev_added = false;
	}

	mutex_lock(&vmbox_minor_lock);
	if (vmbox->devt == vmbox_devt)
		vmbox_minor_in_use = false;
	mutex_unlock(&vmbox_minor_lock);
}

static int vmbox_chrdev_register(struct vmbox_dev *vmbox)
{
	int ret;

	mutex_lock(&vmbox_minor_lock);
	if (vmbox_minor_in_use) {
		mutex_unlock(&vmbox_minor_lock);
		return -EBUSY;
	}
	vmbox_minor_in_use = true;
	mutex_unlock(&vmbox_minor_lock);

	vmbox->devt = vmbox_devt;
	cdev_init(&vmbox->cdev, &vmbox_fops);
	vmbox->cdev.owner = THIS_MODULE;

	ret = cdev_add(&vmbox->cdev, vmbox->devt, 1);
	if (ret)
		goto err_release_minor;
	vmbox->cdev_added = true;

	vmbox->chrdev = device_create(vmbox_class, vmbox->dev, vmbox->devt,
				      vmbox, VMBOX_DEV_NAME);
	if (IS_ERR(vmbox->chrdev)) {
		ret = PTR_ERR(vmbox->chrdev);
		vmbox->chrdev = NULL;
		goto err_del_cdev;
	}
	vmbox->device_created = true;

	return 0;

err_del_cdev:
	cdev_del(&vmbox->cdev);
	vmbox->cdev_added = false;
err_release_minor:
	mutex_lock(&vmbox_minor_lock);
	vmbox_minor_in_use = false;
	mutex_unlock(&vmbox_minor_lock);
	return ret;
}

static void vmbox_teardown(struct vmbox_dev *vmbox)
{
	mutex_lock(&vmbox->lock);
	WRITE_ONCE(vmbox->device_gone, true);
	vmbox->opened = false;
	mutex_unlock(&vmbox->lock);

	vmbox_chrdev_unregister(vmbox);
	vmbox_hw_disable(vmbox);
	if (vmbox->irq_requested) {
		synchronize_irq(vmbox->irq);
		free_irq(vmbox->irq, vmbox);
		vmbox->irq_requested = false;
	}

	wake_up_all(&vmbox->readq);
	wake_up_all(&vmbox->writeq);
}

static int vmbox_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct vmbox_dev *vmbox;
	int ret;

	vmbox = kzalloc(sizeof(*vmbox), GFP_KERNEL);
	if (!vmbox)
		return -ENOMEM;

	kref_init(&vmbox->refcount);
	mutex_init(&vmbox->lock);
	spin_lock_init(&vmbox->stats_lock);
	init_waitqueue_head(&vmbox->readq);
	init_waitqueue_head(&vmbox->writeq);
	vmbox->dev = &pdev->dev;
	platform_set_drvdata(pdev, vmbox);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto err_put_ref;
	}

	if (resource_size(res) < VMBOX_MMIO_SIZE) {
		dev_err(&pdev->dev, "MMIO resource too small: %pr\n", res);
		ret = -ENODEV;
		goto err_put_ref;
	}

	vmbox->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vmbox->regs)) {
		ret = PTR_ERR(vmbox->regs);
		goto err_put_ref;
	}

	vmbox->irq = platform_get_irq(pdev, 0);
	if (vmbox->irq < 0) {
		ret = vmbox->irq;
		goto err_put_ref;
	}

	ret = vmbox_hw_validate(vmbox);
	if (ret)
		goto err_put_ref;

	vmbox_hw_reset(vmbox);

	ret = request_irq(vmbox->irq, vmbox_irq, 0, dev_name(&pdev->dev),
			  vmbox);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d: %d\n",
			vmbox->irq, ret);
		goto err_put_ref;
	}
	vmbox->irq_requested = true;

	vmbox_hw_enable(vmbox);

	ret = vmbox_chrdev_register(vmbox);
	if (ret) {
		dev_err(&pdev->dev, "failed to create %s: %d\n",
			VMBOX_DEV_NAME, ret);
		goto err_teardown;
	}

	dev_info(&pdev->dev, "probed fifo_depth=%u irq=%d\n",
		 vmbox->fifo_depth, vmbox->irq);

	return 0;

err_teardown:
	vmbox_teardown(vmbox);
err_put_ref:
	platform_set_drvdata(pdev, NULL);
	kref_put(&vmbox->refcount, vmbox_release);
	return ret;
}

static void vmbox_remove(struct platform_device *pdev)
{
	struct vmbox_dev *vmbox = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	vmbox_teardown(vmbox);
	kref_put(&vmbox->refcount, vmbox_release);
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

static int __init vmbox_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&vmbox_devt, 0, 1, VMBOX_DRV_NAME);
	if (ret)
		return ret;

	vmbox_class = class_create(VMBOX_DRV_NAME);
	if (IS_ERR(vmbox_class)) {
		ret = PTR_ERR(vmbox_class);
		goto err_unregister_chrdev;
	}

	ret = platform_driver_register(&vmbox_driver);
	if (ret)
		goto err_destroy_class;

	return 0;

err_destroy_class:
	class_destroy(vmbox_class);
	vmbox_class = NULL;
err_unregister_chrdev:
	unregister_chrdev_region(vmbox_devt, 1);
	return ret;
}

static void __exit vmbox_exit(void)
{
	platform_driver_unregister(&vmbox_driver);
	class_destroy(vmbox_class);
	unregister_chrdev_region(vmbox_devt, 1);
}

module_init(vmbox_init);
module_exit(vmbox_exit);

MODULE_AUTHOR("Lee Cheng Han <lee-cheng-han@users.noreply.github.com>");
MODULE_DESCRIPTION("QEMU virtual mailbox platform driver");
MODULE_LICENSE("GPL");
