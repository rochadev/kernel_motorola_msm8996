/*
 * Greybus power transfer protocol (ptp) driver.
 *
 * Copyright 2015 Motorola Mobility, LLC.
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/power_supply.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "greybus.h"

struct gb_ptp {
	struct gb_connection	*connection;
	struct mutex		conn_lock;
	struct power_supply	psy;
};

/* Version of the Greybus ptp protocol we support */
#define	GB_PTP_VERSION_MAJOR		0x00
#define	GB_PTP_VERSION_MINOR		0x01

/* Greybus ptp operation types */
#define GB_PTP_TYPE_GET_FUNCTIONALITY		0x02
#define GB_PTP_TYPE_SET_CURRENT_FLOW		0x03
#define GB_PTP_TYPE_SET_MAX_INPUT_CURRENT	0x04
#define GB_PTP_TYPE_EXT_POWER_CHANGED		0x05
#define GB_PTP_TYPE_EXT_POWER_PRESENT		0x06
#define GB_PTP_TYPE_POWER_REQUIRED_CHANGED	0x07
#define GB_PTP_TYPE_POWER_REQUIRED		0x08

/* Mods internal source send power capabilities */
#define GB_PTP_INT_SND_NEVER		0x00
#define GB_PTP_INT_SND_SUPPLEMENTAL	0x01
#define GB_PTP_INT_SND_LOW_BATT_SAVER	0x02

/* Mods internal source receive power capabilities */
#define GB_PTP_INT_RCV_NEVER		0x00
#define GB_PTP_INT_RCV_FIRST		0x01
#define GB_PTP_INT_RCV_SECOND		0x02
#define GB_PTP_INT_RCV_PARALLEL		0x03

/* Mods external source capabilities */
#define GB_PTP_EXT_NONE			0x00
#define GB_PTP_EXT_SUPPORTED		0x01

/* Current Flow Request */
#define GB_PTP_CURRENT_OFF		0x00
#define GB_PTP_CURRENT_TO_MOD		0x01
#define GB_PTP_CURRENT_FROM_MOD		0x02

/* External Power Present */
#define GB_PTP_EXT_POWER_NOT_PRESENT	0x00
#define GB_PTP_EXT_POWER_PRESENT	0x01

/* Mods internal source power requirements */
#define GB_PTP_POWER_NOT_REQUIRED	0x00
#define GB_PTP_POWER_REQUIRED		0x01

static enum power_supply_property gb_ptp_props[] = {
	POWER_SUPPLY_PROP_PTP_INTERNAL_SEND,
	POWER_SUPPLY_PROP_PTP_INTERNAL_RECEIVE,
	POWER_SUPPLY_PROP_PTP_INTERNAL_MAX_RECEIVE_VOLTAGE,
	POWER_SUPPLY_PROP_PTP_EXTERNAL,
	POWER_SUPPLY_PROP_PTP_CURRENT_FLOW,
	POWER_SUPPLY_PROP_PTP_MAX_INPUT_CURRENT,
	POWER_SUPPLY_PROP_PTP_EXTERNAL_PRESENT,
	POWER_SUPPLY_PROP_PTP_POWER_REQUIRED,
};

/* Greybus messages */
struct gb_ptp_functionality_response {
	__u8 int_snd;
	__u8 int_rcv;
	__le32 int_rcv_max_v;
	__u8 ext;
} __packed;

struct gb_ptp_ext_power_present_response {
	__u8 present;
} __packed;

struct gb_ptp_power_required_response {
	__u8 required;
} __packed;

struct gb_ptp_current_flow_request {
	__u8 direction;
} __packed;

struct gb_ptp_max_input_current_request {
	__le32 curr;
} __packed;

/* Internal structure */
struct gb_ptp_functionality {
	int int_snd;
	int int_rcv;
	int int_rcv_max_v;
	int ext;
};

static int to_internal_send_property(__u8 int_snd)
{
	int prop;

	switch (int_snd) {
	case GB_PTP_INT_SND_NEVER:
		prop = POWER_SUPPLY_PTP_INT_SND_NEVER;
		break;
	case GB_PTP_INT_SND_SUPPLEMENTAL:
		prop = POWER_SUPPLY_PTP_INT_SND_SUPPLEMENTAL;
		break;
	case GB_PTP_INT_SND_LOW_BATT_SAVER:
		prop = POWER_SUPPLY_PTP_INT_SND_LOW_BATT_SAVER;
		break;
	default:
		prop = POWER_SUPPLY_PTP_INT_SND_UNKNOWN;
		break;
	}

	return prop;
}

static int to_internal_receive_property(__u8 int_rcv)
{
	int prop;

	switch (int_rcv) {
	case GB_PTP_INT_RCV_NEVER:
		prop = POWER_SUPPLY_PTP_INT_RCV_NEVER;
		break;
	case GB_PTP_INT_RCV_FIRST:
		prop = POWER_SUPPLY_PTP_INT_RCV_FIRST;
		break;
	case GB_PTP_INT_RCV_SECOND:
		prop = POWER_SUPPLY_PTP_INT_RCV_SECOND;
		break;
	case GB_PTP_INT_RCV_PARALLEL:
		prop = POWER_SUPPLY_PTP_INT_RCV_PARALLEL;
		break;
	default:
		prop = POWER_SUPPLY_PTP_INT_RCV_UNKNOWN;
		break;
	}

	return prop;
}

static int to_external_property(__u8 ext)
{
	int prop;

	switch (ext) {
	case GB_PTP_EXT_NONE:
		prop = POWER_SUPPLY_PTP_EXT_NOT_SUPPORTED;
		break;
	case GB_PTP_EXT_SUPPORTED:
		prop = POWER_SUPPLY_PTP_EXT_SUPPORTED;
		break;
	default:
		prop = POWER_SUPPLY_PTP_EXT_SUPPORT_UNKNOWN;
		break;
	}

	return prop;
}

static int to_external_present_property(__u8 present)
{
	int prop;

	switch (present) {
	case GB_PTP_EXT_POWER_NOT_PRESENT:
		prop = POWER_SUPPLY_PTP_EXT_NOT_PRESENT;
		break;
	case GB_PTP_EXT_POWER_PRESENT:
		prop = POWER_SUPPLY_PTP_EXT_PRESENT;
		break;
	default:
		prop = POWER_SUPPLY_PTP_EXT_PRESENCE_UNKNOWN;
		break;
	}

	return prop;
}

static int to_power_required_property(__u8 required)
{
	int prop;

	switch (required) {
	case GB_PTP_POWER_NOT_REQUIRED:
		prop = POWER_SUPPLY_PTP_POWER_NOT_REQUIRED;
		break;
	case GB_PTP_POWER_REQUIRED:
		prop = POWER_SUPPLY_PTP_POWER_REQUIRED;
		break;
	default:
		prop = POWER_SUPPLY_PTP_POWER_REQUIREMENTS_UNKNOWN;
		break;
	}

	return prop;
}


static int gb_ptp_get_functionality(struct gb_ptp *ptp,
				    struct gb_ptp_functionality *func)
{
	struct gb_ptp_functionality_response response;
	int retval = gb_operation_sync(ptp->connection,
				       GB_PTP_TYPE_GET_FUNCTIONALITY, NULL, 0,
				       &response, sizeof(response));
	if (retval)
		return retval;

	func->int_snd = to_internal_send_property(response.int_snd);
	func->int_rcv = to_internal_receive_property(response.int_rcv);
	func->int_rcv_max_v = le32_to_cpu(response.int_rcv_max_v);
	func->ext = to_external_property(response.ext);

	return 0;
}

static int gb_ptp_ext_power_present(struct gb_ptp *ptp, int *present)
{
	struct gb_ptp_ext_power_present_response response;
	int retval = gb_operation_sync(ptp->connection,
				       GB_PTP_TYPE_EXT_POWER_PRESENT, NULL, 0,
				       &response, sizeof(response));

	if (retval)
		return retval;

	*present = to_external_present_property(response.present);
	return 0;
}

static int gb_ptp_power_required(struct gb_ptp *ptp, int *required)
{
	struct gb_ptp_power_required_response response;
	int retval = gb_operation_sync(ptp->connection,
				       GB_PTP_TYPE_POWER_REQUIRED, NULL, 0,
				       &response, sizeof(response));

	if (retval)
		return retval;

	*required = to_power_required_property(response.required);
	return 0;
}

static int gb_ptp_set_current_flow(struct gb_ptp *ptp, int direction)
{
	struct gb_ptp_current_flow_request request;

	switch (direction) {
	case POWER_SUPPLY_PTP_CURRENT_OFF:
		request.direction = GB_PTP_CURRENT_OFF;
		break;
	case POWER_SUPPLY_PTP_CURRENT_FROM_PHONE:
		request.direction = GB_PTP_CURRENT_TO_MOD;
		break;
	case POWER_SUPPLY_PTP_CURRENT_TO_PHONE:
		request.direction = GB_PTP_CURRENT_FROM_MOD;
		break;
	default:
		return -EINVAL;
	}

	return gb_operation_sync(ptp->connection, GB_PTP_TYPE_SET_CURRENT_FLOW,
				 &request, sizeof(request), NULL, 0);
}

static int gb_ptp_set_maximum_input_current(struct gb_ptp *ptp, int curr)
{
	struct gb_ptp_max_input_current_request request;

	request.curr = cpu_to_le32((u32)curr);
	return gb_operation_sync(ptp->connection,
				 GB_PTP_TYPE_SET_MAX_INPUT_CURRENT, &request,
				 sizeof(request), NULL, 0);
}

static int gb_ptp_receive(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_ptp *ptp = connection->private;

	if (type != GB_PTP_TYPE_EXT_POWER_CHANGED ||
	    type != GB_PTP_TYPE_POWER_REQUIRED_CHANGED)
		return -EINVAL;

	power_supply_changed(&ptp->psy);
	return 0;
}

static int gb_ptp_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct gb_ptp *ptp = container_of(psy, struct gb_ptp, psy);
	struct gb_ptp_functionality func;
	int retval;

	mutex_lock(&ptp->conn_lock);
	if (!ptp->connection) {
		mutex_unlock(&ptp->conn_lock);
		pr_warn("%s: supply already free'd: %s\n",
			__func__, psy->name);
		return -ENODEV;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PTP_INTERNAL_SEND:
		retval = gb_ptp_get_functionality(ptp, &func);
		if (!retval)
			val->intval = func.int_snd;
		break;
	case POWER_SUPPLY_PROP_PTP_INTERNAL_RECEIVE:
		retval = gb_ptp_get_functionality(ptp, &func);
		if (!retval)
			val->intval = func.int_rcv;
		break;
	case POWER_SUPPLY_PROP_PTP_INTERNAL_MAX_RECEIVE_VOLTAGE:
		retval = gb_ptp_get_functionality(ptp, &func);
		if (!retval)
			val->intval = func.int_rcv_max_v;
		break;
	case POWER_SUPPLY_PROP_PTP_EXTERNAL:
		retval = gb_ptp_get_functionality(ptp, &func);
		if (!retval)
			val->intval = func.ext;
		break;
	case POWER_SUPPLY_PROP_PTP_CURRENT_FLOW:
	case POWER_SUPPLY_PROP_PTP_MAX_INPUT_CURRENT:
		retval = -ENODEV; /* to make power_supply_uevent() happy */
		break;
	case POWER_SUPPLY_PROP_PTP_EXTERNAL_PRESENT:
		retval = gb_ptp_ext_power_present(ptp, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PTP_POWER_REQUIRED:
		retval = gb_ptp_power_required(ptp, &val->intval);
		break;
	default:
		retval = -EINVAL;
	}

	mutex_unlock(&ptp->conn_lock);

	return retval;
}

static int gb_ptp_set_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       const union power_supply_propval *val)
{
	struct gb_ptp *ptp = container_of(psy, struct gb_ptp, psy);
	int retval;

	mutex_lock(&ptp->conn_lock);
	if (!ptp->connection) {
		mutex_unlock(&ptp->conn_lock);
		pr_warn("%s: supply already free'd: %s\n",
			__func__, psy->name);
		return -ENODEV;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PTP_CURRENT_FLOW:
		retval = gb_ptp_set_current_flow(ptp, val->intval);
		break;
	case POWER_SUPPLY_PROP_PTP_MAX_INPUT_CURRENT:
		retval = gb_ptp_set_maximum_input_current(ptp, val->intval);
		break;
	default:
		retval = -EINVAL;
	}

	mutex_unlock(&ptp->conn_lock);

	return retval;
}

static int gb_ptp_property_is_writeable(struct power_supply *psy,
				 enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_PTP_CURRENT_FLOW ||
	       psp == POWER_SUPPLY_PROP_PTP_MAX_INPUT_CURRENT;
}

static void gb_ptp_psy_release(struct device *dev)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct gb_ptp *ptp;

	if (!psy)
		return;

	ptp = container_of(psy, struct gb_ptp, psy);
	kfree(ptp);
	kfree(dev);
}

static int gb_ptp_connection_init(struct gb_connection *connection)
{
	struct gb_ptp *ptp;
	int retval;

	ptp = kzalloc(sizeof(*ptp), GFP_KERNEL);
	if (!ptp)
		return -ENOMEM;

	ptp->connection = connection;
	connection->private = ptp;
	mutex_init(&ptp->conn_lock);

	/* Create a power supply */
	ptp->psy.name		= "gb_ptp";
	ptp->psy.type		= POWER_SUPPLY_TYPE_PTP;
	ptp->psy.properties	= gb_ptp_props;
	ptp->psy.num_properties	= ARRAY_SIZE(gb_ptp_props);
	ptp->psy.get_property	= gb_ptp_get_property;
	ptp->psy.set_property	= gb_ptp_set_property;
	ptp->psy.property_is_writeable = gb_ptp_property_is_writeable;
	retval = power_supply_register(&connection->bundle->intf->dev,
				       &ptp->psy);
	if (retval)
		goto error;

	ptp->psy.dev->release = gb_ptp_psy_release;

	return 0;

error:
	kfree(ptp);
	return retval;
}

static void gb_ptp_connection_exit(struct gb_connection *connection)
{
	struct gb_ptp *ptp = connection->private;

	power_supply_unregister(&ptp->psy);
	mutex_lock(&ptp->conn_lock);
	ptp->connection = NULL;
	mutex_unlock(&ptp->conn_lock);
}

static struct gb_protocol ptp_protocol = {
	.name			= "ptp",
	.id			= GREYBUS_PROTOCOL_PTP,
	.major			= GB_PTP_VERSION_MAJOR,
	.minor			= GB_PTP_VERSION_MINOR,
	.connection_init	= gb_ptp_connection_init,
	.connection_exit	= gb_ptp_connection_exit,
	.request_recv		= gb_ptp_receive,
};

gb_protocol_driver(&ptp_protocol);

MODULE_LICENSE("GPL v2");
