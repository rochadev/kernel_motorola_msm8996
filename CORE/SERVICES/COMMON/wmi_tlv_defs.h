/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

#ifndef _WMI_TLV_DEFS_H_
#define _WMI_TLV_DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define WMITLV_FIELD_BUF_IS_ALLOCATED(elem_name) \
       is_allocated_##elem_name

#define WMITLV_FIELD_NUM_OF(elem_name) \
       num_##elem_name

/* Define the structure typedef for the TLV parameters of each cmd/event */
#define WMITLV_TYPEDEF_STRUCT_PARAMS_TLVS(wmi_cmd_event_id) \
       wmi_cmd_event_id##_param_tlvs

/*
 * The following macro WMITLV_OP_* are created by the macro WMITLV_ELEM().
 */
/* macro to define the TLV name in the correct order. When (op==TAG_ORDER) */
#define WMITLV_OP_TAG_ORDER_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      wmi_cmd_event_id##_tlv_order_##elem_name,

/* macro to define the TLV name with the TLV Tag value. When (op==TAG_ID) */
#define WMITLV_OP_TAG_ID_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      wmi_cmd_event_id##_tlv_tag_##elem_name = elem_tlv_tag,

/* macro to define the TLV name with the TLV structure size. May not be accurate when variable length. When (op==TAG_SIZEOF) */
#define WMITLV_OP_TAG_SIZEOF_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      wmi_cmd_event_id##_sizeof_##elem_name = sizeof(elem_struc_type),

/* macro to define the TLV name with value indicating whether the TLV is variable length. When (op==TAG_VAR_SIZED) */
#define WMITLV_OP_TAG_VAR_SIZED_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      wmi_cmd_event_id##_var_sized_##elem_name = var_len,

/* macro to define the TLV name with value indicating the fixed array size. When (op==TAG_ARR_SIZE) */
#define WMITLV_OP_TAG_ARR_SIZE_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      wmi_cmd_event_id##_arr_size_##elem_name = arr_size,

/*
 * macro to define afew fields associated to a TLV. For example, a structure pointer with the TLV name.
 * This macro is expand from WMITLV_ELEM(op) when (op==STRUCT_FIELD).
 * NOTE: If this macro is changed, then "mirror" structure wmitlv_cmd_param_info
 * should be updated too.
 */
#define WMITLV_OP_STRUCT_FIELD_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)  \
      elem_struc_type *elem_name; \
      A_UINT32 WMITLV_FIELD_NUM_OF(elem_name); \
      A_UINT32 WMITLV_FIELD_BUF_IS_ALLOCATED(elem_name);

/*
 * A "mirror" structure that contains the fields that is created by the
 * macro WMITLV_OP_STRUCT_FIELD_macro.
 * NOTE: you should modify this structure and WMITLV_OP_STRUCT_FIELD_macro
 * so that they both has the same kind of fields.
 */
typedef struct {
    void *tlv_ptr;            /* Pointer to the TLV Buffer. But the "real" one will have the right type instead of void. */
    A_UINT32 num_elements;    /* Number of elements. For non-array, this is one. For array, this is the number of elements. */
    A_UINT32 buf_is_allocated;/* Boolean flag to indicate that a new buffer is allocated for this TLV. */
} wmitlv_cmd_param_info;

/*
 * NOTE TRICKY MACRO:
 *  WMITLV_ELEM is re-defined to a "op" specific macro.
 *  Eg. WMITLV_OP_TAG_ORDER_macro is created for the op_type=TAG_ORDER.
 */
#define WMITLV_ELEM(wmi_cmd_event_id, op_type, param_ptr, param_len, elem_tlv_tag, elem_struc_type, elem_name, var_len) \
    WMITLV_OP_##op_type##_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, WMITLV_ARR_SIZE_INVALID)
/*
 *  WMITLV_FXAR (FiX ARray) is similar to WMITLV_ELEM except it has an extra parameter for the fixed number of elements.
 *  It is re-defined to a "op" specific macro.
 *  Eg. WMITLV_OP_TAG_ORDER_macro is created for the op_type=TAG_ORDER.
 */
#define WMITLV_FXAR(wmi_cmd_event_id, op_type, param_ptr, param_len, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size) \
    WMITLV_OP_##op_type##_macro(param_ptr, param_len, wmi_cmd_event_id, elem_tlv_tag, elem_struc_type, elem_name, var_len, arr_size)

#define WMITLV_TABLE(id,op,buf,len) WMITLV_TABLE_##id(id,op,buf,len)

/*
 * This macro will create various enumerations and structures to describe the TLVs for
 * the given Command/Event ID.
 *
 * For example, the following is for WMI_SERVICE_READY_EVENTID:
 *    #define WMITLV_TABLE_WMI_SERVICE_READY_EVENTID(id,op,buf,len)                                                                                                 \
 *       WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_service_ready_event_fixed_param, wmi_service_ready_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)     \
 *       WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_HAL_REG_CAPABILITIES, HAL_REG_CAPABILITIES, hal_reg_capabilities, WMITLV_SIZE_FIX)  \
 *       WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, wmi_service_bitmap, WMITLV_SIZE_FIX, WMI_SERVICE_BM_SIZE) \
 *       WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wlan_host_mem_req, mem_reqs, WMITLV_SIZE_VAR)
 *    WMITLV_CREATE_PARAM_STRUC(WMI_SERVICE_READY_EVENTID);
 * This macro will create the following text:
 *
 * typedef enum {
 *    WMI_SERVICE_READY_EVENTID_tlv_order_wmi_service_ready_event_fixed_param,
 *    WMI_SERVICE_READY_EVENTID_tlv_order_hal_reg_capabilities,
 *    WMI_SERVICE_READY_EVENTID_tlv_order_wmi_service_bitmap,
 *    WMI_SERVICE_READY_EVENTID_tlv_order_mem_reqs,
 *    WMI_TLV_HLPR_NUM_TLVS_FOR_WMI_SERVICE_READY_EVENTID
 * } WMI_SERVICE_READY_EVENTID_TAG_ID_enum_type;
 * //NOTE: WMI_TLV_HLPR_NUM_TLVS_FOR_WMI_SERVICE_READY_EVENTID is the number of TLVs.
 *
 * typedef enum {
 *    WMI_SERVICE_READY_EVENTID_tlv_tag_wmi_service_ready_event_fixed_param = WMITLV_TAG_STRUC_wmi_service_ready_event_fixed_param,
 *    WMI_SERVICE_READY_EVENTID_tlv_tag_hal_reg_capabilities = WMITLV_TAG_STRUC_HAL_REG_CAPABILITIES,
 *    WMI_SERVICE_READY_EVENTID_tlv_tag_wmi_service_bitmap = WMITLV_TAG_ARRAY_UINT32,
 *    WMI_SERVICE_READY_EVENTID_tlv_tag_mem_reqs = WMITLV_TAG_ARRAY_STRUC,
 * } WMI_SERVICE_READY_EVENTID_TAG_ORDER_enum_type;
 *
 * typedef enum {
 *    WMI_SERVICE_READY_EVENTID_sizeof_wmi_service_ready_event_fixed_param = sizeof(wmi_service_ready_event_fixed_param),
 *    WMI_SERVICE_READY_EVENTID_sizeof_hal_reg_capabilities = sizeof(HAL_REG_CAPABILITIES),
 *    WMI_SERVICE_READY_EVENTID_sizeof_wmi_service_bitmap = sizeof(A_UINT32),
 *    WMI_SERVICE_READY_EVENTID_sizeof_mem_reqs = sizeof(wlan_host_mem_req),
 * } WMI_SERVICE_READY_EVENTID_TAG_SIZEOF_enum_type;
 *
 * typedef enum {
 *    WMI_SERVICE_READY_EVENTID_var_sized_wmi_service_ready_event_fixed_param = WMITLV_SIZE_FIX,
 *    WMI_SERVICE_READY_EVENTID_var_sized_hal_reg_capabilities = WMITLV_SIZE_FIX,
 *    WMI_SERVICE_READY_EVENTID_var_sized_wmi_service_bitmap = WMITLV_SIZE_VAR,
 *    WMI_SERVICE_READY_EVENTID_var_sized_mem_reqs = WMITLV_SIZE_VAR,
 * } WMI_SERVICE_READY_EVENTID_TAG_VAR_SIZED_enum_type;
 *
 * typedef enum {
 *    WMI_SERVICE_READY_EVENTID_arr_size_wmi_service_ready_event_fixed_param = WMITLV_ARR_SIZE_INVALID,
 *    WMI_SERVICE_READY_EVENTID_arr_size_hal_reg_capabilities = WMITLV_ARR_SIZE_INVALID,
 *    WMI_SERVICE_READY_EVENTID_arr_size_wmi_service_bitmap = WMI_SERVICE_BM_SIZE,
 *    WMI_SERVICE_READY_EVENTID_arr_size_mem_reqs = WMITLV_ARR_SIZE_INVALID,
 * } WMI_SERVICE_READY_EVENTID_TAG_ARR_SIZE_enum_type;
 *
 * typedef struct {
 *    wmi_service_ready_event_fixed_param *fixed_param;
 *    A_UINT32 num_fixed_param;
 *    A_UINT32 is_allocated_fixed_param;
 *    HAL_REG_CAPABILITIES *hal_reg_capabilities;
 *    A_UINT32 num_hal_reg_capabilities;
 *    A_UINT32 is_allocated_hal_reg_capabilities;
 *    A_UINT32 *wmi_service_bitmap;
 *    A_UINT32 num_wmi_service_bitmap;
 *    A_UINT32 is_allocated_wmi_service_bitmap;
 *    wlan_host_mem_req *mem_reqs;
 *    A_UINT32 num_mem_reqs;
 *    A_UINT32 is_allocated_mem_reqs;
 *
 * } WMI_SERVICE_READY_EVENTID_param_tlvs;
 *
 */

#define WMITLV_CREATE_PARAM_STRUC(wmi_cmd_event_id)            \
    typedef enum {                                             \
        WMITLV_TABLE(wmi_cmd_event_id, TAG_ORDER, NULL, 0)     \
        WMI_TLV_HLPR_NUM_TLVS_FOR_##wmi_cmd_event_id           \
    } wmi_cmd_event_id##_TAG_ORDER_enum_type;                  \
                                                               \
    typedef struct {                                           \
        WMITLV_TABLE(wmi_cmd_event_id, STRUCT_FIELD, NULL, 0)  \
    } WMITLV_TYPEDEF_STRUCT_PARAMS_TLVS(wmi_cmd_event_id);     \

/** Enum list of TLV Tags for each parameter structure type. */
typedef enum {
    /* 0 to 15 is reserved */
    WMITLV_TAG_LAST_RESERVED = 15,
    WMITLV_TAG_FIRST_ARRAY_ENUM, /* First entry of ARRAY type tags */
    WMITLV_TAG_ARRAY_UINT32 = WMITLV_TAG_FIRST_ARRAY_ENUM,
    WMITLV_TAG_ARRAY_BYTE,
    WMITLV_TAG_ARRAY_STRUC,
    WMITLV_TAG_ARRAY_FIXED_STRUC,
    WMITLV_TAG_LAST_ARRAY_ENUM = 31,   /* Last entry of ARRAY type tags */
    WMITLV_TAG_STRUC_wmi_service_ready_event_fixed_param,
    WMITLV_TAG_STRUC_HAL_REG_CAPABILITIES,
    WMITLV_TAG_STRUC_wlan_host_mem_req,
    WMITLV_TAG_STRUC_wmi_ready_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_scan_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_tpc_config_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_chan_info_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_comb_phyerr_rx_hdr,
    WMITLV_TAG_STRUC_wmi_vdev_start_response_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_stopped_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_install_key_complete_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_sta_kickout_event_fixed_param,
	WMITLV_TAG_STRUC_wmi_mgmt_rx_hdr,
    WMITLV_TAG_STRUC_wmi_tbtt_offset_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_tx_delba_complete_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_tx_addba_complete_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_event_fixed_param,
	WMITLV_TAG_STRUC_WOW_EVENT_INFO_fixed_param,
	WMITLV_TAG_STRUC_WOW_EVENT_INFO_SECTION_BITMAP,
    WMITLV_TAG_STRUC_wmi_rtt_event_header,
    WMITLV_TAG_STRUC_wmi_rtt_error_report_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_rtt_meas_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_echo_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_ftm_intg_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_get_keepalive_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_gpio_input_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_csa_event_fixed_param,
    WMITLV_TAG_STRUC_WMI_GTK_OFFLOAD_STATUS_EVENT_fixed_param,
    WMITLV_TAG_STRUC_wmi_igtk_info,
    WMITLV_TAG_STRUC_wmi_dcs_interference_event_fixed_param,
    WMITLV_TAG_STRUC_ath_dcs_cw_int,
    WMITLV_TAG_STRUC_ath_dcs_wlan_int_stat,
    WMITLV_TAG_STRUC_wmi_wlan_profile_ctx_t,
    WMITLV_TAG_STRUC_wmi_wlan_profile_t,
    WMITLV_TAG_STRUC_wmi_pdev_qvit_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_host_swba_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_tim_info,
    WMITLV_TAG_STRUC_wmi_p2p_noa_info,
    WMITLV_TAG_STRUC_wmi_stats_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_avoid_freq_ranges_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_avoid_freq_range_desc,
    WMITLV_TAG_STRUC_wmi_gtk_rekey_fail_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_init_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_resource_config,
    WMITLV_TAG_STRUC_wlan_host_memory_chunk,
    WMITLV_TAG_STRUC_wmi_start_scan_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_stop_scan_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_scan_chan_list_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_channel,
    WMITLV_TAG_STRUC_wmi_pdev_set_regdomain_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_param_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_wmm_params_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wmm_params,
    WMITLV_TAG_STRUC_wmi_pdev_set_quiet_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_create_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_delete_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_start_request_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_p2p_noa_descriptor,
    WMITLV_TAG_STRUC_wmi_p2p_go_set_beacon_ie_fixed_param,
    WMITLV_TAG_STRUC_WMI_GTK_OFFLOAD_CMD_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_up_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_stop_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_down_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_set_param_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_install_key_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_create_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_delete_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_flush_tids_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_set_param_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_assoc_complete_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vht_rate_set,
    WMITLV_TAG_STRUC_wmi_bcn_tmpl_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_prb_tmpl_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_bcn_prb_info,
    WMITLV_TAG_STRUC_wmi_peer_tid_addba_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_tid_delba_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_powersave_mode_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_powersave_param_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_dtim_ps_method_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_scan_mode_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_scan_rssi_threshold_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_scan_period_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_scan_rssi_change_threshold_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_suspend_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_resume_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_add_bcn_filter_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_rmv_bcn_filter_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wow_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wow_hostwakeup_from_sleep_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_uapsd_auto_trig_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_uapsd_auto_trig_param,
    WMITLV_TAG_STRUC_WMI_SET_ARP_NS_OFFLOAD_CMD_fixed_param,
    WMITLV_TAG_STRUC_WMI_ARP_OFFLOAD_TUPLE,
    WMITLV_TAG_STRUC_WMI_NS_OFFLOAD_TUPLE,
    WMITLV_TAG_STRUC_wmi_ftm_intg_cmd_fixed_param,
    WMITLV_TAG_STRUC_WMI_STA_KEEPALIVE_CMD_fixed_param,
    WMITLV_TAG_STRUC_WMI_STA_KEEPALVE_ARP_RESPONSE,
    WMITLV_TAG_STRUC_wmi_p2p_set_vendor_ie_data_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_ap_ps_peer_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_rate_retry_sched_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wlan_profile_trigger_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wlan_profile_set_hist_intvl_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wlan_profile_get_prof_data_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_wlan_profile_enable_profile_id_cmd_fixed_param,
    WMITLV_TAG_STRUC_WMI_WOW_DEL_PATTERN_CMD_fixed_param,
    WMITLV_TAG_STRUC_WMI_WOW_ADD_DEL_EVT_CMD_fixed_param,
    WMITLV_TAG_STRUC_wmi_rtt_measreq_head,
    WMITLV_TAG_STRUC_wmi_rtt_measreq_body,
    WMITLV_TAG_STRUC_wmi_rtt_tsf_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_spectral_configure_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_spectral_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_request_stats_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_nlo_config_cmd_fixed_param,
    WMITLV_TAG_STRUC_nlo_configured_parameters,
    WMITLV_TAG_STRUC_wmi_csa_offload_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_csa_offload_chanswitch_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_chatter_set_mode_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_echo_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_set_keepalive_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_get_keepalive_cmd_fixed_param,
    WMITLV_TAG_STRUC_WMI_FORCE_FW_HANG_CMD_fixed_param,
    WMITLV_TAG_STRUC_wmi_gpio_config_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_gpio_output_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_add_wds_entry_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_remove_wds_entry_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_bcn_tx_hdr,
    WMITLV_TAG_STRUC_wmi_bcn_send_from_host_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_mgmt_tx_hdr,
    WMITLV_TAG_STRUC_wmi_addba_clear_resp_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_addba_send_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_delba_send_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_addba_setresponse_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_send_singleamsdu_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_pktlog_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_pktlog_disable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_ht_ie_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_vht_ie_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_dscp_tid_map_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_green_ap_ps_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_get_tpc_config_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_set_base_macaddr_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_peer_mcast_group_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_ap_profile_fixed_param,
    WMITLV_TAG_STRUC_wmi_ap_profile,
    WMITLV_TAG_STRUC_wmi_scan_sch_priority_table_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_dfs_enable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_pdev_dfs_disable_cmd_fixed_param,
    WMITLV_TAG_STRUC_WMI_WOW_ADD_PATTERN_CMD_fixed_param,
    WMITLV_TAG_STRUC_WOW_BITMAP_PATTERN_T,
    WMITLV_TAG_STRUC_WOW_IPV4_SYNC_PATTERN_T,
    WMITLV_TAG_STRUC_WOW_IPV6_SYNC_PATTERN_T,
    WMITLV_TAG_STRUC_WOW_MAGIC_PATTERN_CMD,
    WMITLV_TAG_STRUC_WMI_scan_update_request_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_chatter_pkt_coalescing_filter,
    WMITLV_TAG_STRUC_wmi_chatter_coalescing_add_filter_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_chatter_coalescing_delete_filter_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_chatter_coalescing_query_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_txbf_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_debug_log_config_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_nlo_event,
	WMITLV_TAG_STRUC_wmi_chatter_query_reply_event_fixed_param,
	WMITLV_TAG_STRUC_wmi_upload_h_hdr,
    WMITLV_TAG_STRUC_wmi_capture_h_event_hdr,
    WMITLV_TAG_STRUC_WMI_VDEV_WNM_SLEEPMODE_CMD_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_wmm_addts_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_wmm_delts_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_set_wmm_params_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_tdls_set_state_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_tdls_peer_update_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_tdls_peer_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_tdls_peer_capabilities,
    WMITLV_TAG_STRUC_wmi_vdev_mcc_set_tbtt_mode_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_roam_chan_list_fixed_param,
    WMITLV_TAG_STRUC_wmi_vdev_mcc_bcn_intvl_change_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_resmgr_adaptive_ocs_disable_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_resmgr_set_chan_time_quota_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_resmgr_set_chan_latency_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_ba_req_ssn_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_ba_rsp_ssn_event_fixed_param,
    WMITLV_TAG_STRUC_wmi_sta_smps_force_mode_cmd_fixed_param,
    WMITLV_TAG_STRUC_WMI_SET_MCASTBCAST_FILTER_CMD_fixed_param,
    WMITLV_TAG_STRUC_wmi_p2p_set_oppps_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_p2p_set_noa_cmd_fixed_param,
    WMITLV_TAG_STRUC_wmi_ba_req_ssn_cmd_sub_struct_param,
    WMITLV_TAG_STRUC_wmi_ba_req_ssn_event_sub_struct_param,
    WMITLV_TAG_STRUC_wmi_sta_smps_param_cmd_fixed_param,
} WMITLV_TAG_ID;

/*
 * IMPORTANT: Please add _ALL_ WMI Commands Here.
 * Otherwise, these WMI TLV Functions will be process them.
 */
#define WMITLV_ALL_CMD_LIST(OP) \
    OP(WMI_INIT_CMDID) \
    OP(WMI_PEER_CREATE_CMDID) \
    OP(WMI_PEER_DELETE_CMDID) \
    OP(WMI_PEER_FLUSH_TIDS_CMDID) \
    OP(WMI_PEER_SET_PARAM_CMDID) \
    OP(WMI_STA_POWERSAVE_MODE_CMDID) \
    OP(WMI_STA_POWERSAVE_PARAM_CMDID) \
    OP(WMI_STA_DTIM_PS_METHOD_CMDID) \
    OP(WMI_PDEV_SET_REGDOMAIN_CMDID) \
    OP(WMI_PEER_TID_ADDBA_CMDID) \
    OP(WMI_PEER_TID_DELBA_CMDID) \
    OP(WMI_PDEV_FTM_INTG_CMDID) \
    OP(WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID) \
    OP(WMI_WOW_ENABLE_CMDID) \
    OP(WMI_RMV_BCN_FILTER_CMDID) \
    OP(WMI_ROAM_SCAN_MODE) \
    OP(WMI_ROAM_SCAN_RSSI_THRESHOLD) \
    OP(WMI_ROAM_SCAN_PERIOD) \
    OP(WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD) \
    OP(WMI_START_SCAN_CMDID) \
    OP(WMI_PDEV_SET_CHANNEL_CMDID) \
    OP(WMI_PDEV_SET_WMM_PARAMS_CMDID) \
    OP(WMI_VDEV_START_REQUEST_CMDID) \
    OP(WMI_VDEV_RESTART_REQUEST_CMDID) \
    OP(WMI_P2P_GO_SET_BEACON_IE) \
    OP(WMI_GTK_OFFLOAD_CMDID) \
    OP(WMI_SCAN_CHAN_LIST_CMDID) \
    OP(WMI_STA_UAPSD_AUTO_TRIG_CMDID) \
    OP(WMI_PRB_TMPL_CMDID) \
    OP(WMI_BCN_TMPL_CMDID) \
    OP(WMI_VDEV_INSTALL_KEY_CMDID) \
    OP(WMI_PEER_ASSOC_CMDID) \
    OP(WMI_ADD_BCN_FILTER_CMDID) \
    OP(WMI_STA_KEEPALIVE_CMDID) \
    OP(WMI_SET_ARP_NS_OFFLOAD_CMDID) \
    OP(WMI_P2P_SET_VENDOR_IE_DATA_CMDID) \
    OP(WMI_AP_PS_PEER_PARAM_CMDID) \
    OP(WMI_WLAN_PROFILE_TRIGGER_CMDID) \
    OP(WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID) \
    OP(WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID) \
    OP(WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID) \
    OP(WMI_WOW_DEL_WAKE_PATTERN_CMDID) \
    OP(WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID) \
    OP(WMI_RTT_MEASREQ_CMDID) \
    OP(WMI_RTT_TSF_CMDID) \
    OP(WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID) \
    OP(WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID) \
    OP(WMI_REQUEST_STATS_CMDID) \
    OP(WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID) \
    OP(WMI_CSA_OFFLOAD_ENABLE_CMDID) \
    OP(WMI_CSA_OFFLOAD_CHANSWITCH_CMDID) \
    OP(WMI_CHATTER_SET_MODE_CMDID) \
    OP(WMI_ECHO_CMDID) \
    OP(WMI_PDEV_UTF_CMDID) \
    OP(WMI_PDEV_QVIT_CMDID) \
    OP(WMI_VDEV_SET_KEEPALIVE_CMDID) \
    OP(WMI_VDEV_GET_KEEPALIVE_CMDID) \
    OP(WMI_FORCE_FW_HANG_CMDID) \
    OP(WMI_GPIO_CONFIG_CMDID) \
    OP(WMI_GPIO_OUTPUT_CMDID) \
    OP(WMI_PEER_ADD_WDS_ENTRY_CMDID) \
    OP(WMI_PEER_REMOVE_WDS_ENTRY_CMDID) \
    OP(WMI_BCN_TX_CMDID) \
    OP(WMI_PDEV_SEND_BCN_CMDID) \
    OP(WMI_MGMT_TX_CMDID) \
    OP(WMI_ADDBA_CLEAR_RESP_CMDID) \
    OP(WMI_ADDBA_SEND_CMDID) \
    OP(WMI_DELBA_SEND_CMDID) \
    OP(WMI_ADDBA_SET_RESP_CMDID) \
    OP(WMI_SEND_SINGLEAMSDU_CMDID) \
    OP(WMI_PDEV_PKTLOG_ENABLE_CMDID) \
    OP(WMI_PDEV_PKTLOG_DISABLE_CMDID) \
    OP(WMI_PDEV_SET_HT_CAP_IE_CMDID) \
    OP(WMI_PDEV_SET_VHT_CAP_IE_CMDID) \
    OP(WMI_PDEV_SET_DSCP_TID_MAP_CMDID) \
    OP(WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID) \
    OP(WMI_PDEV_GET_TPC_CONFIG_CMDID) \
    OP(WMI_PDEV_SET_BASE_MACADDR_CMDID) \
    OP(WMI_PEER_MCAST_GROUP_CMDID) \
    OP(WMI_ROAM_AP_PROFILE) \
    OP(WMI_SCAN_SCH_PRIO_TBL_CMDID) \
    OP(WMI_PDEV_DFS_ENABLE_CMDID) \
    OP(WMI_PDEV_DFS_DISABLE_CMDID) \
    OP(WMI_WOW_ADD_WAKE_PATTERN_CMDID) \
    OP(WMI_PDEV_SUSPEND_CMDID) \
    OP(WMI_PDEV_RESUME_CMDID) \
    OP(WMI_STOP_SCAN_CMDID) \
    OP(WMI_PDEV_SET_PARAM_CMDID) \
    OP(WMI_PDEV_SET_QUIET_MODE_CMDID) \
    OP(WMI_VDEV_CREATE_CMDID) \
    OP(WMI_VDEV_DELETE_CMDID) \
    OP(WMI_VDEV_UP_CMDID) \
    OP(WMI_VDEV_STOP_CMDID) \
    OP(WMI_VDEV_DOWN_CMDID) \
    OP(WMI_VDEV_SET_PARAM_CMDID) \
    OP(WMI_SCAN_UPDATE_REQUEST_CMDID) \
    OP(WMI_CHATTER_ADD_COALESCING_FILTER_CMDID) \
    OP(WMI_CHATTER_DELETE_COALESCING_FILTER_CMDID) \
    OP(WMI_CHATTER_COALESCING_QUERY_CMDID) \
    OP(WMI_TXBF_CMDID) \
    OP(WMI_DBGLOG_CFG_CMDID) \
    OP(WMI_VDEV_WNM_SLEEPMODE_CMDID) \
    OP(WMI_VDEV_WMM_ADDTS_CMDID) \
    OP(WMI_VDEV_WMM_DELTS_CMDID) \
    OP(WMI_VDEV_SET_WMM_PARAMS_CMDID) \
    OP(WMI_TDLS_SET_STATE_CMDID) \
    OP(WMI_TDLS_PEER_UPDATE_CMDID) \
    OP(WMI_FWTEST_VDEV_MCC_SET_TBTT_MODE_CMDID) \
    OP(WMI_ROAM_CHAN_LIST)  \
    OP(WMI_RESMGR_ADAPTIVE_OCS_DISABLE_CMDID)\
    OP(WMI_RESMGR_SET_CHAN_TIME_QUOTA_CMDID)    \
    OP(WMI_RESMGR_SET_CHAN_LATENCY_CMDID) \
    OP(WMI_BA_REQ_SSN_CMDID) \
    OP(WMI_STA_SMPS_FORCE_MODE_CMDID) \
    OP(WMI_SET_MCASTBCAST_FILTER_CMDID) \
    OP(WMI_P2P_SET_OPPPS_PARAM_CMDID) \
    OP(WMI_FWTEST_P2P_SET_NOA_PARAM_CMDID) \
    OP(WMI_STA_SMPS_PARAM_CMDID)
/*
 * IMPORTANT: Please add _ALL_ WMI Events Here.
 * Otherwise, these WMI TLV Functions will be process them.
 */
#define WMITLV_ALL_EVT_LIST(OP) \
    OP(WMI_SERVICE_READY_EVENTID) \
    OP(WMI_READY_EVENTID) \
    OP(WMI_SCAN_EVENTID) \
    OP(WMI_PDEV_TPC_CONFIG_EVENTID) \
    OP(WMI_CHAN_INFO_EVENTID) \
    OP(WMI_PHYERR_EVENTID) \
    OP(WMI_VDEV_START_RESP_EVENTID) \
    OP(WMI_VDEV_STOPPED_EVENTID) \
    OP(WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID) \
    OP(WMI_PEER_STA_KICKOUT_EVENTID) \
    OP(WMI_MGMT_RX_EVENTID) \
    OP(WMI_TBTTOFFSET_UPDATE_EVENTID) \
    OP(WMI_TX_DELBA_COMPLETE_EVENTID) \
    OP(WMI_TX_ADDBA_COMPLETE_EVENTID) \
    OP(WMI_ROAM_EVENTID) \
    OP(WMI_WOW_WAKEUP_HOST_EVENTID) \
    OP(WMI_RTT_ERROR_REPORT_EVENTID) \
    OP(WMI_ECHO_EVENTID) \
    OP(WMI_PDEV_FTM_INTG_EVENTID) \
    OP(WMI_VDEV_GET_KEEPALIVE_EVENTID) \
    OP(WMI_GPIO_INPUT_EVENTID) \
    OP(WMI_CSA_HANDLING_EVENTID) \
    OP(WMI_DEBUG_MESG_EVENTID) \
    OP(WMI_GTK_OFFLOAD_STATUS_EVENTID) \
    OP(WMI_DCS_INTERFERENCE_EVENTID) \
    OP(WMI_WLAN_PROFILE_DATA_EVENTID) \
    OP(WMI_PDEV_UTF_EVENTID) \
    OP(WMI_DEBUG_PRINT_EVENTID) \
    OP(WMI_RTT_MEASUREMENT_REPORT_EVENTID) \
    OP(WMI_HOST_SWBA_EVENTID) \
    OP(WMI_UPDATE_STATS_EVENTID) \
    OP(WMI_PDEV_QVIT_EVENTID) \
    OP(WMI_WLAN_FREQ_AVOID_EVENTID) \
    OP(WMI_GTK_REKEY_FAIL_EVENTID) \
    OP(WMI_NLO_MATCH_EVENTID) \
    OP(WMI_NLO_SCAN_COMPLETE_EVENTID) \
    OP(WMI_CHATTER_PC_QUERY_EVENTID) \
    OP(WMI_UPLOADH_EVENTID) \
    OP(WMI_CAPTUREH_EVENTID) \
    OP(WMI_TDLS_PEER_EVENTID) \
    OP(WMI_VDEV_MCC_BCN_INTERVAL_CHANGE_REQ_EVENTID) \
    OP(WMI_BA_RSP_SSN_EVENTID)

/* TLV definitions of WMI commands */

/* Init Cmd */
#define WMITLV_TABLE_WMI_INIT_CMDID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_init_cmd_fixed_param, wmi_init_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_resource_config, wmi_resource_config, resource_config, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wlan_host_memory_chunk, host_mem_chunks, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_INIT_CMDID);

/* Peer create Cmd */
#define WMITLV_TABLE_WMI_PEER_CREATE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_create_cmd_fixed_param, wmi_peer_create_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_CREATE_CMDID);

/* Peer delete Cmd */
#define WMITLV_TABLE_WMI_PEER_DELETE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_delete_cmd_fixed_param, wmi_peer_delete_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_DELETE_CMDID);

/* Peer flush Cmd*/
#define WMITLV_TABLE_WMI_PEER_FLUSH_TIDS_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_flush_tids_cmd_fixed_param, wmi_peer_flush_tids_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_FLUSH_TIDS_CMDID);

/* Peer Set Param Cmd */
#define WMITLV_TABLE_WMI_PEER_SET_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_set_param_cmd_fixed_param, wmi_peer_set_param_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_SET_PARAM_CMDID);

/* STA Powersave Mode Cmd */
#define WMITLV_TABLE_WMI_STA_POWERSAVE_MODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_powersave_mode_cmd_fixed_param, wmi_sta_powersave_mode_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_POWERSAVE_MODE_CMDID);

/* STA Powersave Param Cmd */
#define WMITLV_TABLE_WMI_STA_POWERSAVE_PARAM_CMDID(id,op,buf,len) \
            WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_powersave_param_cmd_fixed_param, wmi_sta_powersave_param_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_POWERSAVE_PARAM_CMDID);

/* STA DTIM PS METHOD Cmd */
#define WMITLV_TABLE_WMI_STA_DTIM_PS_METHOD_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_dtim_ps_method_cmd_fixed_param, wmi_sta_dtim_ps_method_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_DTIM_PS_METHOD_CMDID);

/* Pdev Set Reg Domain Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_REGDOMAIN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_regdomain_cmd_fixed_param, wmi_pdev_set_regdomain_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_REGDOMAIN_CMDID);


/* Peer TID ADD BA Cmd */
#define WMITLV_TABLE_WMI_PEER_TID_ADDBA_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_tid_addba_cmd_fixed_param, wmi_peer_tid_addba_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_TID_ADDBA_CMDID);

/* Peer TID DEL BA Cmd */
#define WMITLV_TABLE_WMI_PEER_TID_DELBA_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_tid_delba_cmd_fixed_param, wmi_peer_tid_delba_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_TID_DELBA_CMDID);
/* Peer Req Add BA Ssn for staId/tid pair Cmd */
#define WMITLV_TABLE_WMI_BA_REQ_SSN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ba_req_ssn_cmd_fixed_param, wmi_ba_req_ssn_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len,WMITLV_TAG_ARRAY_STRUC, wmi_ba_req_ssn, ba_req_ssn_list, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_BA_REQ_SSN_CMDID);

/* PDEV FTM integration Cmd */
#define WMITLV_TABLE_WMI_PDEV_FTM_INTG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ftm_intg_cmd_fixed_param, wmi_ftm_intg_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_FTM_INTG_CMDID);

/* WOW Wakeup from sleep Cmd */
#define WMITLV_TABLE_WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wow_hostwakeup_from_sleep_cmd_fixed_param, wmi_wow_hostwakeup_from_sleep_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID);

/* WOW Enable Cmd */
#define WMITLV_TABLE_WMI_WOW_ENABLE_CMDID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wow_enable_cmd_fixed_param, wmi_wow_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_WOW_ENABLE_CMDID);

/* Remove Bcn Filter Cmd */
#define WMITLV_TABLE_WMI_RMV_BCN_FILTER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_rmv_bcn_filter_cmd_fixed_param, wmi_rmv_bcn_filter_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_RMV_BCN_FILTER_CMDID);

/** Service bit WMI_SERVICE_ROAM_OFFLOAD for Roaming feature */
/* Roam scan mode Cmd */
#define WMITLV_TABLE_WMI_ROAM_SCAN_MODE(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_scan_mode_fixed_param, wmi_roam_scan_mode_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_start_scan_cmd_fixed_param, wmi_start_scan_cmd_fixed_param, scan_params, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_SCAN_MODE);

/* Roam scan Rssi Threshold Cmd */
#define WMITLV_TABLE_WMI_ROAM_SCAN_RSSI_THRESHOLD(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_scan_rssi_threshold_fixed_param, wmi_roam_scan_rssi_threshold_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_SCAN_RSSI_THRESHOLD);

/* Roam Scan Period Cmd */
#define WMITLV_TABLE_WMI_ROAM_SCAN_PERIOD(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_scan_period_fixed_param, wmi_roam_scan_period_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_SCAN_PERIOD);

/* Roam scan change Rssi Threshold Cmd */
#define WMITLV_TABLE_WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_scan_rssi_change_threshold_fixed_param, wmi_roam_scan_rssi_change_threshold_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD);
/* Roam Scan Channel list Cmd */
#define WMITLV_TABLE_WMI_ROAM_CHAN_LIST(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_chan_list_fixed_param, wmi_roam_chan_list_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, channel_list, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_CHAN_LIST);

/* Start scan Cmd */
#define WMITLV_TABLE_WMI_START_SCAN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_start_scan_cmd_fixed_param, wmi_start_scan_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, channel_list, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_FIXED_STRUC, wmi_ssid, ssid_list, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_FIXED_STRUC, wmi_mac_addr, bssid_list, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, ie_data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_START_SCAN_CMDID);

/* P2P set vendor ID data Cmd */
#define WMITLV_TABLE_WMI_P2P_SET_VENDOR_IE_DATA_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_p2p_set_vendor_ie_data_cmd_fixed_param, wmi_p2p_set_vendor_ie_data_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_P2P_SET_VENDOR_IE_DATA_CMDID);
/* P2P set OppPS parameters Cmd */
#define WMITLV_TABLE_WMI_P2P_SET_OPPPS_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_p2p_set_oppps_cmd_fixed_param, wmi_p2p_set_oppps_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_P2P_SET_OPPPS_PARAM_CMDID);

/* Pdev set channel Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_CHANNEL_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_channel, wmi_channel, chan, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_CHANNEL_CMDID);

/* Echo Cmd */
#define WMITLV_TABLE_WMI_ECHO_CMDID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_echo_cmd_fixed_param, wmi_echo_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_ECHO_CMDID);

/* Pdev set wmm params */
#define WMITLV_TABLE_WMI_PDEV_SET_WMM_PARAMS_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_wmm_params_cmd_fixed_param, wmi_pdev_set_wmm_params_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wmm_params, wmi_wmm_params, wmm_params_ac_be, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wmm_params, wmi_wmm_params, wmm_params_ac_bk, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wmm_params, wmi_wmm_params, wmm_params_ac_vi, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wmm_params, wmi_wmm_params, wmm_params_ac_vo, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_WMM_PARAMS_CMDID);

/* Vdev start request Cmd */
#define WMITLV_TABLE_WMI_VDEV_START_REQUEST_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_start_request_cmd_fixed_param, wmi_vdev_start_request_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_channel, wmi_channel, chan, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_p2p_noa_descriptor, noa_descriptors, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_START_REQUEST_CMDID);

/* Vdev restart request cmd */
#define WMITLV_TABLE_WMI_VDEV_RESTART_REQUEST_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_start_request_cmd_fixed_param, wmi_vdev_start_request_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_channel, wmi_channel, chan, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_p2p_noa_descriptor, noa_descriptors, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_RESTART_REQUEST_CMDID);

/* P2P Go set beacon IE cmd */
#define WMITLV_TABLE_WMI_P2P_GO_SET_BEACON_IE(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_p2p_go_set_beacon_ie_fixed_param, wmi_p2p_go_set_beacon_ie_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, ie_data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_P2P_GO_SET_BEACON_IE);

/* GTK offload Cmd */
#define WMITLV_TABLE_WMI_GTK_OFFLOAD_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_GTK_OFFLOAD_CMD_fixed_param, WMI_GTK_OFFLOAD_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_GTK_OFFLOAD_CMDID);

/* Scan channel list Cmd */
#define WMITLV_TABLE_WMI_SCAN_CHAN_LIST_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_scan_chan_list_cmd_fixed_param, wmi_scan_chan_list_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_channel, chan_info, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_SCAN_CHAN_LIST_CMDID);

/* STA UAPSD Auto trigger Cmd */
#define WMITLV_TABLE_WMI_STA_UAPSD_AUTO_TRIG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_uapsd_auto_trig_cmd_fixed_param, wmi_sta_uapsd_auto_trig_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_sta_uapsd_auto_trig_param, ac_param, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_UAPSD_AUTO_TRIG_CMDID);

/* Probe template Cmd */
#define WMITLV_TABLE_WMI_PRB_TMPL_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_prb_tmpl_cmd_fixed_param, wmi_prb_tmpl_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_bcn_prb_info, wmi_bcn_prb_info, bcn_prb_info, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_PRB_TMPL_CMDID);

/* Beacon template Cmd */
#define WMITLV_TABLE_WMI_BCN_TMPL_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_bcn_tmpl_cmd_fixed_param, wmi_bcn_tmpl_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_bcn_prb_info, wmi_bcn_prb_info, bcn_prb_info, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_BCN_TMPL_CMDID);

/* VDEV install key complete Cmd */
#define WMITLV_TABLE_WMI_VDEV_INSTALL_KEY_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_install_key_cmd_fixed_param, wmi_vdev_install_key_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, key_data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_INSTALL_KEY_CMDID);
/* VDEV WNM SLEEP MODE Cmd */
#define WMITLV_TABLE_WMI_VDEV_WNM_SLEEPMODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_VDEV_WNM_SLEEPMODE_CMD_fixed_param, WMI_VDEV_WNM_SLEEPMODE_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_WNM_SLEEPMODE_CMDID);

/* Peer Assoc Cmd */
#define WMITLV_TABLE_WMI_PEER_ASSOC_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_assoc_complete_cmd_fixed_param, wmi_peer_assoc_complete_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, peer_legacy_rates, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, peer_ht_rates, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vht_rate_set, wmi_vht_rate_set, peer_vht_rates, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_ASSOC_CMDID);

/* Add Beacon filter Cmd */
#define WMITLV_TABLE_WMI_ADD_BCN_FILTER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_add_bcn_filter_cmd_fixed_param, wmi_add_bcn_filter_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, ie_map, WMITLV_SIZE_FIX, BCN_FLT_MAX_ELEMS_IE_LIST)

WMITLV_CREATE_PARAM_STRUC(WMI_ADD_BCN_FILTER_CMDID);

/* Sta keepalive cmd */
#define WMITLV_TABLE_WMI_STA_KEEPALIVE_CMDID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_STA_KEEPALIVE_CMD_fixed_param, WMI_STA_KEEPALIVE_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_STA_KEEPALVE_ARP_RESPONSE, WMI_STA_KEEPALVE_ARP_RESPONSE, arp_resp, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_KEEPALIVE_CMDID);

/* ARP NS offload Cmd */
#define WMITLV_TABLE_WMI_SET_ARP_NS_OFFLOAD_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_SET_ARP_NS_OFFLOAD_CMD_fixed_param, WMI_SET_ARP_NS_OFFLOAD_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WMI_NS_OFFLOAD_TUPLE, ns_tuples, WMITLV_SIZE_FIX, WMI_MAX_NS_OFFLOADS) \
    WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WMI_ARP_OFFLOAD_TUPLE, arp_tuples, WMITLV_SIZE_FIX, WMI_MAX_ARP_OFFLOADS)

WMITLV_CREATE_PARAM_STRUC(WMI_SET_ARP_NS_OFFLOAD_CMDID);

/* AP PS peer param Cmd */
#define WMITLV_TABLE_WMI_AP_PS_PEER_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ap_ps_peer_cmd_fixed_param, wmi_ap_ps_peer_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_AP_PS_PEER_PARAM_CMDID);

/* Profile Trigger Cmd */
#define WMITLV_TABLE_WMI_WLAN_PROFILE_TRIGGER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wlan_profile_trigger_cmd_fixed_param, wmi_wlan_profile_trigger_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_PROFILE_TRIGGER_CMDID);

/* WLAN Profile set hist interval Cmd */
#define WMITLV_TABLE_WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wlan_profile_set_hist_intvl_cmd_fixed_param, wmi_wlan_profile_set_hist_intvl_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID);

/* WLAN Profile get profile data Cmd */
#define WMITLV_TABLE_WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wlan_profile_get_prof_data_cmd_fixed_param, wmi_wlan_profile_get_prof_data_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID);

/* WLAN Profile enable profile ID Cmd */
#define WMITLV_TABLE_WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wlan_profile_enable_profile_id_cmd_fixed_param, wmi_wlan_profile_enable_profile_id_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID);

/* WOW Delete Wake Pattern Cmd */
#define WMITLV_TABLE_WMI_WOW_DEL_WAKE_PATTERN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_WOW_DEL_PATTERN_CMD_fixed_param, WMI_WOW_DEL_PATTERN_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_WOW_DEL_WAKE_PATTERN_CMDID);

/* Wow enable/disable wake up Cmd */
#define WMITLV_TABLE_WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_WOW_ADD_DEL_EVT_CMD_fixed_param, WMI_WOW_ADD_DEL_EVT_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID);

/* RTT measurement request Cmd */
#define WMITLV_TABLE_WMI_RTT_MEASREQ_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_RTT_MEASREQ_CMDID);

/* RTT TSF Cmd */
#define WMITLV_TABLE_WMI_RTT_TSF_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_RTT_TSF_CMDID);

/* Spectral scan configure Cmd */
#define WMITLV_TABLE_WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_spectral_configure_cmd_fixed_param, wmi_vdev_spectral_configure_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID);

/* Spectral scan enable Cmd */
#define WMITLV_TABLE_WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_spectral_enable_cmd_fixed_param, wmi_vdev_spectral_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID);

/* Request stats Cmd */
#define WMITLV_TABLE_WMI_REQUEST_STATS_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_request_stats_cmd_fixed_param, wmi_request_stats_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_REQUEST_STATS_CMDID);

/* Netwrok list offload config Cmd */
#define WMITLV_TABLE_WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_nlo_config_cmd_fixed_param, wmi_nlo_config_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, nlo_configured_parameters, nlo_list, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, channel_list, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID);

/* CSA offload enable Cmd */
#define WMITLV_TABLE_WMI_CSA_OFFLOAD_ENABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_csa_offload_enable_cmd_fixed_param, wmi_csa_offload_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_CSA_OFFLOAD_ENABLE_CMDID);

/* CSA offload channel switch Cmd */
#define WMITLV_TABLE_WMI_CSA_OFFLOAD_CHANSWITCH_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_csa_offload_chanswitch_cmd_fixed_param, wmi_csa_offload_chanswitch_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_channel, wmi_channel, chan, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_CSA_OFFLOAD_CHANSWITCH_CMDID);

/* Chatter set mode Cmd */
#define WMITLV_TABLE_WMI_CHATTER_SET_MODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_chatter_set_mode_cmd_fixed_param, wmi_chatter_set_mode_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_CHATTER_SET_MODE_CMDID);


/* PDEV UTF Cmd */
#define WMITLV_TABLE_WMI_PDEV_UTF_CMDID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_UTF_CMDID);

/* PDEV QVIT Cmd */
#define WMITLV_TABLE_WMI_PDEV_QVIT_CMDID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_QVIT_CMDID);

/* Vdev Set keep alive Cmd  */
#define WMITLV_TABLE_WMI_VDEV_SET_KEEPALIVE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_set_keepalive_cmd_fixed_param, wmi_vdev_set_keepalive_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_SET_KEEPALIVE_CMDID);

/* Vdev Get keep alive Cmd  */
#define WMITLV_TABLE_WMI_VDEV_GET_KEEPALIVE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_get_keepalive_cmd_fixed_param, wmi_vdev_get_keepalive_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_GET_KEEPALIVE_CMDID);
/*FWTEST Set TBTT mode Cmd*/
#define WMITLV_TABLE_WMI_FWTEST_VDEV_MCC_SET_TBTT_MODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_mcc_set_tbtt_mode_cmd_fixed_param, wmi_vdev_mcc_set_tbtt_mode_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_FWTEST_VDEV_MCC_SET_TBTT_MODE_CMDID);

/* FWTEST set NoA parameters Cmd */
#define WMITLV_TABLE_WMI_FWTEST_P2P_SET_NOA_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_p2p_set_noa_cmd_fixed_param, wmi_p2p_set_noa_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_p2p_noa_descriptor, noa_descriptor, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_FWTEST_P2P_SET_NOA_PARAM_CMDID);

/* Force Fw Hang Cmd */
#define WMITLV_TABLE_WMI_FORCE_FW_HANG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_FORCE_FW_HANG_CMD_fixed_param, WMI_FORCE_FW_HANG_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_FORCE_FW_HANG_CMDID);
/* Set Mcast address Cmd */
#define WMITLV_TABLE_WMI_SET_MCASTBCAST_FILTER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_SET_MCASTBCAST_FILTER_CMD_fixed_param, WMI_SET_MCASTBCAST_FILTER_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_SET_MCASTBCAST_FILTER_CMDID);

/* GPIO config Cmd */
#define WMITLV_TABLE_WMI_GPIO_CONFIG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_gpio_config_cmd_fixed_param, wmi_gpio_config_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_GPIO_CONFIG_CMDID);

/* GPIO output Cmd */
#define WMITLV_TABLE_WMI_GPIO_OUTPUT_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_gpio_output_cmd_fixed_param, wmi_gpio_output_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_GPIO_OUTPUT_CMDID);

/* Peer add WDA entry Cmd */
#define WMITLV_TABLE_WMI_PEER_ADD_WDS_ENTRY_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_add_wds_entry_cmd_fixed_param, wmi_peer_add_wds_entry_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_ADD_WDS_ENTRY_CMDID);

/*Peer remove WDS entry Cmd */
#define WMITLV_TABLE_WMI_PEER_REMOVE_WDS_ENTRY_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_remove_wds_entry_cmd_fixed_param, wmi_peer_remove_wds_entry_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_REMOVE_WDS_ENTRY_CMDID);

/* Beacon tx Cmd */
#define WMITLV_TABLE_WMI_BCN_TX_CMDID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_bcn_tx_hdr, wmi_bcn_tx_hdr, hdr, WMITLV_SIZE_FIX)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_BCN_TX_CMDID);

/* PDEV send Beacon Cmd */
#define WMITLV_TABLE_WMI_PDEV_SEND_BCN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_bcn_send_from_host_cmd_fixed_param, wmi_bcn_send_from_host_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SEND_BCN_CMDID);

/* Management tx Cmd */
#define WMITLV_TABLE_WMI_MGMT_TX_CMDID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_mgmt_tx_hdr, wmi_mgmt_tx_hdr, hdr, WMITLV_SIZE_FIX)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_MGMT_TX_CMDID);

/* ADD clear response Cmd */
#define WMITLV_TABLE_WMI_ADDBA_CLEAR_RESP_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_addba_clear_resp_cmd_fixed_param, wmi_addba_clear_resp_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ADDBA_CLEAR_RESP_CMDID);

/* ADD BA send Cmd */
#define WMITLV_TABLE_WMI_ADDBA_SEND_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_addba_send_cmd_fixed_param, wmi_addba_send_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ADDBA_SEND_CMDID);

/* DEL BA send Cmd */
#define WMITLV_TABLE_WMI_DELBA_SEND_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_delba_send_cmd_fixed_param, wmi_delba_send_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_DELBA_SEND_CMDID);

/* ADD BA set response Cmd */
#define WMITLV_TABLE_WMI_ADDBA_SET_RESP_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_addba_setresponse_cmd_fixed_param, wmi_addba_setresponse_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ADDBA_SET_RESP_CMDID);

/* Send single AMSDU Cmd */
#define WMITLV_TABLE_WMI_SEND_SINGLEAMSDU_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_send_singleamsdu_cmd_fixed_param, wmi_send_singleamsdu_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_SEND_SINGLEAMSDU_CMDID);

/* PDev Packet Log enable Cmd */
#define WMITLV_TABLE_WMI_PDEV_PKTLOG_ENABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_pktlog_enable_cmd_fixed_param, wmi_pdev_pktlog_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_PKTLOG_ENABLE_CMDID);

/* PDev Packet Log disable Cmd */
#define WMITLV_TABLE_WMI_PDEV_PKTLOG_DISABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_pktlog_disable_cmd_fixed_param, wmi_pdev_pktlog_disable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_PKTLOG_DISABLE_CMDID);

/* PDev set HT Cap IE Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_HT_CAP_IE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_ht_ie_cmd_fixed_param, wmi_pdev_set_ht_ie_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, ie_data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_HT_CAP_IE_CMDID);

/* PDev set VHT Cap IE Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_VHT_CAP_IE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_vht_ie_cmd_fixed_param, wmi_pdev_set_vht_ie_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, ie_data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_VHT_CAP_IE_CMDID);

/* PDev Set DSCP to TID map Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_DSCP_TID_MAP_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_dscp_tid_map_cmd_fixed_param, wmi_pdev_set_dscp_tid_map_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_DSCP_TID_MAP_CMDID);

/* PDev Green AP PS enable Cmd */
#define WMITLV_TABLE_WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_green_ap_ps_enable_cmd_fixed_param, wmi_pdev_green_ap_ps_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID);

/* PDEV Get TPC Config Cmd */
#define WMITLV_TABLE_WMI_PDEV_GET_TPC_CONFIG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_get_tpc_config_cmd_fixed_param, wmi_pdev_get_tpc_config_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_GET_TPC_CONFIG_CMDID);

/* PDEV Set Base Mac Address Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_BASE_MACADDR_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_base_macaddr_cmd_fixed_param, wmi_pdev_set_base_macaddr_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_BASE_MACADDR_CMDID);

/* Peer multicast group Cmd */
#define WMITLV_TABLE_WMI_PEER_MCAST_GROUP_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_mcast_group_cmd_fixed_param, wmi_peer_mcast_group_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PEER_MCAST_GROUP_CMDID);

/* Roam AP profile Cmd */
#define WMITLV_TABLE_WMI_ROAM_AP_PROFILE(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_ap_profile_fixed_param, wmi_roam_ap_profile_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ap_profile, wmi_ap_profile, ap_profile, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_AP_PROFILE);

/* Scan scheduler priority Table Cmd */
#define WMITLV_TABLE_WMI_SCAN_SCH_PRIO_TBL_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_scan_sch_priority_table_cmd_fixed_param, wmi_scan_sch_priority_table_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, mapping_table, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_SCAN_SCH_PRIO_TBL_CMDID);

/* PDEV DFS enable Cmd */
#define WMITLV_TABLE_WMI_PDEV_DFS_ENABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_dfs_enable_cmd_fixed_param, wmi_pdev_dfs_enable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_DFS_ENABLE_CMDID);

/* PDEV DFS disable Cmd */
#define WMITLV_TABLE_WMI_PDEV_DFS_DISABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_dfs_disable_cmd_fixed_param, wmi_pdev_dfs_disable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_DFS_DISABLE_CMDID);

/* WOW Add Wake Pattern Cmd */
#define WMITLV_TABLE_WMI_WOW_ADD_WAKE_PATTERN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_WOW_ADD_PATTERN_CMD_fixed_param, WMI_WOW_ADD_PATTERN_CMD_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WOW_BITMAP_PATTERN_T, pattern_info_bitmap, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WOW_IPV4_SYNC_PATTERN_T, pattern_info_ipv4, WMITLV_SIZE_VAR)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WOW_IPV6_SYNC_PATTERN_T, pattern_info_ipv6, WMITLV_SIZE_VAR)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WOW_MAGIC_PATTERN_CMD, pattern_info_magic_pattern, WMITLV_SIZE_VAR)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, pattern_info_timeout, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_WOW_ADD_WAKE_PATTERN_CMDID);



/* Stop scan Cmd */
#define WMITLV_TABLE_WMI_STOP_SCAN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_stop_scan_cmd_fixed_param, wmi_stop_scan_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STOP_SCAN_CMDID);

#define WMITLV_TABLE_WMI_PDEV_SET_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_param_cmd_fixed_param, wmi_pdev_set_param_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_PARAM_CMDID);

/* PDev set quiet Cmd */
#define WMITLV_TABLE_WMI_PDEV_SET_QUIET_MODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_set_quiet_cmd_fixed_param, wmi_pdev_set_quiet_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SET_QUIET_MODE_CMDID);

/* Vdev create Cmd */
#define WMITLV_TABLE_WMI_VDEV_CREATE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_create_cmd_fixed_param, wmi_vdev_create_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_CREATE_CMDID);

/* Vdev delete Cmd */
#define WMITLV_TABLE_WMI_VDEV_DELETE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_delete_cmd_fixed_param, wmi_vdev_delete_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_DELETE_CMDID);

/* Vdev up Cmd */
#define WMITLV_TABLE_WMI_VDEV_UP_CMDID(id,op,buf,len)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_up_cmd_fixed_param, wmi_vdev_up_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_UP_CMDID);

/* Vdev stop cmd */
#define WMITLV_TABLE_WMI_VDEV_STOP_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_stop_cmd_fixed_param, wmi_vdev_stop_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_STOP_CMDID);

/* Vdev down Cmd */
#define WMITLV_TABLE_WMI_VDEV_DOWN_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_down_cmd_fixed_param, wmi_vdev_down_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_DOWN_CMDID);

/* Vdev set param Cmd */
#define WMITLV_TABLE_WMI_VDEV_SET_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_set_param_cmd_fixed_param, wmi_vdev_set_param_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_SET_PARAM_CMDID);

/* Pdev suspend Cmd */
#define WMITLV_TABLE_WMI_PDEV_SUSPEND_CMDID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_suspend_cmd_fixed_param, wmi_pdev_suspend_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_SUSPEND_CMDID);

/* Pdev Resume Cmd */
#define WMITLV_TABLE_WMI_PDEV_RESUME_CMDID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_resume_cmd_fixed_param, wmi_pdev_resume_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_RESUME_CMDID);

#define WMITLV_TABLE_WMI_SCAN_UPDATE_REQUEST_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_scan_update_request_cmd_fixed_param, wmi_scan_update_request_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_SCAN_UPDATE_REQUEST_CMDID);

#define WMITLV_TABLE_WMI_CHATTER_ADD_COALESCING_FILTER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len,WMITLV_TAG_STRUC_wmi_chatter_coalescing_add_filter_cmd_fixed_param, wmi_chatter_coalescing_add_filter_cmd_fixed_param, fixed_param,WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, chatter_pkt_coalescing_filter, coalescing_filter, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_CHATTER_ADD_COALESCING_FILTER_CMDID);

#define WMITLV_TABLE_WMI_CHATTER_DELETE_COALESCING_FILTER_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_chatter_coalescing_delete_filter_cmd_fixed_param,wmi_chatter_coalescing_delete_filter_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \

WMITLV_CREATE_PARAM_STRUC(WMI_CHATTER_DELETE_COALESCING_FILTER_CMDID);

#define WMITLV_TABLE_WMI_CHATTER_COALESCING_QUERY_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_chatter_coalescing_query_cmd_fixed_param, wmi_chatter_coalescing_query_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \

WMITLV_CREATE_PARAM_STRUC(WMI_CHATTER_COALESCING_QUERY_CMDID);

#define WMITLV_TABLE_WMI_TXBF_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len,WMITLV_TAG_STRUC_wmi_txbf_cmd_fixed_param, wmi_txbf_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \

WMITLV_CREATE_PARAM_STRUC(WMI_TXBF_CMDID);

#define WMITLV_TABLE_WMI_DBGLOG_CFG_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_debug_log_config_cmd_fixed_param, wmi_debug_log_config_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_FXAR(id,op,buf,len,WMITLV_TAG_ARRAY_UINT32, A_UINT32, module_id_bitmap, WMITLV_SIZE_FIX, MAX_MODULE_ID_BITMAP_WORDS) \

WMITLV_CREATE_PARAM_STRUC(WMI_DBGLOG_CFG_CMDID);

#define WMITLV_TABLE_WMI_VDEV_WMM_ADDTS_CMDID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_wmm_addts_cmd_fixed_param, wmi_vdev_wmm_addts_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_WMM_ADDTS_CMDID);

#define WMITLV_TABLE_WMI_VDEV_WMM_DELTS_CMDID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_wmm_delts_cmd_fixed_param, wmi_vdev_wmm_delts_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_WMM_DELTS_CMDID);

#define WMITLV_TABLE_WMI_VDEV_SET_WMM_PARAMS_CMDID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_set_wmm_params_cmd_fixed_param, wmi_vdev_set_wmm_params_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_SET_WMM_PARAMS_CMDID);

/* TDLS Enable/Disable Cmd */
#define WMITLV_TABLE_WMI_TDLS_SET_STATE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tdls_set_state_cmd_fixed_param, \
            wmi_tdls_set_state_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_TDLS_SET_STATE_CMDID);

/* TDLS Peer Update Cmd */
#define WMITLV_TABLE_WMI_TDLS_PEER_UPDATE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tdls_peer_update_cmd_fixed_param, wmi_tdls_peer_update_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tdls_peer_capabilities, wmi_tdls_peer_capabilities, peer_caps, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_TDLS_PEER_UPDATE_CMDID);

/* Resmgr Disable Adaptive OCS CMD */
#define WMITLV_TABLE_WMI_RESMGR_ADAPTIVE_OCS_DISABLE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_resmgr_adaptive_ocs_disable_cmd_fixed_param, \
            wmi_resmgr_adaptive_ocs_disable_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_RESMGR_ADAPTIVE_OCS_DISABLE_CMDID);

/* Resmgr Set Channel Time Quota CMD */
#define WMITLV_TABLE_WMI_RESMGR_SET_CHAN_TIME_QUOTA_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_resmgr_set_chan_time_quota_cmd_fixed_param, \
            wmi_resmgr_set_chan_time_quota_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_RESMGR_SET_CHAN_TIME_QUOTA_CMDID);

/* Resmgr Set Channel Latency CMD */
#define WMITLV_TABLE_WMI_RESMGR_SET_CHAN_LATENCY_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_resmgr_set_chan_latency_cmd_fixed_param, \
            wmi_resmgr_set_chan_latency_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)  \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_RESMGR_SET_CHAN_LATENCY_CMDID);

/* STA SMPS Force Mode CMD */
#define WMITLV_TABLE_WMI_STA_SMPS_FORCE_MODE_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_smps_force_mode_cmd_fixed_param, \
            wmi_sta_smps_force_mode_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_SMPS_FORCE_MODE_CMDID);

/* STA SMPS Param CMD */
#define WMITLV_TABLE_WMI_STA_SMPS_PARAM_CMDID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_sta_smps_param_cmd_fixed_param, \
            wmi_sta_smps_param_cmd_fixed_param, fixed_param, WMITLV_SIZE_FIX)

WMITLV_CREATE_PARAM_STRUC(WMI_STA_SMPS_PARAM_CMDID);

/************************** TLV definitions of WMI events *******************************/

/* Service Ready event */
#define WMITLV_TABLE_WMI_SERVICE_READY_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_service_ready_event_fixed_param, wmi_service_ready_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)     \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_HAL_REG_CAPABILITIES, HAL_REG_CAPABILITIES, hal_reg_capabilities, WMITLV_SIZE_FIX) \
    WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, wmi_service_bitmap, WMITLV_SIZE_FIX, WMI_SERVICE_BM_SIZE) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wlan_host_mem_req, mem_reqs, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_SERVICE_READY_EVENTID);

/* Ready event */
#define WMITLV_TABLE_WMI_READY_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ready_event_fixed_param, wmi_ready_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_READY_EVENTID);

/* Scan Event */
#define WMITLV_TABLE_WMI_SCAN_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_scan_event_fixed_param, wmi_scan_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_SCAN_EVENTID);

/* PDEV TPC Config Event */
#define WMITLV_TABLE_WMI_PDEV_TPC_CONFIG_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_pdev_tpc_config_event_fixed_param, wmi_pdev_tpc_config_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, ratesArray, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_TPC_CONFIG_EVENTID);

/* Channel Info Event */
#define WMITLV_TABLE_WMI_CHAN_INFO_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_chan_info_event_fixed_param, wmi_chan_info_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_CHAN_INFO_EVENTID);

/* Phy Error Event */
#define WMITLV_TABLE_WMI_PHYERR_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_comb_phyerr_rx_hdr, wmi_comb_phyerr_rx_hdr, hdr, WMITLV_SIZE_FIX)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PHYERR_EVENTID);

/* VDEV Start response Event */
#define WMITLV_TABLE_WMI_VDEV_START_RESP_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_start_response_event_fixed_param, wmi_vdev_start_response_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_START_RESP_EVENTID);

/* VDEV Stopped Event */
#define WMITLV_TABLE_WMI_VDEV_STOPPED_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_stopped_event_fixed_param, wmi_vdev_stopped_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_STOPPED_EVENTID);

/* VDEV Install Key Complete Event */
#define WMITLV_TABLE_WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_install_key_complete_event_fixed_param, wmi_vdev_install_key_complete_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID);

/* Peer STA Kickout Event */
#define WMITLV_TABLE_WMI_PEER_STA_KICKOUT_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_peer_sta_kickout_event_fixed_param, wmi_peer_sta_kickout_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_PEER_STA_KICKOUT_EVENTID);

/* Management Rx Event */
#define WMITLV_TABLE_WMI_MGMT_RX_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_mgmt_rx_hdr, wmi_mgmt_rx_hdr, hdr, WMITLV_SIZE_FIX)   \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_MGMT_RX_EVENTID);

/* TBTT offset Event */
#define WMITLV_TABLE_WMI_TBTTOFFSET_UPDATE_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tbtt_offset_event_fixed_param, wmi_tbtt_offset_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_FXAR(id,op,buf,len, WMITLV_TAG_ARRAY_UINT32, A_UINT32, tbttoffset_list, WMITLV_SIZE_FIX, WMI_MAX_AP_VDEV)
WMITLV_CREATE_PARAM_STRUC(WMI_TBTTOFFSET_UPDATE_EVENTID);

/* TX DELBA Complete Event */
#define WMITLV_TABLE_WMI_TX_DELBA_COMPLETE_EVENTID(id,op,buf,len)                                           \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tx_delba_complete_event_fixed_param, wmi_tx_delba_complete_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_TX_DELBA_COMPLETE_EVENTID);

/* Tx ADDBA Complete Event */
#define WMITLV_TABLE_WMI_TX_ADDBA_COMPLETE_EVENTID(id,op,buf,len)                                       \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tx_addba_complete_event_fixed_param, wmi_tx_addba_complete_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_TX_ADDBA_COMPLETE_EVENTID);

/* ADD BA Req ssn Event */
#define WMITLV_TABLE_WMI_BA_RSP_SSN_EVENTID(id,op,buf,len)                                       \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ba_rsp_ssn_event_fixed_param, wmi_ba_rsp_ssn_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id, op, buf, len, WMITLV_TAG_ARRAY_STRUC, wmi_ba_event_ssn, ba_event_ssn_list, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_BA_RSP_SSN_EVENTID);
/* Roam Event */
#define WMITLV_TABLE_WMI_ROAM_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_roam_event_fixed_param, wmi_roam_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_ROAM_EVENTID);

/* WOW Wakeup Host Event */
/* NOTE: Make sure wow_bitmap_info can be zero or one elements only */
#define WMITLV_TABLE_WMI_WOW_WAKEUP_HOST_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WOW_EVENT_INFO_fixed_param, WOW_EVENT_INFO_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WOW_EVENT_INFO_SECTION_BITMAP, wow_bitmap_info, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, wow_packet_buffer, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, WMI_GTK_OFFLOAD_STATUS_EVENT_fixed_param, wow_gtkigtk, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_WOW_WAKEUP_HOST_EVENTID);

/* RTT error report Event */
#define WMITLV_TABLE_WMI_RTT_ERROR_REPORT_EVENTID(id,op,buf,len)    \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_RTT_ERROR_REPORT_EVENTID);

/* Echo Event */
#define WMITLV_TABLE_WMI_ECHO_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_echo_event_fixed_param, wmi_echo_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_ECHO_EVENTID);

/* FTM Integration Event */
#define WMITLV_TABLE_WMI_PDEV_FTM_INTG_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_ftm_intg_event_fixed_param, wmi_ftm_intg_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_FTM_INTG_EVENTID);

/* VDEV get Keepalive Event */
#define WMITLV_TABLE_WMI_VDEV_GET_KEEPALIVE_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_get_keepalive_event_fixed_param, wmi_vdev_get_keepalive_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_GET_KEEPALIVE_EVENTID);

/* GPIO Input Event */
#define WMITLV_TABLE_WMI_GPIO_INPUT_EVENTID(id,op,buf,len)  \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_gpio_input_event_fixed_param, wmi_gpio_input_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_GPIO_INPUT_EVENTID);

/* CSA Handling Event */
#define WMITLV_TABLE_WMI_CSA_HANDLING_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_csa_event_fixed_param, wmi_csa_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_CSA_HANDLING_EVENTID);

/* Debug Message Event */
#define WMITLV_TABLE_WMI_DEBUG_MESG_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_DEBUG_MESG_EVENTID);

/* IGTK Offload Event */
#define WMITLV_TABLE_WMI_GTK_OFFLOAD_STATUS_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_WMI_GTK_OFFLOAD_STATUS_EVENT_fixed_param, WMI_GTK_OFFLOAD_STATUS_EVENT_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_GTK_OFFLOAD_STATUS_EVENTID);

/* DCA interferance Event */
#define WMITLV_TABLE_WMI_DCS_INTERFERENCE_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_dcs_interference_event_fixed_param, wmi_dcs_interference_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, ath_dcs_cw_int, cw_int, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wlan_dcs_im_tgt_stats_t, wlan_stat, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_DCS_INTERFERENCE_EVENTID);

/* Profile data Event */
#define WMITLV_TABLE_WMI_WLAN_PROFILE_DATA_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_wlan_profile_ctx_t, wmi_wlan_profile_ctx_t, profile_ctx, WMITLV_SIZE_FIX)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_wlan_profile_t, profile_data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_PROFILE_DATA_EVENTID);

/* PDEV UTF Event */
#define WMITLV_TABLE_WMI_PDEV_UTF_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_UTF_EVENTID);

/* Debug print Event */
#define WMITLV_TABLE_WMI_DEBUG_PRINT_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_DEBUG_PRINT_EVENTID);

/* RTT measurement report Event */
#define WMITLV_TABLE_WMI_RTT_MEASUREMENT_REPORT_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_RTT_MEASUREMENT_REPORT_EVENTID);

/* HOST SWBA Event */
#define WMITLV_TABLE_WMI_HOST_SWBA_EVENTID(id,op,buf,len) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_host_swba_event_fixed_param, wmi_host_swba_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_tim_info, tim_info, WMITLV_SIZE_VAR) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_p2p_noa_info, p2p_noa_info, WMITLV_SIZE_VAR)

WMITLV_CREATE_PARAM_STRUC(WMI_HOST_SWBA_EVENTID);


/* Update stats Event */
#define WMITLV_TABLE_WMI_UPDATE_STATS_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_stats_event_fixed_param, wmi_stats_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_UPDATE_STATS_EVENTID);

/* PDEV QVIT Event */
#define WMITLV_TABLE_WMI_PDEV_QVIT_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, data, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_PDEV_QVIT_EVENTID);

/* WLAN Frequency avoid Event */
#define WMITLV_TABLE_WMI_WLAN_FREQ_AVOID_EVENTID(id,op,buf,len)\
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_avoid_freq_ranges_event_fixed_param, wmi_avoid_freq_ranges_event_fixed_param, fixed_param, WMITLV_SIZE_FIX) \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_STRUC, wmi_avoid_freq_range_desc, avd_freq_range, WMITLV_SIZE_VAR)
WMITLV_CREATE_PARAM_STRUC(WMI_WLAN_FREQ_AVOID_EVENTID);

/* GTK rekey fail Event */
#define WMITLV_TABLE_WMI_GTK_REKEY_FAIL_EVENTID(id,op,buf,len)                                                                                                 \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_gtk_rekey_fail_event_fixed_param, wmi_gtk_rekey_fail_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_GTK_REKEY_FAIL_EVENTID);

/* NLO match event */
#define WMITLV_TABLE_WMI_NLO_MATCH_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_nlo_event, wmi_nlo_event, fixed_param, WMITLV_SIZE_FIX)
    WMITLV_CREATE_PARAM_STRUC(WMI_NLO_MATCH_EVENTID);

/* NLO scan complete event */
#define WMITLV_TABLE_WMI_NLO_SCAN_COMPLETE_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_nlo_event, wmi_nlo_event, fixed_param, WMITLV_SIZE_FIX)
    WMITLV_CREATE_PARAM_STRUC(WMI_NLO_SCAN_COMPLETE_EVENTID);

/* Chatter query reply event */
#define WMITLV_TABLE_WMI_CHATTER_PC_QUERY_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_chatter_query_reply_event_fixed_param, wmi_chatter_query_reply_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
    WMITLV_CREATE_PARAM_STRUC(WMI_CHATTER_PC_QUERY_EVENTID);

/* Upload H_CV info event */
#define WMITLV_TABLE_WMI_UPLOADH_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_upload_h_hdr, wmi_upload_h_hdr, hdr, WMITLV_SIZE_FIX)   \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_ARRAY_BYTE, A_UINT8, bufp, WMITLV_SIZE_VAR)
    WMITLV_CREATE_PARAM_STRUC(WMI_UPLOADH_EVENTID);

/* Capture H info event */
#define WMITLV_TABLE_WMI_CAPTUREH_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_capture_h_event_hdr, wmi_capture_h_event_hdr, fixed_param, WMITLV_SIZE_FIX)
    WMITLV_CREATE_PARAM_STRUC(WMI_CAPTUREH_EVENTID);
/* TDLS Peer Update event */
#define WMITLV_TABLE_WMI_TDLS_PEER_EVENTID(id,op,buf,len)                                                                                                 \
WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_tdls_peer_event_fixed_param, wmi_tdls_peer_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
    WMITLV_CREATE_PARAM_STRUC(WMI_TDLS_PEER_EVENTID);

/* VDEV MCC Beacon Interval Change Request Event */
#define WMITLV_TABLE_WMI_VDEV_MCC_BCN_INTERVAL_CHANGE_REQ_EVENTID(id,op,buf,len)                                                         \
    WMITLV_ELEM(id,op,buf,len, WMITLV_TAG_STRUC_wmi_vdev_mcc_bcn_intvl_change_event_fixed_param, wmi_vdev_mcc_bcn_intvl_change_event_fixed_param, fixed_param, WMITLV_SIZE_FIX)
WMITLV_CREATE_PARAM_STRUC(WMI_VDEV_MCC_BCN_INTERVAL_CHANGE_REQ_EVENTID);

#ifdef __cplusplus
}
#endif

#endif /*_WMI_TLV_DEFS_H_*/

