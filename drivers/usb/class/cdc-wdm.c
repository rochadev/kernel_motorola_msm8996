/*
 * cdc-wdm.c
 *
 * This driver supports USB CDC WCM Device Management.
 *
 * Copyright (c) 2007-2008 Oliver Neukum
 *
 * Some code taken from cdc-acm.c
 *
 * Released under the GPLv2.
 *
 * Many thanks to Carl Nordbeck
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.03"
#define DRIVER_AUTHOR "Oliver Neukum"
#define DRIVER_DESC "USB Abstract Control Model driver for USB WCM Device Management"

static struct usb_device_id wdm_ids[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS |
				 USB_DEVICE_ID_MATCH_INT_SUBCLASS,
		.bInterfaceClass = USB_CLASS_COMM,
		.bInterfaceSubClass = USB_CDC_SUBCLASS_DMM
	},
	{ }
};

#define WDM_MINOR_BASE	176


#define WDM_IN_USE		1
#define WDM_DISCONNECTING	2
#define WDM_RESULT		3
#define WDM_READ		4
#define WDM_INT_STALL		5
#define WDM_POLL_RUNNING	6


#define WDM_MAX			16


static DEFINE_MUTEX(wdm_mutex);

/* --- method tables --- */

struct wdm_device {
	u8			*inbuf; /* buffer for response */
	u8			*outbuf; /* buffer for command */
	u8			*sbuf; /* buffer for status */
	u8			*ubuf; /* buffer for copy to user space */

	struct urb		*command;
	struct urb		*response;
	struct urb		*validity;
	struct usb_interface	*intf;
	struct usb_ctrlrequest	*orq;
	struct usb_ctrlrequest	*irq;
	spinlock_t		iuspin;

	unsigned long		flags;
	u16			bufsize;
	u16			wMaxCommand;
	u16			wMaxPacketSize;
	u16			bMaxPacketSize0;
	__le16			inum;
	int			reslength;
	int			length;
	int			read;
	int			count;
	dma_addr_t		shandle;
	dma_addr_t		ihandle;
	struct mutex		wlock;
	struct mutex		rlock;
	struct mutex		plock;
	wait_queue_head_t	wait;
	struct work_struct	rxwork;
	int			werr;
	int			rerr;
};

static struct usb_driver wdm_driver;

/* --- callbacks --- */
static void wdm_out_callback(struct urb *urb)
{
	struct wdm_device *desc;
	desc = urb->context;
	spin_lock(&desc->iuspin);
	desc->werr = urb->status;
	spin_unlock(&desc->iuspin);
	clear_bit(WDM_IN_USE, &desc->flags);
	kfree(desc->outbuf);
	wake_up(&desc->wait);
}

static void wdm_in_callback(struct urb *urb)
{
	struct wdm_device *desc = urb->context;
	int status = urb->status;

	spin_lock(&desc->iuspin);

	if (status) {
		switch (status) {
		case -ENOENT:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ENOENT");
			break;
		case -ECONNRESET:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ECONNRESET");
			break;
		case -ESHUTDOWN:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ESHUTDOWN");
			break;
		case -EPIPE:
			err("nonzero urb status received: -EPIPE");
			break;
		default:
			err("Unexpected error %d", status);
			break;
		}
	}

	desc->rerr = status;
	desc->reslength = urb->actual_length;
	memmove(desc->ubuf + desc->length, desc->inbuf, desc->reslength);
	desc->length += desc->reslength;
	wake_up(&desc->wait);

	set_bit(WDM_READ, &desc->flags);
	spin_unlock(&desc->iuspin);
}

static void wdm_int_callback(struct urb *urb)
{
	int rv = 0;
	int status = urb->status;
	struct wdm_device *desc;
	struct usb_ctrlrequest *req;
	struct usb_cdc_notification *dr;

	desc = urb->context;
	req = desc->irq;
	dr = (struct usb_cdc_notification *)desc->sbuf;

	if (status) {
		switch (status) {
		case -ESHUTDOWN:
		case -ENOENT:
		case -ECONNRESET:
			return; /* unplug */
		case -EPIPE:
			set_bit(WDM_INT_STALL, &desc->flags);
			err("Stall on int endpoint");
			goto sw; /* halt is cleared in work */
		default:
			err("nonzero urb status received: %d", status);
			break;
		}
	}

	if (urb->actual_length < sizeof(struct usb_cdc_notification)) {
		err("wdm_int_callback - %d bytes", urb->actual_length);
		goto exit;
	}

	switch (dr->bNotificationType) {
	case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:
		dev_dbg(&desc->intf->dev,
			"NOTIFY_RESPONSE_AVAILABLE received: index %d len %d",
			dr->wIndex, dr->wLength);
		break;

	case USB_CDC_NOTIFY_NETWORK_CONNECTION:

		dev_dbg(&desc->intf->dev,
			"NOTIFY_NETWORK_CONNECTION %s network",
			dr->wValue ? "connected to" : "disconnected from");
		goto exit;
	default:
		clear_bit(WDM_POLL_RUNNING, &desc->flags);
		err("unknown notification %d received: index %d len %d",
			dr->bNotificationType, dr->wIndex, dr->wLength);
		goto exit;
	}

	req->bRequestType = (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE);
	req->bRequest = USB_CDC_GET_ENCAPSULATED_RESPONSE;
	req->wValue = 0;
	req->wIndex = desc->inum;
	req->wLength = cpu_to_le16(desc->wMaxCommand);

	usb_fill_control_urb(
		desc->response,
		interface_to_usbdev(desc->intf),
		/* using common endpoint 0 */
		usb_rcvctrlpipe(interface_to_usbdev(desc->intf), 0),
		(unsigned char *)req,
		desc->inbuf,
		desc->wMaxCommand,
		wdm_in_callback,
		desc
	);
	desc->response->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	spin_lock(&desc->iuspin);
	clear_bit(WDM_READ, &desc->flags);
	if (!test_bit(WDM_DISCONNECTING, &desc->flags)) {
		rv = usb_submit_urb(desc->response, GFP_ATOMIC);
		dev_dbg(&desc->intf->dev, "%s: usb_submit_urb %d",
			__func__, rv);
	}
	spin_unlock(&desc->iuspin);
	if (rv < 0) {
		if (rv == -EPERM)
			return;
		if (rv == -ENOMEM) {
sw:
			rv = schedule_work(&desc->rxwork);
			if (rv)
				err("Cannot schedule work");
		}
	}
exit:
	rv = usb_submit_urb(urb, GFP_ATOMIC);
	if (rv)
		err("%s - usb_submit_urb failed with result %d",
		     __func__, rv);

}

static void kill_urbs(struct wdm_device *desc)
{
	/* the order here is essential */
	usb_kill_urb(desc->command);
	usb_kill_urb(desc->validity);
	usb_kill_urb(desc->response);
}

static void free_urbs(struct wdm_device *desc)
{
	usb_free_urb(desc->validity);
	usb_free_urb(desc->response);
	usb_free_urb(desc->command);
}

static void cleanup(struct wdm_device *desc)
{
	usb_buffer_free(interface_to_usbdev(desc->intf),
			desc->wMaxPacketSize,
			desc->sbuf,
			desc->validity->transfer_dma);
	usb_buffer_free(interface_to_usbdev(desc->intf),
			desc->wMaxCommand,
			desc->inbuf,
			desc->response->transfer_dma);
	kfree(desc->orq);
	kfree(desc->irq);
	kfree(desc->ubuf);
	free_urbs(desc);
	kfree(desc);
}

static ssize_t wdm_write
(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	u8 *buf;
	int rv = -EMSGSIZE, r, we;
	struct wdm_device *desc = file->private_data;
	struct usb_ctrlrequest *req;

	if (count > desc->wMaxCommand)
		count = desc->wMaxCommand;

	spin_lock_irq(&desc->iuspin);
	we = desc->werr;
	desc->werr = 0;
	spin_unlock_irq(&desc->iuspin);
	if (we < 0)
		return -EIO;

	r = mutex_lock_interruptible(&desc->wlock); /* concurrent writes */
	rv = -ERESTARTSYS;
	if (r)
		goto outnl;

	r = usb_autopm_get_interface(desc->intf);
	if (r < 0)
		goto outnp;
	r = wait_event_interruptible(desc->wait, !test_bit(WDM_IN_USE,
							   &desc->flags));
	if (r < 0)
		goto out;

	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		rv = -ENODEV;
		goto out;
	}

	desc->outbuf = buf = kmalloc(count, GFP_KERNEL);
	if (!buf) {
		rv = -ENOMEM;
		goto out;
	}

	r = copy_from_user(buf, buffer, count);
	if (r > 0) {
		kfree(buf);
		rv = -EFAULT;
		goto out;
	}

	req = desc->orq;
	usb_fill_control_urb(
		desc->command,
		interface_to_usbdev(desc->intf),
		/* using common endpoint 0 */
		usb_sndctrlpipe(interface_to_usbdev(desc->intf), 0),
		(unsigned char *)req,
		buf,
		count,
		wdm_out_callback,
		desc
	);

	req->bRequestType = (USB_DIR_OUT | USB_TYPE_CLASS |
			     USB_RECIP_INTERFACE);
	req->bRequest = USB_CDC_SEND_ENCAPSULATED_COMMAND;
	req->wValue = 0;
	req->wIndex = desc->inum;
	req->wLength = cpu_to_le16(count);
	set_bit(WDM_IN_USE, &desc->flags);

	rv = usb_submit_urb(desc->command, GFP_KERNEL);
	if (rv < 0) {
		kfree(buf);
		clear_bit(WDM_IN_USE, &desc->flags);
		err("Tx URB error: %d", rv);
	} else {
		dev_dbg(&desc->intf->dev, "Tx URB has been submitted index=%d",
			req->wIndex);
	}
out:
	usb_autopm_put_interface(desc->intf);
outnp:
	mutex_unlock(&desc->wlock);
outnl:
	return rv < 0 ? rv : count;
}

static ssize_t wdm_read
(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	int rv, cntr;
	int i = 0;
	struct wdm_device *desc = file->private_data;


	rv = mutex_lock_interruptible(&desc->rlock); /*concurrent reads */
	if (rv < 0)
		return -ERESTARTSYS;

	if (desc->length == 0) {
		desc->read = 0;
retry:
		i++;
		rv = wait_event_interruptible(desc->wait,
					      test_bit(WDM_READ, &desc->flags));

		if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
			rv = -ENODEV;
			goto err;
		}
		usb_mark_last_busy(interface_to_usbdev(desc->intf));
		if (rv < 0) {
			rv = -ERESTARTSYS;
			goto err;
		}

		spin_lock_irq(&desc->iuspin);

		if (desc->rerr) { /* read completed, error happened */
			int t = desc->rerr;
			desc->rerr = 0;
			spin_unlock_irq(&desc->iuspin);
			err("reading had resulted in %d", t);
			rv = -EIO;
			goto err;
		}
		/*
		 * recheck whether we've lost the race
		 * against the completion handler
		 */
		if (!test_bit(WDM_READ, &desc->flags)) { /* lost race */
			spin_unlock_irq(&desc->iuspin);
			goto retry;
		}
		if (!desc->reslength) { /* zero length read */
			spin_unlock_irq(&desc->iuspin);
			goto retry;
		}
		clear_bit(WDM_READ, &desc->flags);
		spin_unlock_irq(&desc->iuspin);
	}

	cntr = count > desc->length ? desc->length : count;
	rv = copy_to_user(buffer, desc->ubuf, cntr);
	if (rv > 0) {
		rv = -EFAULT;
		goto err;
	}

	for (i = 0; i < desc->length - cntr; i++)
		desc->ubuf[i] = desc->ubuf[i + cntr];

	desc->length -= cntr;
	/* in case we had outstanding data */
	if (!desc->length)
		clear_bit(WDM_READ, &desc->flags);
	rv = cntr;

err:
	mutex_unlock(&desc->rlock);
	if (rv < 0)
		err("wdm_read: exit error");
	return rv;
}

static int wdm_flush(struct file *file, fl_owner_t id)
{
	struct wdm_device *desc = file->private_data;

	wait_event(desc->wait, !test_bit(WDM_IN_USE, &desc->flags));
	if (desc->werr < 0)
		err("Error in flush path: %d", desc->werr);

	return desc->werr;
}

static unsigned int wdm_poll(struct file *file, struct poll_table_struct *wait)
{
	struct wdm_device *desc = file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	spin_lock_irqsave(&desc->iuspin, flags);
	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		mask = POLLERR;
		spin_unlock_irqrestore(&desc->iuspin, flags);
		goto desc_out;
	}
	if (test_bit(WDM_READ, &desc->flags))
		mask = POLLIN | POLLRDNORM;
	if (desc->rerr || desc->werr)
		mask |= POLLERR;
	if (!test_bit(WDM_IN_USE, &desc->flags))
		mask |= POLLOUT | POLLWRNORM;
	spin_unlock_irqrestore(&desc->iuspin, flags);

	poll_wait(file, &desc->wait, wait);

desc_out:
	return mask;
}

static int wdm_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int rv = -ENODEV;
	struct usb_interface *intf;
	struct wdm_device *desc;

	mutex_lock(&wdm_mutex);
	intf = usb_find_interface(&wdm_driver, minor);
	if (!intf)
		goto out;

	desc = usb_get_intfdata(intf);
	if (test_bit(WDM_DISCONNECTING, &desc->flags))
		goto out;

	;
	file->private_data = desc;

	rv = usb_autopm_get_interface(desc->intf);
	if (rv < 0) {
		err("Error autopm - %d", rv);
		goto out;
	}
	intf->needs_remote_wakeup = 1;

	mutex_lock(&desc->plock);
	if (!desc->count++) {
		rv = usb_submit_urb(desc->validity, GFP_KERNEL);
		if (rv < 0) {
			desc->count--;
			err("Error submitting int urb - %d", rv);
		}
	} else {
		rv = 0;
	}
	mutex_unlock(&desc->plock);
	usb_autopm_put_interface(desc->intf);
out:
	mutex_unlock(&wdm_mutex);
	return rv;
}

static int wdm_release(struct inode *inode, struct file *file)
{
	struct wdm_device *desc = file->private_data;

	mutex_lock(&wdm_mutex);
	mutex_lock(&desc->plock);
	desc->count--;
	mutex_unlock(&desc->plock);

	if (!desc->count) {
		dev_dbg(&desc->intf->dev, "wdm_release: cleanup");
		kill_urbs(desc);
		if (!test_bit(WDM_DISCONNECTING, &desc->flags))
			desc->intf->needs_remote_wakeup = 0;
	}
	mutex_unlock(&wdm_mutex);
	return 0;
}

static const struct file_operations wdm_fops = {
	.owner =	THIS_MODULE,
	.read =		wdm_read,
	.write =	wdm_write,
	.open =		wdm_open,
	.flush =	wdm_flush,
	.release =	wdm_release,
	.poll =		wdm_poll
};

static struct usb_class_driver wdm_class = {
	.name =		"cdc-wdm%d",
	.fops =		&wdm_fops,
	.minor_base =	WDM_MINOR_BASE,
};

/* --- error handling --- */
static void wdm_rxwork(struct work_struct *work)
{
	struct wdm_device *desc = container_of(work, struct wdm_device, rxwork);
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&desc->iuspin, flags);
	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		spin_unlock_irqrestore(&desc->iuspin, flags);
	} else {
		spin_unlock_irqrestore(&desc->iuspin, flags);
		rv = usb_submit_urb(desc->response, GFP_KERNEL);
		if (rv < 0 && rv != -EPERM) {
			spin_lock_irqsave(&desc->iuspin, flags);
			if (!test_bit(WDM_DISCONNECTING, &desc->flags))
				schedule_work(&desc->rxwork);
			spin_unlock_irqrestore(&desc->iuspin, flags);
		}
	}
}

/* --- hotplug --- */

static int wdm_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int rv = -EINVAL;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct wdm_device *desc;
	struct usb_host_interface *iface;
	struct usb_endpoint_descriptor *ep;
	struct usb_cdc_dmm_desc *dmhd;
	u8 *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	u16 maxcom = 0;

	if (!buffer)
		goto out;

	while (buflen > 0) {
		if (buffer [1] != USB_DT_CS_INTERFACE) {
			err("skipping garbage");
			goto next_desc;
		}

		switch (buffer [2]) {
		case USB_CDC_HEADER_TYPE:
			break;
		case USB_CDC_DMM_TYPE:
			dmhd = (struct usb_cdc_dmm_desc *)buffer;
			maxcom = le16_to_cpu(dmhd->wMaxCommand);
			dev_dbg(&intf->dev,
				"Finding maximum buffer length: %d", maxcom);
			break;
		default:
			err("Ignoring extra header, type %d, length %d",
				buffer[2], buffer[0]);
			break;
		}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	rv = -ENOMEM;
	desc = kzalloc(sizeof(struct wdm_device), GFP_KERNEL);
	if (!desc)
		goto out;
	mutex_init(&desc->wlock);
	mutex_init(&desc->rlock);
	mutex_init(&desc->plock);
	spin_lock_init(&desc->iuspin);
	init_waitqueue_head(&desc->wait);
	desc->wMaxCommand = maxcom;
	desc->inum = cpu_to_le16((u16)intf->cur_altsetting->desc.bInterfaceNumber);
	desc->intf = intf;
	INIT_WORK(&desc->rxwork, wdm_rxwork);

	iface = &intf->altsetting[0];
	ep = &iface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(ep)) {
		rv = -EINVAL;
		goto err;
	}

	desc->wMaxPacketSize = le16_to_cpu(ep->wMaxPacketSize);
	desc->bMaxPacketSize0 = udev->descriptor.bMaxPacketSize0;

	desc->orq = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!desc->orq)
		goto err;
	desc->irq = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!desc->irq)
		goto err;

	desc->validity = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->validity)
		goto err;

	desc->response = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->response)
		goto err;

	desc->command = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->command)
		goto err;

	desc->ubuf = kmalloc(desc->wMaxCommand, GFP_KERNEL);
	if (!desc->ubuf)
		goto err;

	desc->sbuf = usb_buffer_alloc(interface_to_usbdev(intf),
					desc->wMaxPacketSize,
					GFP_KERNEL,
					&desc->validity->transfer_dma);
	if (!desc->sbuf)
		goto err;

	desc->inbuf = usb_buffer_alloc(interface_to_usbdev(intf),
					desc->bMaxPacketSize0,
					GFP_KERNEL,
					&desc->response->transfer_dma);
	if (!desc->inbuf)
		goto err2;

	usb_fill_int_urb(
		desc->validity,
		interface_to_usbdev(intf),
		usb_rcvintpipe(interface_to_usbdev(intf), ep->bEndpointAddress),
		desc->sbuf,
		desc->wMaxPacketSize,
		wdm_int_callback,
		desc,
		ep->bInterval
	);
	desc->validity->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, desc);
	rv = usb_register_dev(intf, &wdm_class);
	dev_info(&intf->dev, "cdc-wdm%d: USB WDM device\n",
		 intf->minor - WDM_MINOR_BASE);
	if (rv < 0)
		goto err;
out:
	return rv;
err2:
	usb_buffer_free(interface_to_usbdev(desc->intf),
			desc->wMaxPacketSize,
			desc->sbuf,
			desc->validity->transfer_dma);
err:
	free_urbs(desc);
	kfree(desc->ubuf);
	kfree(desc->orq);
	kfree(desc->irq);
	kfree(desc);
	return rv;
}

static void wdm_disconnect(struct usb_interface *intf)
{
	struct wdm_device *desc;
	unsigned long flags;

	usb_deregister_dev(intf, &wdm_class);
	mutex_lock(&wdm_mutex);
	desc = usb_get_intfdata(intf);

	/* the spinlock makes sure no new urbs are generated in the callbacks */
	spin_lock_irqsave(&desc->iuspin, flags);
	set_bit(WDM_DISCONNECTING, &desc->flags);
	set_bit(WDM_READ, &desc->flags);
	/* to terminate pending flushes */
	clear_bit(WDM_IN_USE, &desc->flags);
	spin_unlock_irqrestore(&desc->iuspin, flags);
	cancel_work_sync(&desc->rxwork);
	kill_urbs(desc);
	wake_up_all(&desc->wait);
	if (!desc->count)
		cleanup(desc);
	mutex_unlock(&wdm_mutex);
}

static int wdm_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct wdm_device *desc = usb_get_intfdata(intf);
	int rv = 0;

	dev_dbg(&desc->intf->dev, "wdm%d_suspend\n", intf->minor);

	mutex_lock(&desc->plock);
#ifdef CONFIG_PM
	if (interface_to_usbdev(desc->intf)->auto_pm && test_bit(WDM_IN_USE, &desc->flags)) {
		rv = -EBUSY;
	} else {
#endif
		cancel_work_sync(&desc->rxwork);
		kill_urbs(desc);
#ifdef CONFIG_PM
	}
#endif
	mutex_unlock(&desc->plock);

	return rv;
}

static int recover_from_urb_loss(struct wdm_device *desc)
{
	int rv = 0;

	if (desc->count) {
		rv = usb_submit_urb(desc->validity, GFP_NOIO);
		if (rv < 0)
			err("Error resume submitting int urb - %d", rv);
	}
	return rv;
}
static int wdm_resume(struct usb_interface *intf)
{
	struct wdm_device *desc = usb_get_intfdata(intf);
	int rv;

	dev_dbg(&desc->intf->dev, "wdm%d_resume\n", intf->minor);
	mutex_lock(&desc->plock);
	rv = recover_from_urb_loss(desc);
	mutex_unlock(&desc->plock);
	return rv;
}

static int wdm_pre_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = usb_get_intfdata(intf);

	mutex_lock(&desc->plock);
	return 0;
}

static int wdm_post_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = usb_get_intfdata(intf);
	int rv;

	rv = recover_from_urb_loss(desc);
	mutex_unlock(&desc->plock);
	return 0;
}

static struct usb_driver wdm_driver = {
	.name =		"cdc_wdm",
	.probe =	wdm_probe,
	.disconnect =	wdm_disconnect,
	.suspend =	wdm_suspend,
	.resume =	wdm_resume,
	.reset_resume =	wdm_resume,
	.pre_reset =	wdm_pre_reset,
	.post_reset =	wdm_post_reset,
	.id_table =	wdm_ids,
	.supports_autosuspend = 1,
};

/* --- low level module stuff --- */

static int __init wdm_init(void)
{
	int rv;

	rv = usb_register(&wdm_driver);

	return rv;
}

static void __exit wdm_exit(void)
{
	usb_deregister(&wdm_driver);
}

module_init(wdm_init);
module_exit(wdm_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
