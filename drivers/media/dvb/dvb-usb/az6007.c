/*
 * Driver for AzureWave 6007 DVB-C/T USB2.0 and clones
 *
 * Copyright (c) Henry Wang <Henry.wang@AzureWave.com>
 *
 * This driver was made publicly available by Terratec, at:
 *	http://linux.terratec.de/files/TERRATEC_H7/20110323_TERRATEC_H7_Linux.tar.gz
 * The original driver's license is GPL, as declared with MODULE_LICENSE()
 *
 *  Driver modifiyed by Mauro Carvalho Chehab <mchehab@redhat.com> in order
 * 	to work with upstream drxk driver, and to fix some bugs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "drxk.h"
#include "mt2063.h"
#include "dvb_ca_en50221.h"

#define DVB_USB_LOG_PREFIX "az6007"
#include "dvb-usb.h"

/* debug */
int dvb_usb_az6007_debug;
module_param_named(debug, dvb_usb_az6007_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))."
		 DVB_USB_DEBUG_STATUS);

#define deb_info(args...) dprintk(dvb_usb_az6007_debug, 0x01, args)
#define deb_xfer(args...) dprintk(dvb_usb_az6007_debug, 0x02, args)
#define deb_rc(args...)   dprintk(dvb_usb_az6007_debug, 0x04, args)
#define deb_fe(args...)   dprintk(dvb_usb_az6007_debug, 0x08, args)

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* Known requests (Cypress FX2 firmware + az6007 "private" ones*/

#define FX2_OED			0xb5
#define AZ6007_READ_DATA	0xb7
#define AZ6007_I2C_RD		0xb9
#define AZ6007_POWER		0xbc
#define AZ6007_I2C_WR		0xbd
#define FX2_SCON1		0xc0
#define AZ6007_TS_THROUGH	0xc7
#define AZ6007_READ_IR		0xb4

struct az6007_device_state {
	struct			dvb_ca_en50221 ca;
	struct			mutex ca_mutex;
	u8			power_state;

	/* Due to DRX-K - probably need changes */
	int			(*gate_ctrl) (struct dvb_frontend *, int);
	struct			semaphore pll_mutex;
	bool			tuner_attached;
};

static struct drxk_config terratec_h7_drxk = {
	.adr = 0x29,
	.single_master = 1,
	.no_i2c_bridge = 0,
	.chunk_size = 64,
	.microcode_name = "dvb-usb-terratec-h7-drxk.fw",
	.parallel_ts = 1,
};

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct dvb_usb_adapter *adap = fe->sec_priv;
	struct az6007_device_state *st;
	int status = 0;

	deb_info("%s: %s\n", __func__, enable ? "enable" : "disable");

	if (!adap)
		return -EINVAL;

	st = adap->priv;

	if (!st)
		return -EINVAL;

	if (enable) {
#if 0
		down(&st->pll_mutex);
#endif
		status = st->gate_ctrl(fe, 1);
	} else {
#if 0
		status = st->gate_ctrl(fe, 0);
#endif
		up(&st->pll_mutex);
	}
	return status;
}

static struct mt2063_config az6007_mt2063_config = {
	.tuner_address = 0x60,
	.refclock = 36125000,
};

/* check for mutex FIXME */
static int az6007_read(struct usb_device *udev, u8 req, u16 value,
			    u16 index, u8 *b, int blen)
{
	int ret;

	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_IN,
			      value, index, b, blen, 5000);
	if (ret < 0) {
		warn("usb read operation failed. (%d)", ret);
		return -EIO;
	}

	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ", req, value,
		 index);
	debug_dump(b, blen, deb_xfer);

	return ret;
}

static int az6007_write(struct usb_device *udev, u8 req, u16 value,
			     u16 index, u8 *b, int blen)
{
	int ret;

	deb_xfer("out: req. %02x, val: %04x, ind: %04x, buffer: ", req, value,
		 index);
	debug_dump(b, blen, deb_xfer);

	if (blen > 64) {
		err("az6007: tried to write %d bytes, but I2C max size is 64 bytes\n",
		    blen);
		return -EOPNOTSUPP;
	}

	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_OUT,
			      value, index, b, blen, 5000);
	if (ret != blen) {
		err("usb write operation failed. (%d)", ret);
		return -EIO;
	}

	return 0;
}

static int az6007_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	return 0;
}

/* keys for the enclosed remote control */
static struct rc_map_table rc_map_az6007_table[] = {
	{0x0001, KEY_1},
	{0x0002, KEY_2},
};

/* remote control stuff (does not work with my box) */
static int az6007_rc_query(struct dvb_usb_device *d, u32 * event, int *state)
{
	struct rc_map_table *keymap = d->props.rc.legacy.rc_map_table;
	u8 key[10];
	int i;

	/*
	 * FIXME: remove the following return to enabled remote querying
	 * The driver likely needs proper locking to avoid troubles between
	 * this call and other concurrent calls.
	 */
	return 0;

	az6007_read(d->udev, AZ6007_READ_IR, 0, 0, key, 10);

	if (key[1] == 0x44) {
		*state = REMOTE_NO_KEY_PRESSED;
		return 0;
	}

	/*
	 * FIXME: need to make something useful with the keycodes and to
	 * convert it to the non-legacy mode. Yet, it is producing some
	 * debug info already, like:
	 * 88 04 eb 02 fd ff 00 82 63 82 (terratec IR)
	 * 88 04 eb 03 fc 00 00 82 63 82 (terratec IR)
	 * 88 80 7e 0d f2 ff 00 82 63 82 (another NEC-extended based IR)
	 * I suspect that the IR data is at bytes 1 to 4, and byte 5 is parity
	 */
	deb_rc("remote query key: %x %d\n", key[1], key[1]);
	print_hex_dump_bytes("Remote: ", DUMP_PREFIX_NONE, key, 10);

	for (i = 0; i < d->props.rc.legacy.rc_map_size; i++) {
		if (rc5_custom(&keymap[i]) == key[1]) {
			*event = keymap[i].keycode;
			*state = REMOTE_KEY_PRESSED;

			return 0;
		}
	}
	return 0;
}

#if 0
int az6007_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 v = onoff;
	return az6007_write(d->udev, AZ6007_POWER, v , 3, NULL, 1);
}
#endif

static int az6007_read_mac_addr(struct dvb_usb_device *d, u8 mac[6])
{
	int ret;
	ret = az6007_read(d->udev, AZ6007_READ_DATA, 6, 0, mac, 6);

	if (ret > 0)
		deb_info("%s: mac is %02x:%02x:%02x:%02x:%02x:%02x\n",
			 __func__, mac[0], mac[1], mac[2],
			 mac[3], mac[4], mac[5]);

	return ret;
}

static int az6007_frontend_poweron(struct dvb_usb_adapter *adap)
{
	int ret;
	struct usb_device *udev = adap->dev->udev;

	deb_info("%s: adap=%p adap->dev=%p\n", __func__, adap, adap->dev);

	ret = az6007_write(udev, AZ6007_POWER, 0, 2, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(150);
	ret = az6007_write(udev, AZ6007_POWER, 1, 4, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(100);
	ret = az6007_write(udev, AZ6007_POWER, 1, 3, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(100);
	ret = az6007_write(udev, AZ6007_POWER, 1, 4, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(100);
	ret = az6007_write(udev, FX2_SCON1, 0, 3, NULL, 0);
	if (ret < 0)
		goto error;
	msleep (10);
	ret = az6007_write(udev, FX2_SCON1, 1, 3, NULL, 0);
	if (ret < 0)
		goto error;
	msleep (10);
	ret = az6007_write(udev, AZ6007_POWER, 0, 0, NULL, 0);

error:
	if (ret < 0)
		err("%s failed with error %d", __func__, ret);

	return ret;
}

static int az6007_led_on_off(struct usb_interface *intf, int onoff)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int ret;

	/* TS through */
	ret = az6007_write(udev, AZ6007_POWER, onoff, 0, NULL, 0);
	if (ret < 0)
		err("%s failed with error %d", __func__, ret);

	return ret;
}

static int az6007_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap->priv;

	BUG_ON(!st);

	az6007_frontend_poweron(adap);

	info("attaching demod drxk");
	adap->fe_adap[0].fe = dvb_attach(drxk_attach, &terratec_h7_drxk,
					 &adap->dev->i2c_adap);
	if (!adap->fe_adap[0].fe)
		return -EINVAL;

	adap->fe_adap[0].fe->sec_priv = adap;
	/* FIXME: do we need a pll semaphore? */
	sema_init(&st->pll_mutex, 1);
	st->gate_ctrl = adap->fe_adap[0].fe->ops.i2c_gate_ctrl;
	adap->fe_adap[0].fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;

	return 0;
}

static int az6007_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap->priv;

	if (st->tuner_attached)
		return 0;

	st->tuner_attached = true;

	info("attaching tuner mt2063");
	/* Attach mt2063 to DVB-C frontend */
	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 1);
	if (!dvb_attach(mt2063_attach, adap->fe_adap[0].fe, 
			&az6007_mt2063_config,
			&adap->dev->i2c_adap))
		return -EINVAL;

	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 0);

	return 0;
}

int az6007_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (!onoff)
		return 0;


	info("Sending poweron sequence");

	az6007_write(d->udev, AZ6007_TS_THROUGH, 0, 0, NULL, 0);

#if 0
	// Seems to be a poweroff sequence
	az6007_write(d->udev, 0xbc, 1, 3, NULL, 0);
	az6007_write(d->udev, 0xbc, 1, 4, NULL, 0);
	az6007_write(d->udev, 0xc0, 0, 3, NULL, 0);
	az6007_write(d->udev, 0xc0, 1, 3, NULL, 0);
	az6007_write(d->udev, 0xbc, 0, 1, NULL, 0);
#endif
	return 0;
}

static struct dvb_usb_device_properties az6007_properties;

static void az6007_usb_disconnect(struct usb_interface *intf)
{
	dvb_usb_device_exit(intf);
}

/* I2C */
static int az6007_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			   int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i, j, len;
	int ret = 0;
	u16 index;
	u16 value;
	int length;
	u8 req, addr;
	u8 data[512];

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		if (((i + 1) < num)
		    && (msgs[i].len == 1)
		    && (!msgs[i].flags & I2C_M_RD)
		    && (msgs[i + 1].flags & I2C_M_RD)
		    && (msgs[i].addr == msgs[i + 1].addr)) {
			/*
			 * A write + read xfer for the same address, where
			 * the first xfer has just 1 byte length.
			 * Need to join both into one operation
			 */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write+read addr=0x%x len=%d/%d: ",
				       addr, msgs[i].len, msgs[i + 1].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = 6 + msgs[i + 1].len;
			len = msgs[i + 1].len;
			ret = az6007_read(d->udev, req, value, index, data,
					       length);
			if (ret >= len) {
				for (j = 0; j < len; j++) {
					msgs[i + 1].buf[j] = data[j + 5];
					if (dvb_usb_az6007_debug & 2)
						printk(KERN_CONT
						       "0x%02x ",
						       msgs[i + 1].buf[j]);
				}
			} else
				ret = -EIO;
			i++;
		} else if (!(msgs[i].flags & I2C_M_RD)) {
			/* write bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = AZ6007_I2C_WR;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = msgs[i].len - 1;
			len = msgs[i].len - 1;
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_CONT "(0x%02x) ", msgs[i].buf[0]);
			for (j = 0; j < len; j++) {
				data[j] = msgs[i].buf[j + 1];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT "0x%02x ", data[j]);
			}
			ret =  az6007_write(d->udev, req, value, index, data,
						 length);
		} else {
			/* read bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer read addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr;
			length = msgs[i].len + 6;
			len = msgs[i].len;
			ret = az6007_read(d->udev, req, value, index, data,
					       length);
			for (j = 0; j < len; j++) {
				msgs[i].buf[j] = data[j + 5];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT
					       "0x%02x ", data[j + 5]);
			}
		}
		if (dvb_usb_az6007_debug & 2)
			printk(KERN_CONT "\n");
		if (ret < 0)
			goto err;
	}
err:
	mutex_unlock(&d->i2c_mutex);

	if (ret < 0) {
		info("%s ERROR: %i", __func__, ret);
		return ret;
	}
	return num;
}

static u32 az6007_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm az6007_i2c_algo = {
	.master_xfer = az6007_i2c_xfer,
	.functionality = az6007_i2c_func,
};

int az6007_identify_state(struct usb_device *udev,
			  struct dvb_usb_device_properties *props,
			  struct dvb_usb_device_description **desc, int *cold)
{
	int ret;
	u8 mac[6];

	/* Try to read the mac address */
	ret = az6007_read(udev, AZ6007_READ_DATA, 6, 0, mac, 6);
	if (ret == 6)
		*cold = 0;
	else
		*cold = 1;

	deb_info("Device is on %s state\n", *cold? "warm" : "cold");
	return 0;
}

static int az6007_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	az6007_led_on_off(intf, 0);

	return dvb_usb_device_init(intf, &az6007_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

static struct usb_device_id az6007_usb_table[] = {
	{USB_DEVICE(USB_VID_AZUREWAVE, USB_PID_AZUREWAVE_6007)},
	{USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7)},
	{0},
};

MODULE_DEVICE_TABLE(usb, az6007_usb_table);

static struct dvb_usb_device_properties az6007_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = CYPRESS_FX2,
	.firmware            = "dvb-usb-terratec-h7-az6007.fw",
	.no_reconnect        = 1,

	.identify_state		= az6007_identify_state,
	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = az6007_streaming_ctrl,
			.tuner_attach     = az6007_tuner_attach,
			.frontend_attach  = az6007_frontend_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
			.size_of_priv     = sizeof(struct az6007_device_state),
		}}
	} },
	.power_ctrl       = az6007_power_ctrl,
	.read_mac_address = az6007_read_mac_addr,

	.rc.legacy = {
		.rc_map_table  = rc_map_az6007_table,
		.rc_map_size  = ARRAY_SIZE(rc_map_az6007_table),
		.rc_interval      = 400,
		.rc_query         = az6007_rc_query,
	},
	.i2c_algo         = &az6007_i2c_algo,

	.num_device_descs = 2,
	.devices = {
		{ .name = "AzureWave DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[0], NULL },
		  .warm_ids = { NULL },
		},
		{ .name = "TerraTec DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[1], NULL },
		  .warm_ids = { NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver az6007_usb_driver = {
	.name		= "dvb_usb_az6007",
	.probe		= az6007_usb_probe,
	.disconnect = dvb_usb_device_exit,
	/* .disconnect	= az6007_usb_disconnect, */
	.id_table	= az6007_usb_table,
};

/* module stuff */
static int __init az6007_usb_module_init(void)
{
	int result;
	deb_info("az6007 usb module init\n");

	result = usb_register(&az6007_usb_driver);
	if (result) {
		err("usb_register failed. (%d)", result);
		return result;
	}

	return 0;
}

static void __exit az6007_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	deb_info("az6007 usb module exit\n");
	usb_deregister(&az6007_usb_driver);
}

module_init(az6007_usb_module_init);
module_exit(az6007_usb_module_exit);

MODULE_AUTHOR("Henry Wang <Henry.wang@AzureWave.com>");
MODULE_DESCRIPTION("Driver for AzureWave 6007 DVB-C/T USB2.0 and clones");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
