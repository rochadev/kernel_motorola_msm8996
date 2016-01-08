/*
 *  HIDPP protocol for Logitech Unifying receivers
 *
 *  Copyright (c) 2011 Logitech (c)
 *  Copyright (c) 2012-2013 Google (c)
 *  Copyright (c) 2013-2014 Red Hat Inc.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kfifo.h>
#include <linux/input/mt.h>
#include <asm/unaligned.h>
#include "hid-ids.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_AUTHOR("Nestor Lopez Casado <nlopezcasad@logitech.com>");

#define REPORT_ID_HIDPP_SHORT			0x10
#define REPORT_ID_HIDPP_LONG			0x11

#define HIDPP_REPORT_SHORT_LENGTH		7
#define HIDPP_REPORT_LONG_LENGTH		20

#define HIDPP_QUIRK_CLASS_WTP			BIT(0)

/*
 * There are two hidpp protocols in use, the first version hidpp10 is known
 * as register access protocol or RAP, the second version hidpp20 is known as
 * feature access protocol or FAP
 *
 * Most older devices (including the Unifying usb receiver) use the RAP protocol
 * where as most newer devices use the FAP protocol. Both protocols are
 * compatible with the underlying transport, which could be usb, Unifiying, or
 * bluetooth. The message lengths are defined by the hid vendor specific report
 * descriptor for the HIDPP_SHORT report type (total message lenth 7 bytes) and
 * the HIDPP_LONG report type (total message length 20 bytes)
 *
 * The RAP protocol uses both report types, whereas the FAP only uses HIDPP_LONG
 * messages. The Unifying receiver itself responds to RAP messages (device index
 * is 0xFF for the receiver), and all messages (short or long) with a device
 * index between 1 and 6 are passed untouched to the corresponding paired
 * Unifying device.
 *
 * The paired device can be RAP or FAP, it will receive the message untouched
 * from the Unifiying receiver.
 */

struct fap {
	u8 feature_index;
	u8 funcindex_clientid;
	u8 params[HIDPP_REPORT_LONG_LENGTH - 4U];
};

struct rap {
	u8 sub_id;
	u8 reg_address;
	u8 params[HIDPP_REPORT_LONG_LENGTH - 4U];
};

struct hidpp_report {
	u8 report_id;
	u8 device_index;
	union {
		struct fap fap;
		struct rap rap;
		u8 rawbytes[sizeof(struct fap)];
	};
} __packed;

struct hidpp_device {
	struct hid_device *hid_dev;
	struct mutex send_mutex;
	void *send_receive_buf;
	wait_queue_head_t wait;
	bool answer_available;
	u8 protocol_major;
	u8 protocol_minor;

	void *private_data;

	unsigned long quirks;
};


#define HIDPP_ERROR				0x8f
#define HIDPP_ERROR_SUCCESS			0x00
#define HIDPP_ERROR_INVALID_SUBID		0x01
#define HIDPP_ERROR_INVALID_ADRESS		0x02
#define HIDPP_ERROR_INVALID_VALUE		0x03
#define HIDPP_ERROR_CONNECT_FAIL		0x04
#define HIDPP_ERROR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERROR_ALREADY_EXISTS		0x06
#define HIDPP_ERROR_BUSY			0x07
#define HIDPP_ERROR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERROR_RESOURCE_ERROR		0x09
#define HIDPP_ERROR_REQUEST_UNAVAILABLE		0x0a
#define HIDPP_ERROR_INVALID_PARAM_VALUE		0x0b
#define HIDPP_ERROR_WRONG_PIN_CODE		0x0c

static int __hidpp_send_report(struct hid_device *hdev,
				struct hidpp_report *hidpp_report)
{
	int fields_count, ret;

	switch (hidpp_report->report_id) {
	case REPORT_ID_HIDPP_SHORT:
		fields_count = HIDPP_REPORT_SHORT_LENGTH;
		break;
	case REPORT_ID_HIDPP_LONG:
		fields_count = HIDPP_REPORT_LONG_LENGTH;
		break;
	default:
		return -ENODEV;
	}

	/*
	 * set the device_index as the receiver, it will be overwritten by
	 * hid_hw_request if needed
	 */
	hidpp_report->device_index = 0xff;

	ret = hid_hw_raw_request(hdev, hidpp_report->report_id,
		(u8 *)hidpp_report, fields_count, HID_OUTPUT_REPORT,
		HID_REQ_SET_REPORT);

	return ret == fields_count ? 0 : -1;
}

static int hidpp_send_message_sync(struct hidpp_device *hidpp,
	struct hidpp_report *message,
	struct hidpp_report *response)
{
	int ret;

	mutex_lock(&hidpp->send_mutex);

	hidpp->send_receive_buf = response;
	hidpp->answer_available = false;

	/*
	 * So that we can later validate the answer when it arrives
	 * in hidpp_raw_event
	 */
	*response = *message;

	ret = __hidpp_send_report(hidpp->hid_dev, message);

	if (ret) {
		dbg_hid("__hidpp_send_report returned err: %d\n", ret);
		memset(response, 0, sizeof(struct hidpp_report));
		goto exit;
	}

	if (!wait_event_timeout(hidpp->wait, hidpp->answer_available,
				5*HZ)) {
		dbg_hid("%s:timeout waiting for response\n", __func__);
		memset(response, 0, sizeof(struct hidpp_report));
		ret = -ETIMEDOUT;
	}

	if (response->report_id == REPORT_ID_HIDPP_SHORT &&
	    response->fap.feature_index == HIDPP_ERROR) {
		ret = response->fap.params[1];
		dbg_hid("__hidpp_send_report got hidpp error %02X\n", ret);
		goto exit;
	}

exit:
	mutex_unlock(&hidpp->send_mutex);
	return ret;

}

static int hidpp_send_fap_command_sync(struct hidpp_device *hidpp,
	u8 feat_index, u8 funcindex_clientid, u8 *params, int param_count,
	struct hidpp_report *response)
{
	struct hidpp_report *message = kzalloc(sizeof(struct hidpp_report),
			GFP_KERNEL);
	int ret;

	if (param_count > sizeof(message->fap.params))
		return -EINVAL;

	message->report_id = REPORT_ID_HIDPP_LONG;
	message->fap.feature_index = feat_index;
	message->fap.funcindex_clientid = funcindex_clientid;
	memcpy(&message->fap.params, params, param_count);

	ret = hidpp_send_message_sync(hidpp, message, response);
	kfree(message);
	return ret;
}

static inline bool hidpp_match_answer(struct hidpp_report *question,
		struct hidpp_report *answer)
{
	return (answer->fap.feature_index == question->fap.feature_index) &&
	   (answer->fap.funcindex_clientid == question->fap.funcindex_clientid);
}

static inline bool hidpp_match_error(struct hidpp_report *question,
		struct hidpp_report *answer)
{
	return (answer->fap.feature_index == HIDPP_ERROR) &&
	    (answer->fap.funcindex_clientid == question->fap.feature_index) &&
	    (answer->fap.params[0] == question->fap.funcindex_clientid);
}

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000
#define HIDPP_PAGE_ROOT_IDX				0x00

#define CMD_ROOT_GET_FEATURE				0x01
#define CMD_ROOT_GET_PROTOCOL_VERSION			0x11

static int hidpp_root_get_feature(struct hidpp_device *hidpp, u16 feature,
	u8 *feature_index, u8 *feature_type)
{
	struct hidpp_report response;
	int ret;
	u8 params[2] = { feature >> 8, feature & 0x00FF };

	ret = hidpp_send_fap_command_sync(hidpp,
			HIDPP_PAGE_ROOT_IDX,
			CMD_ROOT_GET_FEATURE,
			params, 2, &response);
	if (ret)
		return ret;

	*feature_index = response.fap.params[0];
	*feature_type = response.fap.params[1];

	return ret;
}

static int hidpp_root_get_protocol_version(struct hidpp_device *hidpp)
{
	struct hidpp_report response;
	int ret;

	ret = hidpp_send_fap_command_sync(hidpp,
			HIDPP_PAGE_ROOT_IDX,
			CMD_ROOT_GET_PROTOCOL_VERSION,
			NULL, 0, &response);

	if (ret == 1) {
		hidpp->protocol_major = 1;
		hidpp->protocol_minor = 0;
		return 0;
	}

	if (ret)
		return -ret;

	hidpp->protocol_major = response.fap.params[0];
	hidpp->protocol_minor = response.fap.params[1];

	return ret;
}

static bool hidpp_is_connected(struct hidpp_device *hidpp)
{
	int ret;

	ret = hidpp_root_get_protocol_version(hidpp);
	if (!ret)
		hid_dbg(hidpp->hid_dev, "HID++ %u.%u device connected.\n",
			hidpp->protocol_major, hidpp->protocol_minor);
	return ret == 0;
}

/* -------------------------------------------------------------------------- */
/* 0x0005: GetDeviceNameType                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_GET_DEVICE_NAME_TYPE			0x0005

#define CMD_GET_DEVICE_NAME_TYPE_GET_COUNT		0x01
#define CMD_GET_DEVICE_NAME_TYPE_GET_DEVICE_NAME	0x11
#define CMD_GET_DEVICE_NAME_TYPE_GET_TYPE		0x21

static int hidpp_devicenametype_get_count(struct hidpp_device *hidpp,
	u8 feature_index, u8 *nameLength)
{
	struct hidpp_report response;
	int ret;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_GET_DEVICE_NAME_TYPE_GET_COUNT, NULL, 0, &response);

	if (ret)
		return -ret;

	*nameLength = response.fap.params[0];

	return ret;
}

static int hidpp_devicenametype_get_device_name(struct hidpp_device *hidpp,
	u8 feature_index, u8 char_index, char *device_name, int len_buf)
{
	struct hidpp_report response;
	int ret, i;
	int count;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_GET_DEVICE_NAME_TYPE_GET_DEVICE_NAME, &char_index, 1,
		&response);

	if (ret)
		return -ret;

	if (response.report_id == REPORT_ID_HIDPP_LONG)
		count = HIDPP_REPORT_LONG_LENGTH - 4;
	else
		count = HIDPP_REPORT_SHORT_LENGTH - 4;

	if (len_buf < count)
		count = len_buf;

	for (i = 0; i < count; i++)
		device_name[i] = response.fap.params[i];

	return count;
}

static char *hidpp_get_device_name(struct hidpp_device *hidpp, u8 *name_length)
{
	u8 feature_type;
	u8 feature_index;
	u8 __name_length;
	char *name;
	unsigned index = 0;
	int ret;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_GET_DEVICE_NAME_TYPE,
		&feature_index, &feature_type);
	if (ret)
		goto out_err;

	ret = hidpp_devicenametype_get_count(hidpp, feature_index,
		&__name_length);
	if (ret)
		goto out_err;

	name = kzalloc(__name_length + 1, GFP_KERNEL);
	if (!name)
		goto out_err;

	*name_length = __name_length + 1;
	while (index < __name_length)
		index += hidpp_devicenametype_get_device_name(hidpp,
			feature_index, index, name + index,
			__name_length - index);

	return name;

out_err:
	*name_length = 0;
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* 0x6100: TouchPadRawXY                                                      */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_TOUCHPAD_RAW_XY			0x6100

#define CMD_TOUCHPAD_GET_RAW_INFO			0x01

#define TOUCHPAD_RAW_XY_ORIGIN_LOWER_LEFT		0x01
#define TOUCHPAD_RAW_XY_ORIGIN_UPPER_LEFT		0x03

struct hidpp_touchpad_raw_info {
	u16 x_size;
	u16 y_size;
	u8 z_range;
	u8 area_range;
	u8 timestamp_unit;
	u8 maxcontacts;
	u8 origin;
	u16 res;
};

struct hidpp_touchpad_raw_xy_finger {
	u8 contact_type;
	u8 contact_status;
	u16 x;
	u16 y;
	u8 z;
	u8 area;
	u8 finger_id;
};

struct hidpp_touchpad_raw_xy {
	u16 timestamp;
	struct hidpp_touchpad_raw_xy_finger fingers[2];
	u8 spurious_flag;
	u8 end_of_frame;
	u8 finger_count;
	u8 button;
};

static int hidpp_touchpad_get_raw_info(struct hidpp_device *hidpp,
	u8 feature_index, struct hidpp_touchpad_raw_info *raw_info)
{
	struct hidpp_report response;
	int ret;
	u8 *params = (u8 *)response.fap.params;

	ret = hidpp_send_fap_command_sync(hidpp, feature_index,
		CMD_TOUCHPAD_GET_RAW_INFO, NULL, 0, &response);

	if (ret)
		return -ret;

	raw_info->x_size = get_unaligned_be16(&params[0]);
	raw_info->y_size = get_unaligned_be16(&params[2]);
	raw_info->z_range = params[4];
	raw_info->area_range = params[5];
	raw_info->maxcontacts = params[7];
	raw_info->origin = params[8];
	/* res is given in unit per inch */
	raw_info->res = get_unaligned_be16(&params[13]) * 2 / 51;

	return ret;
}

/* ************************************************************************** */
/*                                                                            */
/* Device Support                                                             */
/*                                                                            */
/* ************************************************************************** */

/* -------------------------------------------------------------------------- */
/* Touchpad HID++ devices                                                     */
/* -------------------------------------------------------------------------- */

struct wtp_data {
	struct input_dev *input;
	u16 x_size, y_size;
	u8 finger_count;
	u8 mt_feature_index;
	u8 button_feature_index;
	u8 maxcontacts;
	bool flip_y;
	unsigned int resolution;
};

static int wtp_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	return -1;
}

static void wtp_input_configured(struct hid_device *hdev,
				struct hid_input *hidinput)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd = hidpp->private_data;
	struct input_dev *input_dev = hidinput->input;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__clear_bit(EV_REL, input_dev->evbit);
	__clear_bit(EV_LED, input_dev->evbit);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, wd->x_size, 0, 0);
	input_abs_set_res(input_dev, ABS_MT_POSITION_X, wd->resolution);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, wd->y_size, 0, 0);
	input_abs_set_res(input_dev, ABS_MT_POSITION_Y, wd->resolution);

	/* Max pressure is not given by the devices, pick one */
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 50, 0, 0);

	input_set_capability(input_dev, EV_KEY, BTN_LEFT);

	__set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

	input_mt_init_slots(input_dev, wd->maxcontacts, INPUT_MT_POINTER |
		INPUT_MT_DROP_UNUSED);

	wd->input = input_dev;
}

static void wtp_touch_event(struct wtp_data *wd,
	struct hidpp_touchpad_raw_xy_finger *touch_report)
{
	int slot;

	if (!touch_report->finger_id || touch_report->contact_type)
		/* no actual data */
		return;

	slot = input_mt_get_slot_by_key(wd->input, touch_report->finger_id);

	input_mt_slot(wd->input, slot);
	input_mt_report_slot_state(wd->input, MT_TOOL_FINGER,
					touch_report->contact_status);
	if (touch_report->contact_status) {
		input_event(wd->input, EV_ABS, ABS_MT_POSITION_X,
				touch_report->x);
		input_event(wd->input, EV_ABS, ABS_MT_POSITION_Y,
				wd->flip_y ? wd->y_size - touch_report->y :
					     touch_report->y);
		input_event(wd->input, EV_ABS, ABS_MT_PRESSURE,
				touch_report->area);
	}
}

static void wtp_send_raw_xy_event(struct hidpp_device *hidpp,
		struct hidpp_touchpad_raw_xy *raw)
{
	struct wtp_data *wd = hidpp->private_data;
	int i;

	for (i = 0; i < 2; i++)
		wtp_touch_event(wd, &(raw->fingers[i]));

	if (raw->end_of_frame)
		input_event(wd->input, EV_KEY, BTN_LEFT, raw->button);

	if (raw->end_of_frame || raw->finger_count <= 2) {
		input_mt_sync_frame(wd->input);
		input_sync(wd->input);
	}
}

static int wtp_mouse_raw_xy_event(struct hidpp_device *hidpp, u8 *data)
{
	struct wtp_data *wd = hidpp->private_data;
	u8 c1_area = ((data[7] & 0xf) * (data[7] & 0xf) +
		      (data[7] >> 4) * (data[7] >> 4)) / 2;
	u8 c2_area = ((data[13] & 0xf) * (data[13] & 0xf) +
		      (data[13] >> 4) * (data[13] >> 4)) / 2;
	struct hidpp_touchpad_raw_xy raw = {
		.timestamp = data[1],
		.fingers = {
			{
				.contact_type = 0,
				.contact_status = !!data[7],
				.x = get_unaligned_le16(&data[3]),
				.y = get_unaligned_le16(&data[5]),
				.z = c1_area,
				.area = c1_area,
				.finger_id = data[2],
			}, {
				.contact_type = 0,
				.contact_status = !!data[13],
				.x = get_unaligned_le16(&data[9]),
				.y = get_unaligned_le16(&data[11]),
				.z = c2_area,
				.area = c2_area,
				.finger_id = data[8],
			}
		},
		.finger_count = wd->maxcontacts,
		.spurious_flag = 0,
		.end_of_frame = (data[0] >> 7) == 0,
		.button = data[0] & 0x01,
	};

	wtp_send_raw_xy_event(hidpp, &raw);

	return 1;
}

static int wtp_raw_event(struct hid_device *hdev, u8 *data, int size)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd = hidpp->private_data;

	if (!wd || !wd->input || (data[0] != 0x02) || size < 21)
		return 1;

	return wtp_mouse_raw_xy_event(hidpp, &data[7]);
}

static int wtp_get_config(struct hidpp_device *hidpp)
{
	struct wtp_data *wd = hidpp->private_data;
	struct hidpp_touchpad_raw_info raw_info = {0};
	u8 feature_type;
	int ret;

	ret = hidpp_root_get_feature(hidpp, HIDPP_PAGE_TOUCHPAD_RAW_XY,
		&wd->mt_feature_index, &feature_type);
	if (ret)
		/* means that the device is not powered up */
		return ret;

	ret = hidpp_touchpad_get_raw_info(hidpp, wd->mt_feature_index,
		&raw_info);
	if (ret)
		return ret;

	wd->x_size = raw_info.x_size;
	wd->y_size = raw_info.y_size;
	wd->maxcontacts = raw_info.maxcontacts;
	wd->flip_y = raw_info.origin == TOUCHPAD_RAW_XY_ORIGIN_LOWER_LEFT;
	wd->resolution = raw_info.res;

	return 0;
}

static int wtp_allocate(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	struct wtp_data *wd;

	wd = devm_kzalloc(&hdev->dev, sizeof(struct wtp_data),
			GFP_KERNEL);
	if (!wd)
		return -ENOMEM;

	hidpp->private_data = wd;

	return 0;
};

/* -------------------------------------------------------------------------- */
/* Generic HID++ devices                                                      */
/* -------------------------------------------------------------------------- */

static int hidpp_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		return wtp_input_mapping(hdev, hi, field, usage, bit, max);

	return 0;
}

static int  hidpp_input_configured(struct hid_device *hdev,
				struct hid_input *hidinput)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		wtp_input_configured(hdev, hidinput);

	return 0;
}

static int hidpp_raw_hidpp_event(struct hidpp_device *hidpp, u8 *data,
		int size)
{
	struct hidpp_report *question = hidpp->send_receive_buf;
	struct hidpp_report *answer = hidpp->send_receive_buf;
	struct hidpp_report *report = (struct hidpp_report *)data;

	/*
	 * If the mutex is locked then we have a pending answer from a
	 * previoulsly sent command
	 */
	if (unlikely(mutex_is_locked(&hidpp->send_mutex))) {
		/*
		 * Check for a correct hidpp20 answer or the corresponding
		 * error
		 */
		if (hidpp_match_answer(question, report) ||
				hidpp_match_error(question, report)) {
			*answer = *report;
			hidpp->answer_available = true;
			wake_up(&hidpp->wait);
			/*
			 * This was an answer to a command that this driver sent
			 * We return 1 to hid-core to avoid forwarding the
			 * command upstream as it has been treated by the driver
			 */

			return 1;
		}
	}

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		return wtp_raw_event(hidpp->hid_dev, data, size);

	return 0;
}

static int hidpp_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *data, int size)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	switch (data[0]) {
	case REPORT_ID_HIDPP_LONG:
		if (size != HIDPP_REPORT_LONG_LENGTH) {
			hid_err(hdev, "received hid++ report of bad size (%d)",
				size);
			return 1;
		}
		return hidpp_raw_hidpp_event(hidpp, data, size);
	case REPORT_ID_HIDPP_SHORT:
		if (size != HIDPP_REPORT_SHORT_LENGTH) {
			hid_err(hdev, "received hid++ report of bad size (%d)",
				size);
			return 1;
		}
		return hidpp_raw_hidpp_event(hidpp, data, size);
	}

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP)
		return wtp_raw_event(hdev, data, size);

	return 0;
}

static void hidpp_overwrite_name(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);
	char *name;
	u8 name_length;

	name = hidpp_get_device_name(hidpp, &name_length);

	if (!name)
		hid_err(hdev, "unable to retrieve the name of the device");
	else
		snprintf(hdev->name, sizeof(hdev->name), "%s", name);

	kfree(name);
}

static int hidpp_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidpp_device *hidpp;
	int ret;
	bool connected;

	hidpp = devm_kzalloc(&hdev->dev, sizeof(struct hidpp_device),
			GFP_KERNEL);
	if (!hidpp)
		return -ENOMEM;

	hidpp->hid_dev = hdev;
	hid_set_drvdata(hdev, hidpp);

	hidpp->quirks = id->driver_data;

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP) {
		ret = wtp_allocate(hdev, id);
		if (ret)
			return ret;
	}

	mutex_init(&hidpp->send_mutex);
	init_waitqueue_head(&hidpp->wait);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "%s:parse failed\n", __func__);
		goto hid_parse_fail;
	}

	/* Allow incoming packets */
	hid_device_io_start(hdev);

	connected = hidpp_is_connected(hidpp);
	if (!connected) {
		hid_err(hdev, "Device not connected");
		goto hid_parse_fail;
	}

	/* the device is connected, we can ask for its name */
	hid_info(hdev, "HID++ %u.%u device connected.\n",
		 hidpp->protocol_major, hidpp->protocol_minor);
	hidpp_overwrite_name(hdev);

	if (hidpp->quirks & HIDPP_QUIRK_CLASS_WTP) {
		ret = wtp_get_config(hidpp);
		if (ret)
			goto hid_parse_fail;
	}

	/* Block incoming packets */
	hid_device_io_stop(hdev);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "%s:hid_hw_start returned error\n", __func__);
		goto hid_hw_start_fail;
	}

	return ret;

hid_hw_start_fail:
hid_parse_fail:
	mutex_destroy(&hidpp->send_mutex);
	hid_set_drvdata(hdev, NULL);
	return ret;
}

static void hidpp_remove(struct hid_device *hdev)
{
	struct hidpp_device *hidpp = hid_get_drvdata(hdev);

	mutex_destroy(&hidpp->send_mutex);
	hid_hw_stop(hdev);
}

static const struct hid_device_id hidpp_devices[] = {
	{ /* wireless touchpad T651 */
	  HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH,
		USB_DEVICE_ID_LOGITECH_T651),
	  .driver_data = HIDPP_QUIRK_CLASS_WTP },
	{}
};

MODULE_DEVICE_TABLE(hid, hidpp_devices);

static struct hid_driver hidpp_driver = {
	.name = "logitech-hidpp-device",
	.id_table = hidpp_devices,
	.probe = hidpp_probe,
	.remove = hidpp_remove,
	.raw_event = hidpp_raw_event,
	.input_configured = hidpp_input_configured,
	.input_mapping = hidpp_input_mapping,
};

module_hid_driver(hidpp_driver);
