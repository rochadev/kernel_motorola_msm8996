/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2010  Nokia Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

struct mgmt_hdr {
	__le16 opcode;
	__le16 len;
} __packed;
#define MGMT_HDR_SIZE			4

#define MGMT_OP_READ_VERSION		0x0001
struct mgmt_rp_read_version {
	__u8 version;
	__le16 revision;
} __packed;

#define MGMT_OP_READ_INDEX_LIST		0x0003
struct mgmt_rp_read_index_list {
	__le16 num_controllers;
	__le16 index[0];
} __packed;

#define MGMT_OP_READ_INFO		0x0004
struct mgmt_cp_read_info {
	__le16 index;
} __packed;
struct mgmt_rp_read_info {
	__le16 index;
	__u8 type;
	__u8 powered;
	__u8 connectable;
	__u8 discoverable;
	__u8 pairable;
	__u8 sec_mode;
	bdaddr_t bdaddr;
	__u8 dev_class[3];
	__u8 features[8];
	__u16 manufacturer;
	__u8 hci_ver;
	__u16 hci_rev;
} __packed;

struct mgmt_mode {
	__le16 index;
	__u8 val;
} __packed;

#define MGMT_OP_SET_POWERED		0x0005

#define MGMT_OP_SET_DISCOVERABLE	0x0006

#define MGMT_OP_SET_CONNECTABLE		0x0007

#define MGMT_OP_SET_PAIRABLE		0x0008

#define MGMT_OP_ADD_UUID		0x0009
struct mgmt_cp_add_uuid {
	__le16 index;
	__u8 uuid[16];
	__u8 svc_hint;
} __packed;

#define MGMT_OP_REMOVE_UUID		0x000A
struct mgmt_cp_remove_uuid {
	__le16 index;
	__u8 uuid[16];
} __packed;

#define MGMT_OP_SET_DEV_CLASS		0x000B
struct mgmt_cp_set_dev_class {
	__le16 index;
	__u8 major;
	__u8 minor;
} __packed;

#define MGMT_OP_SET_SERVICE_CACHE	0x000C
struct mgmt_cp_set_service_cache {
	__le16 index;
	__u8 enable;
} __packed;

struct mgmt_key_info {
	bdaddr_t bdaddr;
	u8 type;
	u8 val[16];
	u8 pin_len;
} __packed;

#define MGMT_OP_LOAD_KEYS		0x000D
struct mgmt_cp_load_keys {
	__le16 index;
	__u8 debug_keys;
	__le16 key_count;
	struct mgmt_key_info keys[0];
} __packed;

#define MGMT_OP_REMOVE_KEY		0x000E
struct mgmt_cp_remove_key {
	__le16 index;
	bdaddr_t bdaddr;
	__u8 disconnect;
} __packed;

#define MGMT_EV_CMD_COMPLETE		0x0001
struct mgmt_ev_cmd_complete {
	__le16 opcode;
	__u8 data[0];
} __packed;

#define MGMT_EV_CMD_STATUS		0x0002
struct mgmt_ev_cmd_status {
	__u8 status;
	__le16 opcode;
} __packed;

#define MGMT_EV_CONTROLLER_ERROR	0x0003
struct mgmt_ev_controller_error {
	__le16 index;
	__u8 error_code;
} __packed;

#define MGMT_EV_INDEX_ADDED		0x0004
struct mgmt_ev_index_added {
	__le16 index;
} __packed;

#define MGMT_EV_INDEX_REMOVED		0x0005
struct mgmt_ev_index_removed {
	__le16 index;
} __packed;

#define MGMT_EV_POWERED			0x0006

#define MGMT_EV_DISCOVERABLE		0x0007

#define MGMT_EV_CONNECTABLE		0x0008

#define MGMT_EV_PAIRABLE		0x0009

#define MGMT_EV_NEW_KEY			0x000A
struct mgmt_ev_new_key {
	__le16 index;
	struct mgmt_key_info key;
	__u8 old_key_type;
} __packed;
