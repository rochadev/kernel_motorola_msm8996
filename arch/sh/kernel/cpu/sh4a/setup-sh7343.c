/*
 * SH7343 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>

static struct resource iic0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start  = 0x04470000,
		.end    = 0x04470017,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 96,
		.end    = 99,
		.flags  = IORESOURCE_IRQ,
       },
};

static struct platform_device iic0_device = {
	.name           = "i2c-sh_mobile",
	.num_resources  = ARRAY_SIZE(iic0_resources),
	.resource       = iic0_resources,
};

static struct resource iic1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start  = 0x04750000,
		.end    = 0x04750017,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 44,
		.end    = 47,
		.flags  = IORESOURCE_IRQ,
       },
};

static struct platform_device iic1_device = {
	.name           = "i2c-sh_mobile",
	.num_resources  = ARRAY_SIZE(iic1_resources),
	.resource       = iic1_resources,
};

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 80, 81, 83, 82 },
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
	},
};

static struct platform_device *sh7343_devices[] __initdata = {
	&iic0_device,
	&iic1_device,
	&sci_device,
};

static int __init sh7343_devices_setup(void)
{
	return platform_add_devices(sh7343_devices,
				    ARRAY_SIZE(sh7343_devices));
}
__initcall(sh7343_devices_setup);

void __init plat_irq_setup(void)
{
}
