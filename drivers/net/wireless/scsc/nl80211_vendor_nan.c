/*****************************************************************************
 *
 * Copyright (c) 2014 - 2019 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include "cfg80211_ops.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"

struct net_device *slsi_nan_get_netdev(struct slsi_dev *sdev)
{
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= 4
	return slsi_get_netdev(sdev, SLSI_NET_INDEX_NAN);
#else
	return NULL;
#endif
}

static int slsi_nan_get_new_id(u32 id_map, int max_ids)
{
	int i;

	for (i = 1; i <= max_ids; i++) {
		if (!(id_map & BIT(i)))
			return i;
	}
	return 0;
}

static int slsi_nan_get_new_publish_id(struct netdev_vif *ndev_vif)
{
	return slsi_nan_get_new_id(ndev_vif->nan.publish_id_map, SLSI_NAN_MAX_PUBLISH_ID);
}

static int slsi_nan_get_new_subscribe_id(struct netdev_vif *ndev_vif)
{
	return slsi_nan_get_new_id(ndev_vif->nan.subscribe_id_map, SLSI_NAN_MAX_SUBSCRIBE_ID);
}

static bool slsi_nan_is_publish_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	return ndev_vif->nan.publish_id_map & BIT(id);
}

static bool slsi_nan_is_subscribe_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	return ndev_vif->nan.subscribe_id_map & BIT(id);
}

/*Note: vendor IE length of the stitched info may not be correct. Caller
 *      has to depend on return value for full length of IE.
 */
static u32 slsi_nan_stitch_ie(u8 *buff, u32 buff_len, u16 oui_type_subtype, u8 *dest_buff)
{
	u8 samsung_oui[] = {0x00, 0x16, 0x32};
	u8 *pos = buff;
	u32 dest_buff_len = 0;

	while (buff_len - (pos - buff) > 2 + sizeof(samsung_oui) + 2) {
		/* check if buffer is alteast al long as IE length */
		if (pos[1] + 2 + pos - buff > buff_len) {
			pos = buff + buff_len;
			continue;
		}
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC &&
		    memcmp(samsung_oui, &pos[2], sizeof(samsung_oui)) == 0 &&
		    memcmp((u8 *)&oui_type_subtype, &pos[5], sizeof(oui_type_subtype)) == 0) {
			if (!dest_buff_len) {
				memcpy(dest_buff, pos, pos[1] + 2);
				dest_buff_len = pos[1] + 2;
			} else {
				memcpy(&dest_buff[dest_buff_len], &pos[7], pos[1] - 5);
				dest_buff_len += pos[1] - 5;
			}
		}
		pos += pos[1] + 2;
	}

	return dest_buff_len;
}

void slsi_nan_get_mac(struct slsi_dev *sdev, char *nan_mac_addr)
{
	memset(nan_mac_addr, 0, ETH_ALEN);
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= 4
	if (slsi_dev_nan_supported(sdev))
		ether_addr_copy(nan_mac_addr, sdev->netdev_addresses[SLSI_NET_INDEX_NAN]);
#endif
}

static void slsi_vendor_nan_command_reply(struct wiphy *wiphy, u32 status, u32 error, u32 response_type,
					  u16 publish_subscribe_id, struct slsi_hal_nan_capabilities *capabilities)
{
	int reply_len;
	struct sk_buff  *reply;

	reply_len = SLSI_NL_VENDOR_REPLY_OVERHEAD + SLSI_NL_ATTRIBUTE_U32_LEN *
		    (3 + sizeof(struct slsi_hal_nan_capabilities));
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, reply_len);
	if (!reply) {
		SLSI_WARN_NODEV("SKB alloc failed for vendor_cmd reply\n");
		return;
	}

	nla_put_u32(reply, NAN_REPLY_ATTR_STATUS_TYPE, status);
	nla_put_u32(reply, NAN_REPLY_ATTR_VALUE, error);
	nla_put_u32(reply, NAN_REPLY_ATTR_RESPONSE_TYPE, response_type);

	if (capabilities) {
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_CONCURRENT_CLUSTER,
			    capabilities->max_concurrent_nan_clusters);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_PUBLISHES, capabilities->max_publishes);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SUBSCRIBES, capabilities->max_subscribes);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_NAME_LEN, capabilities->max_service_name_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MATCH_FILTER_LEN, capabilities->max_match_filter_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_TOTAL_MATCH_FILTER_LEN,
			    capabilities->max_total_match_filter_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_SPECIFIC_INFO_LEN,
			    capabilities->max_service_specific_info_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_VSA_DATA_LEN, capabilities->max_vsa_data_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MESH_DATA_LEN, capabilities->max_mesh_data_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDI_INTERFACES, capabilities->max_ndi_interfaces);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDP_SESSIONS, capabilities->max_ndp_sessions);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_APP_INFO_LEN, capabilities->max_app_info_len);
	} else if (publish_subscribe_id) {
		nla_put_u16(reply, NAN_REPLY_ATTR_PUBLISH_SUBSCRIBE_TYPE, publish_subscribe_id);
	}

	if (cfg80211_vendor_cmd_reply(reply))
		SLSI_ERR_NODEV("FAILED to reply nan coammnd. response_type:%d\n", response_type);
}

static int slsi_nan_enable_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_enable_req *hal_req,
					 const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_MASTER_PREF:
			hal_req->master_pref = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_CLUSTER_LOW:
			hal_req->cluster_low = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_CLUSTER_HIGH:
			hal_req->cluster_high = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUPPORT_5G_VAL:
			hal_req->support_5g_val = nla_get_u8(iter);
			hal_req->config_support_5g = 1;
			break;

		case NAN_REQ_ATTR_SID_BEACON_VAL:
			hal_req->sid_beacon_val = nla_get_u8(iter);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_2G4_VAL:
			hal_req->rssi_close_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_2G4_VAL:
			hal_req->rssi_middle_2dot4g_val =  nla_get_u8(iter);
			hal_req->config_2dot4g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			hal_req->rssi_proximity_2dot4g_val = nla_get_u8(iter);
			hal_req->rssi_proximity_2dot4g_val = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_LIMIT_VAL:
			hal_req->hop_count_limit_val = nla_get_u8(iter);
			hal_req->config_hop_count_limit = 1;
			break;

		case NAN_REQ_ATTR_SUPPORT_2G4_VAL:
			hal_req->support_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_support = 1;
			break;

		case NAN_REQ_ATTR_BEACONS_2G4_VAL:
			hal_req->beacon_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_2G4_VAL:
			hal_req->sdf_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_sdf = 1;
			break;

		case NAN_REQ_ATTR_BEACON_5G_VAL:
			hal_req->beacon_5g_val = nla_get_u8(iter);
			hal_req->config_5g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_5G_VAL:
			hal_req->sdf_5g_val = nla_get_u8(iter);
			hal_req->config_5g_sdf = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_5G_VAL:
			hal_req->rssi_close_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_5G_VAL:
			hal_req->rssi_middle_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			hal_req->rssi_close_proximity_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			hal_req->rssi_window_size_val = nla_get_u8(iter);
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_OUI_VAL:
			hal_req->oui_val = nla_get_u32(iter);
			hal_req->config_oui = 1;
			break;

		case NAN_REQ_ATTR_MAC_ADDR_VAL:
			memcpy(hal_req->intf_addr_val, nla_data(iter), ETH_ALEN);
			hal_req->config_intf_addr = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			hal_req->config_cluster_attribute_val = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			memcpy(hal_req->scan_params_val.dwell_time, nla_data(iter),
			       sizeof(hal_req->scan_params_val.dwell_time));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			memcpy(hal_req->scan_params_val.scan_period, nla_data(iter),
			       sizeof(hal_req->scan_params_val.scan_period));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			hal_req->random_factor_force_val = nla_get_u8(iter);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			hal_req->hop_count_force_val = nla_get_u8(iter);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_2G4_MHZ_VAL:
			hal_req->channel_24g_val = nla_get_u32(iter);
			hal_req->config_24g_channel = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_5G_MHZ_VAL:
			hal_req->channel_5g_val = nla_get_u8(iter);
			hal_req->config_5g_channel = 1;
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL:
			hal_req->subscribe_sid_beacon_val = nla_get_u8(iter);
			hal_req->config_subscribe_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_DW_2G4_INTERVAL:
			hal_req->dw_2dot4g_interval_val = nla_get_u8(iter);
			/* valid range for 2.4G is 1-5 */
			if (hal_req->dw_2dot4g_interval_val > 0  && hal_req->dw_2dot4g_interval_val < 5)
				hal_req->config_2dot4g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DW_5G_INTERVAL:
			hal_req->dw_5g_interval_val = nla_get_u8(iter);
			/* valid range for 5g is 0-5 */
			if (hal_req->dw_5g_interval_val < 5)
				hal_req->config_5g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL:
			hal_req->disc_mac_addr_rand_interval_sec = nla_get_u8(iter);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN enable attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_enable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_enable_req hal_req;
	int ret;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	u8 nan_vif_mac_address[ETH_ALEN];
	u8 broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "No NAN interface\n");
		ret = -ENOTSUPP;
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	if (!slsi_dev_nan_supported(sdev)) {
		SLSI_ERR(sdev, "NAN not allowed(mib:%d)\n", sdev->nan_enabled);
		ret = WIFI_HAL_ERROR_NOT_SUPPORTED;
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	reply_status = slsi_nan_enable_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (ndev_vif->activated) {
		ret = -EINVAL;
		SLSI_DBG1(sdev, SLSI_GSCAN, "Already Enabled. Req Rejected\n");
		goto exit_with_mutex;
	}
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;

	if (hal_req.config_intf_addr)
		ether_addr_copy(nan_vif_mac_address, hal_req.intf_addr_val);
	else
		slsi_nan_get_mac(sdev, nan_vif_mac_address);

	ret = slsi_mlme_add_vif(sdev, dev, nan_vif_mac_address, broadcast_mac);
	if (ret) {
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		SLSI_ERR(sdev, "failed to set unsync vif. Cannot start NAN\n");
	} else {
		ret = slsi_mlme_nan_enable(sdev, dev, &hal_req);
		if (ret) {
			SLSI_ERR(sdev, "failed to enable NAN.\n");
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			slsi_mlme_del_vif(sdev, dev);
			ndev_vif->activated = false;
			ndev_vif->nan.subscribe_id_map = 0;
			ndev_vif->nan.publish_id_map = 0;
		} else {
			slsi_vif_activated(sdev, dev);
		}
	}

exit_with_mutex:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_ENABLED, 0, NULL);
	return ret;
}

int slsi_nan_disable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;

	if (dev) {
		ndev_vif = netdev_priv(dev);
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
		if (ndev_vif->activated) {
			slsi_mlme_del_vif(sdev, dev);
			ndev_vif->activated = false;
			ndev_vif->nan.subscribe_id_map = 0;
			ndev_vif->nan.publish_id_map = 0;
		} else {
			SLSI_WARN(sdev, "NAN FWif not active!!");
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	} else {
		SLSI_WARN(sdev, "No NAN interface!!");
	}

	slsi_vendor_nan_command_reply(wiphy, SLSI_HAL_NAN_STATUS_SUCCESS, 0, NAN_RESPONSE_DISABLED, 0, NULL);

	return 0;
}

static int slsi_nan_publish_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_publish_req *hal_req,
					  const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			hal_req->publish_id = nla_get_u16(iter);
			break;
		case NAN_REQ_ATTR_PUBLISH_TTL:
			hal_req->ttl = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_PERIOD:
			hal_req->period = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TYPE:
			hal_req->publish_type = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_TYPE:
			hal_req->tx_type = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_COUNT:
			hal_req->publish_count = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME_LEN:
			hal_req->service_name_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME:
			memcpy(hal_req->service_name, nla_data(iter), hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_MATCH_ALGO:
			hal_req->publish_match_indicator = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO_LEN:
			hal_req->service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER_LEN:
			hal_req->rx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER:
			memcpy(hal_req->rx_match_filter, nla_data(iter), hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER_LEN:
			hal_req->tx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER:
			memcpy(hal_req->tx_match_filter, nla_data(iter), hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_RSSI_THRESHOLD_FLAG:
			hal_req->rssi_threshold_flag = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_CONN_MAP:
			hal_req->connmap = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			hal_req->sdea_service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			memcpy(hal_req->sdea_service_specific_info, nla_data(iter),
			       hal_req->sdea_service_specific_info_len);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN publish attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_publish(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_publish_req hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int ret;
	u32 reply_status;
	u32 publish_id = 0;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_publish_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req.publish_id) {
		hal_req.publish_id = slsi_nan_get_new_publish_id(ndev_vif);
	} else if (!slsi_nan_is_publish_id_active(ndev_vif, hal_req.publish_id)) {
		SLSI_WARN(sdev, "Publish id %d not found. map:%x\n", hal_req.publish_id,
			  ndev_vif->nan.publish_id_map);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	if (hal_req.publish_id) {
		ret = slsi_mlme_nan_publish(sdev, dev, &hal_req, hal_req.publish_id);
		if (ret)
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		else
			publish_id = hal_req.publish_id;
	} else {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		SLSI_WARN(sdev, "Too Many concurrent PUBLISH REQ(map:%x)\n",
			  ndev_vif->nan.publish_id_map);
		ret = -ENOTSUPP;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH, publish_id, NULL);
	return ret;
}

int slsi_nan_publish_cancel(struct wiphy *wiphy, struct wireless_dev *wdev,
			    const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int type, tmp, ret = 0;
	u16 publish_id = 0;
	const struct nlattr *iter;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			publish_id = nla_get_u16(iter);
			break;
		default:
			SLSI_ERR(sdev, "Unexpected NAN publishcancel attribute TYPE:%d\n", type);
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}
	if (!publish_id || !slsi_nan_is_publish_id_active(ndev_vif, publish_id)) {
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		SLSI_WARN(sdev, "Publish_id(%d) not active. map:%x\n",
			  publish_id, ndev_vif->nan.publish_id_map);
	} else {
		ret = slsi_mlme_nan_publish(sdev, dev, NULL, publish_id);
		if (ret)
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH_CANCEL, publish_id, NULL);
	return ret;
}

static int slsi_nan_subscribe_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_subscribe_req *hal_req,
					    const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			hal_req->subscribe_id = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TTL:
			hal_req->ttl = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_PERIOD:
			hal_req->period = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TYPE:
			hal_req->subscribe_type = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_FILTER_TYPE:
			hal_req->service_response_filter = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_INCLUDE:
			hal_req->service_response_include = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_USE_RESP_FILTER:
			hal_req->use_service_response_filter = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SSI_REQUIRED:
			hal_req->ssi_required_for_match_indication = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_MATCH_INDICATOR:
			hal_req->subscribe_match_indicator = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_COUNT:
			hal_req->subscribe_count = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME_LEN:
			hal_req->service_name_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME:
			memcpy(hal_req->service_name, nla_data(iter), hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO_LEN:
			hal_req->service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER_LEN:
			hal_req->rx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER:
			memcpy(hal_req->rx_match_filter, nla_data(iter), hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER_LEN:
			hal_req->tx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER:
			memcpy(hal_req->tx_match_filter, nla_data(iter), hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RSSI_THRESHOLD_FLAG:
			hal_req->rssi_threshold_flag = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_CONN_MAP:
			hal_req->connmap = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_NUM_INTF_ADDR_PRESENT:
			hal_req->num_intf_addr_present = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_INTF_ADDR:
			memcpy(hal_req->intf_addr, nla_data(iter), hal_req->num_intf_addr_present * ETH_ALEN);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			hal_req->sdea_service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			memcpy(hal_req->sdea_service_specific_info, nla_data(iter),
			       hal_req->sdea_service_specific_info_len);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN subscribe attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_subscribe(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_subscribe_req *hal_req;
	int ret;
	u32 reply_status;
	u32 subscribe_id = 0;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "Failed to alloc hal_req structure!!!\n");
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_subscribe_get_nl_params(sdev, hal_req, data, len);
	if (reply_status != SLSI_HAL_NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->subscribe_id) {
		hal_req->subscribe_id = slsi_nan_get_new_subscribe_id(ndev_vif);
	} else if (!slsi_nan_is_subscribe_id_active(ndev_vif, hal_req->subscribe_id)) {
		SLSI_WARN(sdev, "Subscribe id %d not found. map:%x\n", hal_req->subscribe_id,
			  ndev_vif->nan.subscribe_id_map);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_subscribe(sdev, dev, hal_req, hal_req->subscribe_id);
	if (ret)
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
	else
		subscribe_id = hal_req->subscribe_id;

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE, subscribe_id, NULL);
	return ret;
}

int slsi_nan_subscribe_cancel(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int type, tmp, ret = WIFI_HAL_ERROR_UNKNOWN;
	u16 subscribe_id = 0;
	const struct nlattr *iter;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			subscribe_id = nla_get_u16(iter);
			break;
		default:
			SLSI_ERR(sdev, "Unexpected NAN subscribecancel attribute TYPE:%d\n", type);
			reply_status = SLSI_HAL_NAN_STATUS_INVALID_PARAM;
			goto exit;
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (ndev_vif->activated) {
		if (!subscribe_id || !slsi_nan_is_subscribe_id_active(ndev_vif, subscribe_id)) {
			SLSI_WARN(sdev, "subscribe_id(%d) not active. map:%x\n",
				  subscribe_id, ndev_vif->nan.subscribe_id_map);
			reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		} else {
			ret = slsi_mlme_nan_subscribe(sdev, dev, NULL, subscribe_id);
			if (ret)
				reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		}
	} else {
		SLSI_ERR(sdev, "vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE_CANCEL, subscribe_id, NULL);
	return ret;
}

static int slsi_nan_followup_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_transmit_followup_req *hal_req,
					   const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_FOLLOWUP_ID:
			hal_req->publish_subscribe_id = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_REQUESTOR_ID:
			hal_req->requestor_instance_id = nla_get_u32(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_ADDR:
			memcpy(hal_req->addr, nla_data(iter), ETH_ALEN);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_PRIORITY:
			hal_req->priority = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_TX_WINDOW:
			hal_req->dw_or_faw = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME_LEN:
			hal_req->service_specific_info_len =  nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA_LEN:
			hal_req->sdea_service_specific_info_len =  nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SDEA:
			memcpy(hal_req->sdea_service_specific_info, nla_data(iter),
			       hal_req->sdea_service_specific_info_len);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN followup attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_transmit_followup(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_transmit_followup_req hal_req;
	int ret;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_followup_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req.publish_subscribe_id ||
	    !(slsi_nan_is_subscribe_id_active(ndev_vif, hal_req.publish_subscribe_id) ||
	    slsi_nan_is_publish_id_active(ndev_vif, hal_req.publish_subscribe_id))) {
		SLSI_WARN(sdev, "publish/Subscribe id %d not found. map:%x\n", hal_req.publish_subscribe_id,
			  ndev_vif->nan.subscribe_id_map);
		reply_status = SLSI_HAL_NAN_STATUS_INVALID_PUBLISH_SUBSCRIBE_ID;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_tx_followup(sdev, dev, &hal_req);
	if (ret)
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_TRANSMIT_FOLLOWUP, 0, NULL);
	return ret;
}

static int slsi_nan_config_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_config_req *hal_req,
					 const void *data, int len)
{
	int type, type1, tmp, tmp1, disc_attr_idx = 0, famchan_idx = 0;
	const struct nlattr *iter, *iter1;
	struct slsi_hal_nan_post_discovery_param *disc_attr;
	struct slsi_hal_nan_further_availability_channel *famchan;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SID_BEACON_VAL:
			hal_req->sid_beacon = nla_get_u8(iter);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			hal_req->rssi_proximity = nla_get_u8(iter);
			hal_req->config_rssi_proximity = 1;
			break;

		case NAN_REQ_ATTR_MASTER_PREF:
			hal_req->master_pref = nla_get_u8(iter);
			hal_req->config_master_pref = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			hal_req->rssi_close_proximity_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			hal_req->rssi_window_size_val = nla_get_u8(iter);
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			hal_req->config_cluster_attribute_val = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			memcpy(hal_req->scan_params_val.dwell_time, nla_data(iter),
			       sizeof(hal_req->scan_params_val.dwell_time));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			memcpy(hal_req->scan_params_val.scan_period, nla_data(iter),
			       sizeof(hal_req->scan_params_val.scan_period));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			hal_req->random_factor_force_val = nla_get_u8(iter);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			hal_req->hop_count_force_val = nla_get_u8(iter);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_PAYLOAD_TX:
			hal_req->conn_capability_val.payload_transmit_flag = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFD:
			hal_req->conn_capability_val.is_wfd_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFDS:
			hal_req->conn_capability_val.is_wfds_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_TDLS:
			hal_req->conn_capability_val.is_tdls_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_MESH:
			hal_req->conn_capability_val.is_mesh_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_IBSS:
			hal_req->conn_capability_val.is_ibss_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WLAN_INFRA:
			hal_req->conn_capability_val.wlan_infra_field = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_NUM_ENTRIES:
			hal_req->num_config_discovery_attr = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_VAL:
			if (disc_attr_idx >= hal_req->num_config_discovery_attr) {
				SLSI_ERR(sdev,
					 "disc attr(%d) > num disc attr(%d)\n",
					 disc_attr_idx + 1, hal_req->num_config_discovery_attr);
				return -EINVAL;
			}
			disc_attr = &hal_req->discovery_attr_val[disc_attr_idx];
			disc_attr_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_CONN_TYPE:
					disc_attr->type = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_NAN_ROLE:
					disc_attr->role = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_TRANSMIT_FREQ:
					disc_attr->transmit_freq = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_DURATION:
					disc_attr->duration = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_INTERVAL:
					disc_attr->avail_interval_bitmap = nla_get_u32(iter1);
					break;

				case NAN_REQ_ATTR_MAC_ADDR_VAL:
					memcpy(disc_attr->addr, nla_data(iter1), ETH_ALEN);
					break;

				case NAN_REQ_ATTR_MESH_ID_LEN:
					disc_attr->mesh_id_len = nla_get_u16(iter1);
					break;

				case NAN_REQ_ATTR_MESH_ID:
					memcpy(disc_attr->mesh_id, nla_data(iter1), disc_attr->mesh_id_len);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID_LEN:
					disc_attr->infrastructure_ssid_len = nla_get_u16(iter1);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID:
					memcpy(disc_attr->infrastructure_ssid_val, nla_data(iter1),
					       disc_attr->infrastructure_ssid_len);
					break;
				}
			}
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_NUM_ENTRIES:
			hal_req->fam_val.numchans = nla_get_u8(iter);
			hal_req->config_fam = 1;
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_VAL:
			hal_req->config_fam = 1;
			if (famchan_idx >= hal_req->fam_val.numchans) {
				SLSI_ERR(sdev,
					 "famchan attr(%d) > numchans(%d)\n",
					 famchan_idx + 1, hal_req->fam_val.numchans);
				return -EINVAL;
			}
			famchan = &hal_req->fam_val.famchan[famchan_idx];
			famchan_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_FURTHER_AVAIL_ENTRY_CTRL:
					famchan->entry_control = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_CLASS:
					famchan->class_val = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN:
					famchan->channel = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_MAPID:
					famchan->mapid = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_INTERVAL_BITMAP:
					famchan->avail_interval_bitmap = nla_get_u32(iter1);
					break;
				}
			}
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SID_BEACON_VAL:
			hal_req->subscribe_sid_beacon_val = nla_get_u8(iter);
			hal_req->config_subscribe_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_DW_2G4_INTERVAL:
			hal_req->dw_2dot4g_interval_val = nla_get_u8(iter);
			/* valid range for 2.4G is 1-5 */
			if (hal_req->dw_2dot4g_interval_val > 0  && hal_req->dw_2dot4g_interval_val < 6)
				hal_req->config_2dot4g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DW_5G_INTERVAL:
			hal_req->dw_5g_interval_val = nla_get_u8(iter);
			/* valid range for 5g is 0-5 */
			if (hal_req->dw_5g_interval_val < 6)
				hal_req->config_5g_dw_band = 1;
			break;

		case NAN_REQ_ATTR_DISC_MAC_ADDR_RANDOM_INTERVAL:
			hal_req->disc_mac_addr_rand_interval_sec = nla_get_u8(iter);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN config attribute TYPE:%d\n", type);
			return SLSI_HAL_NAN_STATUS_INVALID_PARAM;
		}
	}
	return SLSI_HAL_NAN_STATUS_SUCCESS;
}

int slsi_nan_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_config_req hal_req;
	int ret;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_config_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	} else {
		ret = slsi_mlme_nan_set_config(sdev, dev, &hal_req);
		if (ret)
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_CONFIG, 0, NULL);
	return ret;
}

int slsi_nan_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	u32 reply_status = SLSI_HAL_NAN_STATUS_SUCCESS;
	struct slsi_hal_nan_capabilities nan_capabilities;
	int ret = 0, i;
	struct slsi_mib_value *values = NULL;
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_CLUSTERS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_PUBLISHES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_CONCURRENT_SUBSCRIBES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_SERVICE_NAME_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_TOTAL_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_SERVICE_SPECIFIC_INFO_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_VSA_DATA_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_MESH_DATA_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_NDI_INTERFACES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_NDP_SESSIONS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NAN_MAX_APP_INFO_LENGTH, { 0, 0 } } };
	u32 *capabilities_mib_val[] = { &nan_capabilities.max_concurrent_nan_clusters,
									&nan_capabilities.max_publishes,
									&nan_capabilities.max_subscribes,
									&nan_capabilities.max_service_name_len,
									&nan_capabilities.max_match_filter_len,
									&nan_capabilities.max_total_match_filter_len,
									&nan_capabilities.max_service_specific_info_len,
									&nan_capabilities.max_vsa_data_len,
									&nan_capabilities.max_mesh_data_len,
									&nan_capabilities.max_ndi_interfaces,
									&nan_capabilities.max_ndp_sessions,
									&nan_capabilities.max_app_info_len };

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = SLSI_HAL_NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	/* Expect each mib length in response is 11 */
	mibrsp.dataLength = 11 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		reply_status = SLSI_HAL_NAN_STATUS_NO_RESOURCE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (!values) {
		ret = 0xFFFFFFFF;
		reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
		goto exit_with_mibrsp;
	}

	for (i = 0; i < ARRAY_SIZE(get_values); i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			*capabilities_mib_val[i] = values[i].u.uintValue;
			SLSI_DBG2(sdev, SLSI_GSCAN, "MIB value = %ud\n", *capabilities_mib_val[i]);
		} else {
			SLSI_ERR(sdev, "invalid type(%d). iter:%d\n", values[i].type, i);
			ret = 0xFFFFFFFF;
			reply_status = SLSI_HAL_NAN_STATUS_INTERNAL_FAILURE;
			*capabilities_mib_val[i] = 0;
		}
	}

	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_GET_CAPABILITIES, 0, &nan_capabilities);
	return ret;
}

void slsi_nan_event(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct sk_buff *nl_skb = NULL;
	int res = 0;
	u16 event, identifier, evt_reason;
	u8 *mac_addr;
	u16 hal_event;
	struct netdev_vif *ndev_vif;
	enum slsi_nan_disc_event_type disc_event_type = 0;

	ndev_vif = netdev_priv(dev);
	event = fapi_get_u16(skb, u.mlme_nan_event_ind.event);
	identifier = fapi_get_u16(skb, u.mlme_nan_event_ind.identifier);
	mac_addr = fapi_get_buff(skb, u.mlme_nan_event_ind.address_or_identifier);
	evt_reason = fapi_get_u16(skb, u.mlme_nan_event_ind.reason_code);

	switch (event) {
	case FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED:
		hal_event = SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_MATCH_EXPIRED:
		hal_event = SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_SUBSCRIBE_TERMINATED:
		hal_event = SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_ADDRESS_CHANGED:
		disc_event_type = NAN_EVENT_ID_DISC_MAC_ADDR;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_STARTED:
		disc_event_type = NAN_EVENT_ID_STARTED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_JOINED:
		disc_event_type = NAN_EVENT_ID_JOINED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	default:
		return;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(hal_event), hal_event);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		return;
	}

	switch (hal_event) {
	case SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PUBLISH_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PUBLISH_ID, evt_reason);
		ndev_vif->nan.publish_id_map &= ~BIT(identifier);
		break;
	case SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, evt_reason);
		break;
	case SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_SUBSCRIBE_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_SUBSCRIBE_REASON, evt_reason);
		ndev_vif->nan.subscribe_id_map &= ~BIT(identifier);
		break;
	case SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_EVT_TYPE, disc_event_type);
		res |= nla_put(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_MAC_ADDR, ETH_ALEN, mac_addr);
		break;
	}

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
}

void slsi_nan_followup_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *stitched_ie_p, *ptr;
	int stitched_ie_len;
	struct slsi_hal_nan_followup_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	stitched_ie_len = fapi_get_datalen(skb); /* max length of stitched_ie */
	if (!stitched_ie_len) {
		SLSI_ERR(sdev, "mlme_nan_followup_ind no mbulk data\n");
		return;
	}
	stitched_ie_p = kmalloc(stitched_ie_len, GFP_KERNEL);
	if (!stitched_ie_p) {
		SLSI_ERR(sdev, "No memory for followup_ind fapi_data\n");
		return;
	}

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for followup_ind\n");
		kfree(stitched_ie_p);
		return;
	}
	memset(hal_evt, 0, sizeof(*hal_evt));
	stitched_ie_len = slsi_nan_stitch_ie(fapi_get_data(skb), stitched_ie_len, 0x050b, stitched_ie_p);
	if (!stitched_ie_len) {
		SLSI_ERR(sdev, "No followup ind IE\n");
		kfree(hal_evt);
		kfree(stitched_ie_p);
		return;
	}
	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.publish_subscribe_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.peer_id);
	ptr = stitched_ie_p + 7; /* 7 = ie_id(1), ie_len(1), oui(3) type/subtype(2)*/

	ether_addr_copy(hal_evt->addr, ptr);
	ptr += ETH_ALEN;
	ptr += 1; /* skip priority */
	hal_evt->dw_or_faw = *ptr;
	ptr += 1;
	while (stitched_ie_len > (ptr - stitched_ie_p) + 4) {
		tag_id = *(u16 *)ptr;
		ptr += 2;
		tag_len = *(u16 *)ptr;
		ptr += 2;
		if (stitched_ie_p[1] + 2 < (ptr - stitched_ie_p) + tag_len) {
			SLSI_ERR(sdev, "TLV error\n");
			kfree(hal_evt);
			return;
		}
		if (tag_id == SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO) {
			hal_evt->service_specific_info_len = tag_len;
			memcpy(hal_evt->service_specific_info, ptr, tag_len);
		} else if (tag_id == SLSI_FAPI_NAN_SDEA) {
			hal_evt->sdea_service_specific_info_len = tag_len;
			memcpy(hal_evt->sdea_service_specific_info, ptr, tag_len);
		}
		ptr += tag_len;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_NAN_FOLLOWUP_EVENT), SLSI_NL80211_NAN_FOLLOWUP_EVENT);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		kfree(stitched_ie_p);
		return;
	}

	res = nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_PUBLISH_SUBSCRIBE_ID,
			   cpu_to_le16(hal_evt->publish_subscribe_id));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_REQUESTOR_INSTANCE_ID,
			    cpu_to_le16(hal_evt->requestor_instance_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_FOLLOWUP_DW_OR_FAW, hal_evt->dw_or_faw);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			       hal_evt->service_specific_info);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_SDEA_LEN, hal_evt->sdea_service_specific_info_len);
	if (hal_evt->sdea_service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_SDEA, hal_evt->sdea_service_specific_info_len,
			       hal_evt->sdea_service_specific_info);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		kfree(stitched_ie_p);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
	kfree(stitched_ie_p);
}

void slsi_nan_service_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *stitched_ie_p, *ptr;
	int stitched_ie_len;
	struct slsi_hal_nan_match_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	stitched_ie_len = fapi_get_datalen(skb); /* max length of stitched_ie */
	if (!stitched_ie_len) {
		SLSI_ERR(sdev, "mlme_nan_service_ind no mbulk data\n");
		return;
	}

	stitched_ie_p = kmalloc(stitched_ie_len, GFP_KERNEL);
	if (!stitched_ie_p) {
		SLSI_ERR(sdev, "No memory for service_ind fapi_data\n");
		return;
	}

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for service_ind\n");
		kfree(stitched_ie_p);
		return;
	}

	memset(hal_evt, 0, sizeof(*hal_evt));
	stitched_ie_len = slsi_nan_stitch_ie(fapi_get_data(skb), stitched_ie_len, 0x040b, stitched_ie_p);
	if (!stitched_ie_len) {
		SLSI_ERR(sdev, "No match ind IE\n");
		kfree(hal_evt);
		kfree(stitched_ie_p);
		return;
	}
	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_service_ind.publish_subscribe_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_service_ind.peer_id);

	/* 7 = ie_id(1), ie_len(1), oui(3) type/subtype(2)*/
	ptr = stitched_ie_p + 7;
	ether_addr_copy(hal_evt->addr, ptr);
	ptr += ETH_ALEN;
	hal_evt->match_occurred_flag = *ptr;
	ptr += 1;
	hal_evt->out_of_resource_flag = *ptr;
	ptr += 1;
	hal_evt->rssi_value = *ptr;
	ptr += 1 + 8; /* skip 8 bytes related to datapath and ranging*/
	while (stitched_ie_len > (ptr - stitched_ie_p) + 4) {
		tag_id = *(u16 *)ptr;
		ptr += 2;
		tag_len = *(u16 *)ptr;
		ptr += 2;
		if (stitched_ie_p[1] + 2 < (ptr - stitched_ie_p) + tag_len) {
			SLSI_ERR(sdev, "TLV error\n");
			kfree(hal_evt);
			kfree(stitched_ie_p);
			return;
		}
		switch (tag_id) {
		case SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO:
			hal_evt->service_specific_info_len = tag_len;
			memcpy(hal_evt->service_specific_info, ptr, tag_len);
			break;

		case SLSI_FAPI_NAN_SDEA:
			hal_evt->sdea_service_specific_info_len = tag_len;
			memcpy(hal_evt->sdea_service_specific_info, ptr, tag_len);
			break;

		case SLSI_FAPI_NAN_SDF_MATCH_FILTER:
			hal_evt->sdf_match_filter_len = tag_len;
			memcpy(hal_evt->sdf_match_filter, ptr, tag_len);
			break;
		}
		ptr += tag_len;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_NAN_MATCH_EVENT), SLSI_NL80211_NAN_MATCH_EVENT);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		kfree(stitched_ie_p);
		return;
	}
	res = nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, hal_evt->publish_subscribe_id);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, hal_evt->requestor_instance_id);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			hal_evt->service_specific_info);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER_LEN, hal_evt->sdf_match_filter_len);
	if (hal_evt->sdf_match_filter_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER, hal_evt->sdf_match_filter_len,
			hal_evt->sdf_match_filter);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_SDEA_LEN, hal_evt->sdea_service_specific_info_len);
	if (hal_evt->sdea_service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_SDEA, hal_evt->sdea_service_specific_info_len,
			hal_evt->sdea_service_specific_info);

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_MATCH_OCCURRED_FLAG, hal_evt->match_occurred_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_OUT_OF_RESOURCE_FLAG, hal_evt->out_of_resource_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_RSSI_VALUE, hal_evt->rssi_value);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		kfree(hal_evt);
		kfree(stitched_ie_p);
		return;
	}

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
	kfree(stitched_ie_p);
}
