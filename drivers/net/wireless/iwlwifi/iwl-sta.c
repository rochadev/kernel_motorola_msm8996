/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-4965.h"

int iwl_send_static_wepkey_cmd(struct iwl_priv *priv, u8 send_if_empty)
{
	int i, not_empty = 0;
	u8 buff[sizeof(struct iwl_wep_cmd) +
		sizeof(struct iwl_wep_key) * WEP_KEYS_MAX];
	struct iwl_wep_cmd *wep_cmd = (struct iwl_wep_cmd *)buff;
	size_t cmd_size  = sizeof(struct iwl_wep_cmd);
	struct iwl_host_cmd cmd = {
		.id = REPLY_WEPKEY,
		.data = wep_cmd,
		.meta.flags = CMD_ASYNC,
	};

	memset(wep_cmd, 0, cmd_size +
			(sizeof(struct iwl_wep_key) * WEP_KEYS_MAX));

	for (i = 0; i < WEP_KEYS_MAX ; i++) {
		wep_cmd->key[i].key_index = i;
		if (priv->wep_keys[i].key_size) {
			wep_cmd->key[i].key_offset = i;
			not_empty = 1;
		} else {
			wep_cmd->key[i].key_offset = WEP_INVALID_OFFSET;
		}

		wep_cmd->key[i].key_size = priv->wep_keys[i].key_size;
		memcpy(&wep_cmd->key[i].key[3], priv->wep_keys[i].key,
				priv->wep_keys[i].key_size);
	}

	wep_cmd->global_key_type = WEP_KEY_WEP_TYPE;
	wep_cmd->num_keys = WEP_KEYS_MAX;

	cmd_size += sizeof(struct iwl_wep_key) * WEP_KEYS_MAX;

	cmd.len = cmd_size;

	if (not_empty || send_if_empty)
		return iwl_send_cmd(priv, &cmd);
	else
		return 0;
}

int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct ieee80211_key_conf *key)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->default_wep_key--;
	memset(&priv->wep_keys[key->keyidx], 0, sizeof(priv->wep_keys[0]));
	ret = iwl_send_static_wepkey_cmd(priv, 1);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct ieee80211_key_conf *keyconf)
{
	int ret;
	unsigned long flags;

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = keyconf->keyidx;
	priv->stations[IWL_AP_ID].keyinfo.alg = ALG_WEP;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->default_wep_key++;

	priv->wep_keys[keyconf->keyidx].key_size = keyconf->keylen;
	memcpy(&priv->wep_keys[keyconf->keyidx].key, &keyconf->key,
							keyconf->keylen);

	ret = iwl_send_static_wepkey_cmd(priv, 0);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}

int iwl_set_wep_dynamic_key_info(struct iwl_priv *priv,
				struct ieee80211_key_conf *keyconf,
				u8 sta_id)
{
	unsigned long flags;
	__le16 key_flags = 0;
	int ret;

	keyconf->flags &= ~IEEE80211_KEY_FLAG_GENERATE_IV;
	keyconf->hw_key_idx = keyconf->keyidx;

	key_flags |= (STA_KEY_FLG_WEP | STA_KEY_FLG_MAP_KEY_MSK);
	key_flags |= cpu_to_le16(keyconf->keyidx << STA_KEY_FLG_KEYID_POS);
	key_flags &= ~STA_KEY_FLG_INVALID;

	if (keyconf->keylen == WEP_KEY_LEN_128)
		key_flags |= STA_KEY_FLG_KEY_SIZE_MSK;

	if (sta_id == priv->hw_setting.bcast_sta_id)
		key_flags |= STA_KEY_MULTICAST_MSK;

	spin_lock_irqsave(&priv->sta_lock, flags);

	priv->stations[sta_id].keyinfo.alg = keyconf->alg;
	priv->stations[sta_id].keyinfo.keylen = keyconf->keylen;
	priv->stations[sta_id].keyinfo.keyidx = keyconf->keyidx;

	memcpy(priv->stations[sta_id].keyinfo.key,
				keyconf->key, keyconf->keylen);

	memcpy(&priv->stations[sta_id].sta.key.key[3],
				keyconf->key, keyconf->keylen);

	priv->stations[sta_id].sta.key.key_offset = sta_id % 8; /* FIXME */
	priv->stations[sta_id].sta.key.key_flags = key_flags;

	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_KEY_MASK;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;

	ret = iwl4965_send_add_station(priv,
		&priv->stations[sta_id].sta, CMD_ASYNC);

	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return ret;
}
