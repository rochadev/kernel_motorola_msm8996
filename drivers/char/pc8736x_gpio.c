/* linux/drivers/char/pc8736x_gpio.c

   National Semiconductor PC8736x GPIO driver.  Allows a user space
   process to play with the GPIO pins.

   Copyright (c) 2005 Jim Cromie <jim.cromie@gmail.com>

   adapted from linux/drivers/char/scx200_gpio.c
   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>,
*/

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/nsc_gpio.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define DEVNAME "pc8736x_gpio"

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_DESCRIPTION("NatSemi PC-8736x GPIO Pin Driver");
MODULE_LICENSE("GPL");

static int major;		/* default to dynamic major */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

static DEFINE_SPINLOCK(pc8736x_gpio_config_lock);
static unsigned pc8736x_gpio_base;
static u8 pc8736x_gpio_shadow[4];

#define SIO_BASE1       0x2E	/* 1st command-reg to check */
#define SIO_BASE2       0x4E	/* alt command-reg to check */
#define SIO_BASE_OFFSET 0x20

#define SIO_SID		0x20	/* SuperI/O ID Register */
#define SIO_SID_VALUE	0xe9	/* Expected value in SuperI/O ID Register */

#define SIO_CF1		0x21	/* chip config, bit0 is chip enable */

#define PC8736X_GPIO_SIZE	16

#define SIO_UNIT_SEL	0x7	/* unit select reg */
#define SIO_UNIT_ACT	0x30	/* unit enable */
#define SIO_GPIO_UNIT	0x7	/* unit number of GPIO */
#define SIO_VLM_UNIT	0x0D
#define SIO_TMS_UNIT	0x0E

/* config-space addrs to read/write each unit's runtime addr */
#define SIO_BASE_HADDR		0x60
#define SIO_BASE_LADDR		0x61

/* GPIO config-space pin-control addresses */
#define SIO_GPIO_PIN_SELECT	0xF0
#define SIO_GPIO_PIN_CONFIG     0xF1
#define SIO_GPIO_PIN_EVENT      0xF2

static unsigned char superio_cmd = 0;
static unsigned char selected_device = 0xFF;	/* bogus start val */

/* GPIO port runtime access, functionality */
static int port_offset[] = { 0, 4, 8, 10 };	/* non-uniform offsets ! */
/* static int event_capable[] = { 1, 1, 0, 0 };   ports 2,3 are hobbled */

#define PORT_OUT	0
#define PORT_IN		1
#define PORT_EVT_EN	2
#define PORT_EVT_STST	3

static struct platform_device *pdev;  /* use in dev_*() */

static inline void superio_outb(int addr, int val)
{
	outb_p(addr, superio_cmd);
	outb_p(val, superio_cmd + 1);
}

static inline int superio_inb(int addr)
{
	outb_p(addr, superio_cmd);
	return inb_p(superio_cmd + 1);
}

static int pc8736x_superio_present(void)
{
	/* try the 2 possible values, read a hardware reg to verify */
	superio_cmd = SIO_BASE1;
	if (superio_inb(SIO_SID) == SIO_SID_VALUE)
		return superio_cmd;

	superio_cmd = SIO_BASE2;
	if (superio_inb(SIO_SID) == SIO_SID_VALUE)
		return superio_cmd;

	return 0;
}

static void device_select(unsigned devldn)
{
	superio_outb(SIO_UNIT_SEL, devldn);
	selected_device = devldn;
}

static void select_pin(unsigned iminor)
{
	/* select GPIO port/pin from device minor number */
	device_select(SIO_GPIO_UNIT);
	superio_outb(SIO_GPIO_PIN_SELECT,
		     ((iminor << 1) & 0xF0) | (iminor & 0x7));
}

static inline u32 pc8736x_gpio_configure_fn(unsigned index, u32 mask, u32 bits,
					    u32 func_slct)
{
	u32 config, new_config;
	unsigned long flags;

	spin_lock_irqsave(&pc8736x_gpio_config_lock, flags);

	device_select(SIO_GPIO_UNIT);
	select_pin(index);

	/* read current config value */
	config = superio_inb(func_slct);

	/* set new config */
	new_config = (config & mask) | bits;
	superio_outb(func_slct, new_config);

	spin_unlock_irqrestore(&pc8736x_gpio_config_lock, flags);

	return config;
}

static u32 pc8736x_gpio_configure(unsigned index, u32 mask, u32 bits)
{
	return pc8736x_gpio_configure_fn(index, mask, bits,
					 SIO_GPIO_PIN_CONFIG);
}

static int pc8736x_gpio_get(unsigned minor)
{
	int port, bit, val;

	port = minor >> 3;
	bit = minor & 7;
	val = inb_p(pc8736x_gpio_base + port_offset[port] + PORT_IN);
	val >>= bit;
	val &= 1;

	dev_dbg(&pdev->dev, "_gpio_get(%d from %x bit %d) == val %d\n",
		minor, pc8736x_gpio_base + port_offset[port] + PORT_IN, bit,
		val);

	return val;
}

static void pc8736x_gpio_set(unsigned minor, int val)
{
	int port, bit, curval;

	minor &= 0x1f;
	port = minor >> 3;
	bit = minor & 7;
	curval = inb_p(pc8736x_gpio_base + port_offset[port] + PORT_OUT);

	dev_dbg(&pdev->dev, "addr:%x cur:%x bit-pos:%d cur-bit:%x + new:%d -> bit-new:%d\n",
		pc8736x_gpio_base + port_offset[port] + PORT_OUT,
		curval, bit, (curval & ~(1 << bit)), val, (val << bit));

	val = (curval & ~(1 << bit)) | (val << bit);

	dev_dbg(&pdev->dev, "gpio_set(minor:%d port:%d bit:%d)"
		" %2x -> %2x\n", minor, port, bit, curval, val);

	outb_p(val, pc8736x_gpio_base + port_offset[port] + PORT_OUT);

	curval = inb_p(pc8736x_gpio_base + port_offset[port] + PORT_OUT);
	val = inb_p(pc8736x_gpio_base + port_offset[port] + PORT_IN);

	dev_dbg(&pdev->dev, "wrote %x, read: %x\n", curval, val);
	pc8736x_gpio_shadow[port] = val;
}

static void pc8736x_gpio_set_high(unsigned index)
{
	pc8736x_gpio_set(index, 1);
}

static void pc8736x_gpio_set_low(unsigned index)
{
	pc8736x_gpio_set(index, 0);
}

static int pc8736x_gpio_current(unsigned minor)
{
	int port, bit;
	minor &= 0x1f;
	port = minor >> 3;
	bit = minor & 7;
	return ((pc8736x_gpio_shadow[port] >> bit) & 0x01);
}

static void pc8736x_gpio_change(unsigned index)
{
	pc8736x_gpio_set(index, !pc8736x_gpio_current(index));
}

static struct nsc_gpio_ops pc8736x_access = {
	.owner		= THIS_MODULE,
	.gpio_config	= pc8736x_gpio_configure,
	.gpio_dump	= nsc_gpio_dump,
	.gpio_get	= pc8736x_gpio_get,
	.gpio_set	= pc8736x_gpio_set,
	.gpio_set_high	= pc8736x_gpio_set_high,
	.gpio_set_low	= pc8736x_gpio_set_low,
	.gpio_change	= pc8736x_gpio_change,
	.gpio_current	= pc8736x_gpio_current
};

static int pc8736x_gpio_open(struct inode *inode, struct file *file)
{
	unsigned m = iminor(inode);
	file->private_data = &pc8736x_access;

	dev_dbg(&pdev->dev, "open %d\n", m);

	if (m > 63)
		return -EINVAL;
	return nonseekable_open(inode, file);
}

static struct file_operations pc8736x_gpio_fops = {
	.owner	= THIS_MODULE,
	.open	= pc8736x_gpio_open,
	.write	= nsc_gpio_write,
	.read	= nsc_gpio_read,
};

static void __init pc8736x_init_shadow(void)
{
	int port;

	/* read the current values driven on the GPIO signals */
	for (port = 0; port < 4; ++port)
		pc8736x_gpio_shadow[port]
		    = inb_p(pc8736x_gpio_base + port_offset[port]
			    + PORT_OUT);

}

static int __init pc8736x_gpio_init(void)
{
	int rc = 0;

	pdev = platform_device_alloc(DEVNAME, 0);
	if (!pdev)
		return -ENOMEM;

	rc = platform_device_add(pdev);
	if (rc) {
		rc = -ENODEV;
		goto undo_platform_dev_alloc;
	}
	dev_info(&pdev->dev, "NatSemi pc8736x GPIO Driver Initializing\n");

	if (!pc8736x_superio_present()) {
		rc = -ENODEV;
		dev_err(&pdev->dev, "no device found\n");
		goto undo_platform_dev_add;
	}
	pc8736x_access.dev = &pdev->dev;

	/* Verify that chip and it's GPIO unit are both enabled.
	   My BIOS does this, so I take minimum action here
	 */
	rc = superio_inb(SIO_CF1);
	if (!(rc & 0x01)) {
		rc = -ENODEV;
		dev_err(&pdev->dev, "device not enabled\n");
		goto undo_platform_dev_add;
	}
	device_select(SIO_GPIO_UNIT);
	if (!superio_inb(SIO_UNIT_ACT)) {
		rc = -ENODEV;
		dev_err(&pdev->dev, "GPIO unit not enabled\n");
		goto undo_platform_dev_add;
	}

	/* read the GPIO unit base addr that chip responds to */
	pc8736x_gpio_base = (superio_inb(SIO_BASE_HADDR) << 8
			     | superio_inb(SIO_BASE_LADDR));

	if (!request_region(pc8736x_gpio_base, 16, DEVNAME)) {
		rc = -ENODEV;
		dev_err(&pdev->dev, "GPIO ioport %x busy\n",
			pc8736x_gpio_base);
		goto undo_platform_dev_add;
	}
	dev_info(&pdev->dev, "GPIO ioport %x reserved\n", pc8736x_gpio_base);

	rc = register_chrdev(major, DEVNAME, &pc8736x_gpio_fops);
	if (rc < 0) {
		dev_err(&pdev->dev, "register-chrdev failed: %d\n", rc);
		goto undo_platform_dev_add;
	}
	if (!major) {
		major = rc;
		dev_dbg(&pdev->dev, "got dynamic major %d\n", major);
	}

	pc8736x_init_shadow();
	return 0;

undo_platform_dev_add:
	platform_device_put(pdev);
undo_platform_dev_alloc:
	kfree(pdev);
	return rc;
}

static void __exit pc8736x_gpio_cleanup(void)
{
	dev_dbg(&pdev->dev, " cleanup\n");

	release_region(pc8736x_gpio_base, 16);

	unregister_chrdev(major, DEVNAME);
}

EXPORT_SYMBOL(pc8736x_access);

module_init(pc8736x_gpio_init);
module_exit(pc8736x_gpio_cleanup);
