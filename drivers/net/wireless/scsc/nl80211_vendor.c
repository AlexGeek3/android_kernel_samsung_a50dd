/*****************************************************************************
 *
 * Copyright (c) 2014 - 2019 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#include <linux/version.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include "dev.h"
#include "cfg80211_ops.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"
#include "netif.h"
#include "unifiio.h"
#include "mib.h"
#include "nl80211_vendor.h"

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
#include "scsc_wifilogger.h"
#include "scsc_wifilogger_rings.h"
#include "scsc_wifilogger_types.h"
#endif
#define SLSI_WIFI_TAG_RSSI               21
#define SLSI_WIFI_TAG_REASON_CODE        14
#define SLSI_WIFI_TAG_VENDOR_SPECIFIC    0
#define SLSI_WIFI_TAG_EAPOL_MESSAGE_TYPE 29
#define SLSI_WIFI_TAG_STATUS             4

#define SLSI_GSCAN_INVALID_RSSI        0x7FFF
#define SLSI_EPNO_AUTH_FIELD_WEP_OPEN  1
#define SLSI_EPNO_AUTH_FIELD_WPA_PSK   2
#define SLSI_EPNO_AUTH_FIELD_WPA_EAP   4
#define WIFI_EVENT_FW_BTM_FRAME_REQUEST   56  // Request for a BTM frame is received
#define WIFI_EVENT_FW_BTM_FRAME_RESPONSE  57     // A BTM frame is transmitted.
#define WIFI_EVENT_FW_NR_FRAME_REQUEST      58
#define WIFI_EVENT_FW_RM_FRAME_RESPONSE    59

#define SLSI_WIFI_TAG_VD_CHANNEL_UTILISATION   0xf01a
#define SLSI_WIFI_TAG_VD_ROAMING_REASON           0xf019
#define SLSI_WIFI_TAG_VD_BTM_REQUEST_MODE      0xf01b
#define SLSI_WIFI_TAG_VD_BTM_RESPONSE_STATUS  0xf01c
#define SLSI_WIFI_TAG_VD_RETRY_COUNT                   0xf00f
#define SLSI_WIFI_TAG_VD_EAPOL_KEY_TYPE             0xF008
#define SLSI_WIFI_EAPOL_KEY_TYPE_GTK                      0x0000
#define SLSI_WIFI_EAPOL_KEY_TYPE_PTK                      0x0001
#define SLSI_WIFI_ROAMING_SEARCH_REASON_RESERVED   0
#define SLSI_WIFI_ROAMING_SEARCH_REASON_LOW_RSSI             1
#define SLSI_WIFI_ROAMING_SEARCH_REASON_LINK_LOSS             2
#define SLSI_WIFI_ROAMING_SEARCH_REASON_BTM_REQ                3
#define SLSI_WIFI_ROAMING_SEARCH_REASON_CU_TRIGGER           4

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
static int mem_dump_buffer_size;
static char *mem_dump_buffer;
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
char *slsi_print_event_name(int event_id)
{
	switch (event_id) {
	case SLSI_NL80211_SCAN_RESULTS_AVAILABLE_EVENT:
		return "SCAN_RESULTS_AVAILABLE_EVENT";
	case SLSI_NL80211_FULL_SCAN_RESULT_EVENT:
		return "FULL_SCAN_RESULT_EVENT";
	case SLSI_NL80211_SCAN_EVENT:
		return "BUCKET_SCAN_DONE_EVENT";
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	case SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH:
		return "KEY_MGMT_ROAM_AUTH";
#endif
	case SLSI_NL80211_VENDOR_HANGED_EVENT:
		return "SLSI_NL80211_VENDOR_HANGED_EVENT";
	case SLSI_NL80211_EPNO_EVENT:
		return "SLSI_NL80211_EPNO_EVENT";
	case SLSI_NL80211_HOTSPOT_MATCH:
		return "SLSI_NL80211_HOTSPOT_MATCH";
	case SLSI_NL80211_RSSI_REPORT_EVENT:
		return "SLSI_NL80211_RSSI_REPORT_EVENT";
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	case SLSI_NL80211_LOGGER_RING_EVENT:
		return "SLSI_NL80211_LOGGER_RING_EVENT";
	case SLSI_NL80211_LOGGER_FW_DUMP_EVENT:
		return "SLSI_NL80211_LOGGER_FW_DUMP_EVENT";
#endif
	case SLSI_NL80211_NAN_RESPONSE_EVENT:
		return "SLSI_NL80211_NAN_RESPONSE_EVENT";
	case SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT:
		return "SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT";
	case SLSI_NL80211_NAN_MATCH_EVENT:
		return "SLSI_NL80211_NAN_MATCH_EVENT";
	case SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT:
		return "SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT";
	case SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT:
		return "SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT";
	case SLSI_NL80211_NAN_FOLLOWUP_EVENT:
		return "SLSI_NL80211_NAN_FOLLOWUP_EVENT";
	case SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT:
		return "SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT";
	case SLSI_NL80211_RTT_RESULT_EVENT:
		return "SLSI_NL80211_RTT_RESULT_EVENT";
	case SLSI_NL80211_RTT_COMPLETE_EVENT:
		return "SLSI_NL80211_RTT_COMPLETE_EVENT";
	case SLSI_NL80211_VENDOR_ACS_EVENT:
		return "SLSI_NL80211_VENDOR_ACS_EVENT";
	default:
		return "UNKNOWN_EVENT";
	}
}
#endif

int slsi_vendor_event(struct slsi_dev *sdev, int event_id, const void *data, int len)
{
	struct sk_buff *skb;

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_MLME, "Event: %s(%d), data = %p, len = %d\n",
			slsi_print_event_name(event_id), event_id, data, len);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, len, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, len, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		return -ENOMEM;
	}

	nla_put_nohdr(skb, len, data);

	cfg80211_vendor_event(skb, GFP_KERNEL);

	return 0;
}

static int slsi_vendor_cmd_reply(struct wiphy *wiphy, const void *data, int len)
{
	struct sk_buff *skb;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, len);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb\n");
		return -ENOMEM;
	}

	nla_put_nohdr(skb, len, data);

	return cfg80211_vendor_cmd_reply(skb);
}

static struct net_device *slsi_gscan_get_netdev(struct slsi_dev *sdev)
{
	return slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
}

static struct netdev_vif *slsi_gscan_get_vif(struct slsi_dev *sdev)
{
	struct net_device *dev;

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("Dev is NULL\n");
		return NULL;
	}

	return netdev_priv(dev);
}

#ifdef CONFIG_SCSC_WLAN_DEBUG
static void slsi_gscan_add_dump_params(struct slsi_nl_gscan_param *nl_gscan_param)
{
	int i;
	int j;

	SLSI_DBG2_NODEV(SLSI_GSCAN, "Parameters for SLSI_NL80211_VENDOR_SUBCMD_ADD_GSCAN sub-command:\n");
	SLSI_DBG2_NODEV(SLSI_GSCAN, "base_period: %d max_ap_per_scan: %d report_threshold_percent: %d report_threshold_num_scans = %d num_buckets: %d\n",
			nl_gscan_param->base_period, nl_gscan_param->max_ap_per_scan,
			nl_gscan_param->report_threshold_percent, nl_gscan_param->report_threshold_num_scans,
			nl_gscan_param->num_buckets);

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		SLSI_DBG2_NODEV(SLSI_GSCAN, "Bucket: %d\n", i);
		SLSI_DBG2_NODEV(SLSI_GSCAN, "\tbucket_index: %d band: %d period: %d report_events: %d num_channels: %d\n",
				nl_gscan_param->nl_bucket[i].bucket_index, nl_gscan_param->nl_bucket[i].band,
				nl_gscan_param->nl_bucket[i].period, nl_gscan_param->nl_bucket[i].report_events,
				nl_gscan_param->nl_bucket[i].num_channels);

		for (j = 0; j < nl_gscan_param->nl_bucket[i].num_channels; j++)
			SLSI_DBG2_NODEV(SLSI_GSCAN, "\tchannel_list[%d]: %d\n",
					j, nl_gscan_param->nl_bucket[i].channels[j].channel);
	}
}

void slsi_gscan_scan_res_dump(struct slsi_gscan_result *scan_data)
{
	struct slsi_nl_scan_result_param *nl_scan_res = &scan_data->nl_scan_res;

	SLSI_DBG3_NODEV(SLSI_GSCAN, "TS:%llu SSID:%s BSSID:%pM Chan:%d RSSI:%d Bcn_Int:%d Capab:%#x IE_Len:%d\n",
			nl_scan_res->ts, nl_scan_res->ssid, nl_scan_res->bssid, nl_scan_res->channel,
			nl_scan_res->rssi, nl_scan_res->beacon_period, nl_scan_res->capability, nl_scan_res->ie_length);

	SLSI_DBG_HEX_NODEV(SLSI_GSCAN, &nl_scan_res->ie_data[0], nl_scan_res->ie_length, "IE_Data:\n");
	if (scan_data->anqp_length) {
		SLSI_DBG3_NODEV(SLSI_GSCAN, "ANQP_LENGTH:%d\n", scan_data->anqp_length);
		SLSI_DBG_HEX_NODEV(SLSI_GSCAN, nl_scan_res->ie_data + nl_scan_res->ie_length, scan_data->anqp_length, "ANQP_info:\n");
	}
}
#endif

static int slsi_gscan_get_capabilities(struct wiphy *wiphy,
				       struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_nl_gscan_capabilities nl_cap;
	int                               ret = 0;
	struct slsi_dev                   *sdev = SDEV_FROM_WIPHY(wiphy);

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_GSCAN_CAPABILITIES\n");

	memset(&nl_cap, 0, sizeof(struct slsi_nl_gscan_capabilities));

	ret = slsi_mib_get_gscan_cap(sdev, &nl_cap);
	if (ret != 0) {
		SLSI_ERR(sdev, "Failed to read mib\n");
		return ret;
	}

	nl_cap.max_scan_cache_size = SLSI_GSCAN_MAX_SCAN_CACHE_SIZE;
	nl_cap.max_ap_cache_per_scan = SLSI_GSCAN_MAX_AP_CACHE_PER_SCAN;
	nl_cap.max_scan_reporting_threshold = SLSI_GSCAN_MAX_SCAN_REPORTING_THRESHOLD;

	ret = slsi_vendor_cmd_reply(wiphy, &nl_cap, sizeof(struct slsi_nl_gscan_capabilities));
	if (ret)
		SLSI_ERR_NODEV("gscan_get_capabilities vendor cmd reply failed (err = %d)\n", ret);

	return ret;
}

static u32 slsi_gscan_put_channels(struct ieee80211_supported_band *chan_data, bool no_dfs, bool only_dfs, u32 *buf)
{
	u32 chan_count = 0;
	u32 chan_flags;
	int i;

	if (chan_data == NULL) {
		SLSI_DBG3_NODEV(SLSI_GSCAN, "Band not supported\n");
		return 0;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
	chan_flags = (IEEE80211_CHAN_PASSIVE_SCAN | IEEE80211_CHAN_NO_OFDM | IEEE80211_CHAN_RADAR);
#else
	chan_flags = (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_OFDM | IEEE80211_CHAN_RADAR);
#endif

	for (i = 0; i < chan_data->n_channels; i++) {
		if (chan_data->channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;
		if (only_dfs) {
			if (chan_data->channels[i].flags & chan_flags)
				buf[chan_count++] = chan_data->channels[i].center_freq;
			continue;
		}
		if (no_dfs && (chan_data->channels[i].flags & chan_flags))
			continue;
		buf[chan_count++] = chan_data->channels[i].center_freq;
	}
	return chan_count;
}

static int slsi_gscan_get_valid_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev, const void *data, int len)
{
	int             ret = 0, type, band;
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u32             *chan_list;
	u32             chan_count = 0, mem_len = 0;
	struct sk_buff  *reply;

	if (len < SLSI_NL_VENDOR_DATA_OVERHEAD || !data)
		return -EINVAL;

	type = nla_type(data);

	if (type == GSCAN_ATTRIBUTE_BAND) {
		if (nla_len((struct nlattr *)data) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
			return -EINVAL;
		band = nla_get_u32(data);
	}
	else
		return -EINVAL;

	if (band == 0) {
		SLSI_WARN(sdev, "NO Bands. return 0 channel\n");
		return ret;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	SLSI_DBG3(sdev, SLSI_GSCAN, "band %d\n", band);
	if (wiphy->bands[NL80211_BAND_2GHZ])
		mem_len += wiphy->bands[NL80211_BAND_2GHZ]->n_channels * sizeof(u32);

	if (wiphy->bands[NL80211_BAND_5GHZ])
		mem_len += wiphy->bands[NL80211_BAND_5GHZ]->n_channels * sizeof(u32);

	if (mem_len == 0) {
		ret = -ENOTSUPP;
		goto exit;
	}

	chan_list = kmalloc(mem_len, GFP_KERNEL);
	if (chan_list == NULL) {
		ret = -ENOMEM;
		goto exit;
	}
	mem_len += SLSI_NL_VENDOR_REPLY_OVERHEAD + (SLSI_NL_ATTRIBUTE_U32_LEN * 2);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, mem_len);
	if (reply == NULL) {
		ret = -ENOMEM;
		goto exit_with_chan_list;
	}
	switch (band) {
	case WIFI_BAND_BG:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_2GHZ], false, false, chan_list);
		break;
	case WIFI_BAND_A:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_5GHZ], true, false, chan_list);
		break;
	case WIFI_BAND_A_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_5GHZ], false, true, chan_list);
		break;
	case WIFI_BAND_A_WITH_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_5GHZ], false, false, chan_list);
		break;
	case WIFI_BAND_ABG:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_2GHZ], true, false, chan_list);
		chan_count += slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_5GHZ], true, false, chan_list + chan_count);
		break;
	case WIFI_BAND_ABG_WITH_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_2GHZ], false, false, chan_list);
		chan_count += slsi_gscan_put_channels(wiphy->bands[NL80211_BAND_5GHZ], false, false, chan_list + chan_count);
		break;
	default:
		chan_count = 0;
		SLSI_WARN(sdev, "Invalid Band %d\n", band);
	}
	nla_put_u32(reply, GSCAN_ATTRIBUTE_NUM_CHANNELS, chan_count);
	nla_put(reply, GSCAN_ATTRIBUTE_CHANNEL_LIST, chan_count * sizeof(u32), chan_list);

	ret =  cfg80211_vendor_cmd_reply(reply);

	if (ret)
		SLSI_ERR(sdev, "FAILED to reply GET_VALID_CHANNELS\n");

exit_with_chan_list:
	kfree(chan_list);
exit:
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	return ret;
}

struct slsi_gscan_result *slsi_prepare_scan_result(struct sk_buff *skb, u16 anqp_length, int hs2_id)
{
	struct ieee80211_mgmt    *mgmt = fapi_get_mgmt(skb);
	struct slsi_gscan_result *scan_res;
	struct timespec          ts;
	const u8                 *ssid_ie;
	int                      mem_reqd;
	int                      ie_len;
	u8                       *ie;

	ie = &mgmt->u.beacon.variable[0];
	ie_len = fapi_get_datalen(skb) - (ie - (u8 *)mgmt) - anqp_length;

	/* Exclude 1 byte for ie_data[1]. sizeof(u16) to include anqp_length, sizeof(int) for hs_id */
	mem_reqd = (sizeof(struct slsi_gscan_result) - 1) + ie_len + anqp_length + sizeof(int) + sizeof(u16);

	/* Allocate memory for scan result */
	scan_res = kmalloc(mem_reqd, GFP_KERNEL);
	if (scan_res == NULL) {
		SLSI_ERR_NODEV("Failed to allocate memory for scan result\n");
		return NULL;
	}

	/* Exclude 1 byte for ie_data[1] */
	scan_res->scan_res_len = (sizeof(struct slsi_nl_scan_result_param) - 1) + ie_len;
	scan_res->anqp_length = 0;

	get_monotonic_boottime(&ts);
	scan_res->nl_scan_res.ts = (u64)TIMESPEC_TO_US(ts);

	ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, &mgmt->u.beacon.variable[0], ie_len);
	if ((ssid_ie != NULL) && (ssid_ie[1] > 0) && (ssid_ie[1] < IEEE80211_MAX_SSID_LEN)) {
		memcpy(scan_res->nl_scan_res.ssid, &ssid_ie[2], ssid_ie[1]);
		scan_res->nl_scan_res.ssid[ssid_ie[1]] = '\0';
	} else {
		scan_res->nl_scan_res.ssid[0] = '\0';
	}

	SLSI_ETHER_COPY(scan_res->nl_scan_res.bssid, mgmt->bssid);
	scan_res->nl_scan_res.channel = fapi_get_u16(skb, u.mlme_scan_ind.channel_frequency) / 2;
	scan_res->nl_scan_res.rssi = fapi_get_s16(skb, u.mlme_scan_ind.rssi);
	scan_res->nl_scan_res.rtt = SLSI_GSCAN_RTT_UNSPECIFIED;
	scan_res->nl_scan_res.rtt_sd = SLSI_GSCAN_RTT_UNSPECIFIED;
	scan_res->nl_scan_res.beacon_period = mgmt->u.beacon.beacon_int;
	scan_res->nl_scan_res.capability = mgmt->u.beacon.capab_info;
	scan_res->nl_scan_res.ie_length = ie_len;
	memcpy(scan_res->nl_scan_res.ie_data, ie, ie_len);
	memcpy(scan_res->nl_scan_res.ie_data + ie_len, &hs2_id, sizeof(int));
	memcpy(scan_res->nl_scan_res.ie_data + ie_len + sizeof(int), &anqp_length, sizeof(u16));
	if (anqp_length) {
		memcpy(scan_res->nl_scan_res.ie_data + ie_len + sizeof(u16) + sizeof(int), ie + ie_len, anqp_length);
		scan_res->anqp_length = anqp_length + sizeof(u16) + sizeof(int);
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	slsi_gscan_scan_res_dump(scan_res);
#endif

	return scan_res;
}

static void slsi_gscan_hash_add(struct slsi_dev *sdev, struct slsi_gscan_result *scan_res)
{
	u8                key = SLSI_GSCAN_GET_HASH_KEY(scan_res->nl_scan_res.bssid[5]);
	struct netdev_vif *ndev_vif;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	scan_res->hnext = sdev->gscan_hash_table[key];
	sdev->gscan_hash_table[key] = scan_res;

	/* Update the total buffer consumed and number of scan results */
	sdev->buffer_consumed += scan_res->scan_res_len;
	sdev->num_gscan_results++;
}

static struct slsi_gscan_result *slsi_gscan_hash_get(struct slsi_dev *sdev, u8 *mac)
{
	struct slsi_gscan_result *temp;
	struct netdev_vif        *ndev_vif;
	u8                       key = SLSI_GSCAN_GET_HASH_KEY(mac[5]);

	ndev_vif = slsi_gscan_get_vif(sdev);

	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	temp = sdev->gscan_hash_table[key];
	while (temp != NULL) {
		if (memcmp(temp->nl_scan_res.bssid, mac, ETH_ALEN) == 0)
			return temp;
		temp = temp->hnext;
	}

	return NULL;
}

void slsi_gscan_hash_remove(struct slsi_dev *sdev, u8 *mac)
{
	u8                       key = SLSI_GSCAN_GET_HASH_KEY(mac[5]);
	struct slsi_gscan_result *curr;
	struct slsi_gscan_result *prev;
	struct netdev_vif        *ndev_vif;
	struct slsi_gscan_result *scan_res = NULL;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	if (sdev->gscan_hash_table[key] == NULL)
		return;

	if (memcmp(sdev->gscan_hash_table[key]->nl_scan_res.bssid, mac, ETH_ALEN) == 0) {
		scan_res = sdev->gscan_hash_table[key];
		sdev->gscan_hash_table[key] = sdev->gscan_hash_table[key]->hnext;
	} else {
		prev = sdev->gscan_hash_table[key];
		curr = prev->hnext;

		while (curr != NULL) {
			if (memcmp(curr->nl_scan_res.bssid, mac, ETH_ALEN) == 0) {
				scan_res = curr;
				prev->hnext = curr->hnext;
				break;
			}
			prev = curr;
			curr = curr->hnext;
		}
	}

	if (scan_res) {
		/* Update the total buffer consumed and number of scan results */
		sdev->buffer_consumed -= scan_res->scan_res_len;
		sdev->num_gscan_results--;
		kfree(scan_res);
	}

	if (sdev->num_gscan_results < 0)
		SLSI_ERR(sdev, "Wrong num_gscan_results: %d\n", sdev->num_gscan_results);
}

int slsi_check_scan_result(struct slsi_dev *sdev, struct slsi_bucket *bucket, struct slsi_gscan_result *new_scan_res)
{
	struct slsi_gscan_result *scan_res;

	/* Check if the scan result for the same BSS already exists in driver buffer */
	scan_res = slsi_gscan_hash_get(sdev, new_scan_res->nl_scan_res.bssid);
	if (scan_res == NULL) { /* New scan result */
		if ((sdev->buffer_consumed + new_scan_res->scan_res_len) >= SLSI_GSCAN_MAX_SCAN_CACHE_SIZE) {
			SLSI_DBG2(sdev, SLSI_GSCAN,
				  "Scan buffer full, discarding scan result, buffer_consumed = %d, buffer_threshold = %d\n",
				  sdev->buffer_consumed, sdev->buffer_threshold);

			/* Scan buffer is full can't store anymore new results */
			return SLSI_DISCARD_SCAN_RESULT;
		}

		return SLSI_KEEP_SCAN_RESULT;
	}

	/* Even if scan buffer is full existing results can be replaced with the latest one */
	if (scan_res->scan_cycle == bucket->scan_cycle)
		/* For the same scan cycle the result will be replaced only if the RSSI is better */
		if (new_scan_res->nl_scan_res.rssi < scan_res->nl_scan_res.rssi)
			return SLSI_DISCARD_SCAN_RESULT;

	/* Remove the existing scan result */
	slsi_gscan_hash_remove(sdev, scan_res->nl_scan_res.bssid);

	return SLSI_KEEP_SCAN_RESULT;
}

void slsi_gscan_handle_scan_result(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 scan_id, bool scan_done)
{
	struct slsi_gscan_result *scan_res = NULL;
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_bucket       *bucket;
	u16                      bucket_index;
	int                      event_type = WIFI_SCAN_FAILED;
	u16                      anqp_length;
	int                      hs2_network_id;

	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	SLSI_NET_DBG_HEX(dev, SLSI_GSCAN, skb->data, skb->len, "mlme_scan_ind skb->len: %d\n", skb->len);

	bucket_index = scan_id - SLSI_GSCAN_SCAN_ID_START;
	if (bucket_index >= SLSI_GSCAN_MAX_BUCKETS) {
		SLSI_NET_ERR(dev, "Invalid bucket index: %d (scan_id = %#x)\n", bucket_index, scan_id);
		goto out;
	}

	bucket = &sdev->bucket[bucket_index];
	if (!bucket->used) {
		SLSI_NET_DBG1(dev, SLSI_GSCAN, "Bucket is not active, index: %d (scan_id = %#x)\n", bucket_index, scan_id);
		goto out;
	}

	/* For scan_done indication - no need to store the results */
	if (scan_done) {
		bucket->scan_cycle++;
		bucket->gscan->num_scans++;

		SLSI_NET_DBG3(dev, SLSI_GSCAN, "scan done, scan_cycle = %d, num_scans = %d\n",
			      bucket->scan_cycle, bucket->gscan->num_scans);

		if (bucket->report_events & SLSI_REPORT_EVENTS_EACH_SCAN)
			event_type = WIFI_SCAN_RESULTS_AVAILABLE;
		if (bucket->gscan->num_scans % bucket->gscan->report_threshold_num_scans == 0)
			event_type = WIFI_SCAN_THRESHOLD_NUM_SCANS;
		if (sdev->buffer_consumed >= sdev->buffer_threshold)
			event_type = WIFI_SCAN_THRESHOLD_PERCENT;

		if (event_type != WIFI_SCAN_FAILED)
			slsi_vendor_event(sdev, SLSI_NL80211_SCAN_EVENT, &event_type, sizeof(event_type));

		goto out;
	}

	anqp_length = fapi_get_u16(skb, u.mlme_scan_ind.anqp_elements_length);
	/* TODO new FAPI 3.c has mlme_scan_ind.network_block_id, use that when fapi is updated. */
	hs2_network_id = 1;

	scan_res = slsi_prepare_scan_result(skb, anqp_length, hs2_network_id);
	if (scan_res == NULL) {
		SLSI_NET_ERR(dev, "Failed to prepare scan result\n");
		goto out;
	}

	/* Check for ePNO networks */
	if (fapi_get_u16(skb, u.mlme_scan_ind.preferrednetwork_ap)) {
		if (anqp_length == 0)
			slsi_vendor_event(sdev, SLSI_NL80211_EPNO_EVENT,
					  &scan_res->nl_scan_res, scan_res->scan_res_len);
		else
			slsi_vendor_event(sdev, SLSI_NL80211_HOTSPOT_MATCH,
					  &scan_res->nl_scan_res, scan_res->scan_res_len + scan_res->anqp_length);
	}

	if (bucket->report_events & SLSI_REPORT_EVENTS_FULL_RESULTS) {
		struct sk_buff *nlevent;

		SLSI_NET_DBG3(dev, SLSI_GSCAN, "report_events: SLSI_REPORT_EVENTS_FULL_RESULTS\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nlevent = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, scan_res->scan_res_len + 4, SLSI_NL80211_FULL_SCAN_RESULT_EVENT, GFP_KERNEL);
#else
	nlevent = cfg80211_vendor_event_alloc(sdev->wiphy, scan_res->scan_res_len + 4, SLSI_NL80211_FULL_SCAN_RESULT_EVENT, GFP_KERNEL);
#endif
		if (!nlevent) {
			SLSI_ERR(sdev, "failed to allocate sbk of size:%d\n", scan_res->scan_res_len + 4);
			kfree(scan_res);
			goto out;
		}
		if (nla_put_u32(nlevent, GSCAN_ATTRIBUTE_SCAN_BUCKET_BIT, (1 << bucket_index)) ||
		    nla_put(nlevent, GSCAN_ATTRIBUTE_SCAN_RESULTS, scan_res->scan_res_len, &scan_res->nl_scan_res)) {
			SLSI_ERR(sdev, "failed to put data\n");
			kfree_skb(nlevent);
			kfree(scan_res);
			goto out;
		}
		cfg80211_vendor_event(nlevent, GFP_KERNEL);
	}

	if (slsi_check_scan_result(sdev, bucket, scan_res) == SLSI_DISCARD_SCAN_RESULT) {
		kfree(scan_res);
		goto out;
	 }
	slsi_gscan_hash_add(sdev, scan_res);

out:
	slsi_kfree_skb(skb);
}

u8 slsi_gscan_get_scan_policy(enum wifi_band band)
{
	u8 scan_policy;

	switch (band) {
	case WIFI_BAND_UNSPECIFIED:
		scan_policy = FAPI_SCANPOLICY_ANY_RA;
		break;
	case WIFI_BAND_BG:
		scan_policy = FAPI_SCANPOLICY_2_4GHZ;
		break;
	case WIFI_BAND_A:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS);
		break;
	case WIFI_BAND_A_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_DFS);
		break;
	case WIFI_BAND_A_WITH_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_DFS);
		break;
	case WIFI_BAND_ABG:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_2_4GHZ);
		break;
	case WIFI_BAND_ABG_WITH_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_DFS |
			       FAPI_SCANPOLICY_2_4GHZ);
		break;
	default:
		scan_policy = FAPI_SCANPOLICY_ANY_RA;
		break;
	}

	SLSI_DBG2_NODEV(SLSI_GSCAN, "Scan Policy: %#x\n", scan_policy);

	return scan_policy;
}

static int slsi_gscan_add_read_params(struct slsi_nl_gscan_param *nl_gscan_param, const void *data, int len)
{
	int                         j = 0;
	int                         type, tmp, tmp1, tmp2, k = 0;
	const struct nlattr         *iter, *iter1, *iter2;
	struct slsi_nl_bucket_param *nl_bucket;

	nla_for_each_attr(iter, data, len, tmp) {
		if (!iter)
			return -EINVAL;

		type = nla_type(iter);

		if (j >= SLSI_GSCAN_MAX_BUCKETS)
			break;

		switch (type) {
		case GSCAN_ATTRIBUTE_BASE_PERIOD:
			if (nla_len(iter) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_gscan_param->base_period = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_NUM_AP_PER_SCAN:
			if (nla_len(iter) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_gscan_param->max_ap_per_scan = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_REPORT_THRESHOLD:
			if (nla_len(iter) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_gscan_param->report_threshold_percent = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_REPORT_THRESHOLD_NUM_SCANS:
			if (nla_len(iter) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_gscan_param->report_threshold_num_scans = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_NUM_BUCKETS:
			if (nla_len(iter) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_gscan_param->num_buckets = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_CH_BUCKET_1:
		case GSCAN_ATTRIBUTE_CH_BUCKET_2:
		case GSCAN_ATTRIBUTE_CH_BUCKET_3:
		case GSCAN_ATTRIBUTE_CH_BUCKET_4:
		case GSCAN_ATTRIBUTE_CH_BUCKET_5:
		case GSCAN_ATTRIBUTE_CH_BUCKET_6:
		case GSCAN_ATTRIBUTE_CH_BUCKET_7:
		case GSCAN_ATTRIBUTE_CH_BUCKET_8:
			nla_for_each_nested(iter1, iter, tmp1) {
				if (!iter1)
					return -EINVAL;

				type = nla_type(iter1);

				nl_bucket = nl_gscan_param->nl_bucket;

				switch (type) {
				case GSCAN_ATTRIBUTE_BUCKET_ID:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].bucket_index = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_PERIOD:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].period = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_NUM_CHANNELS:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].num_channels = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_CHANNELS:
					nla_for_each_nested(iter2, iter1, tmp2) {
						if (k >= SLSI_GSCAN_MAX_CHANNELS)
							break;

						if (nla_len(iter2) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
							return -EINVAL;

						nl_bucket[j].channels[k].channel = nla_get_u32(iter2);
						k++;
					}
					k = 0;
					break;
				case GSCAN_ATTRIBUTE_BUCKETS_BAND:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].band = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_REPORT_EVENTS:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].report_events = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_EXPONENT:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].exponent = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_STEP_COUNT:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].step_count = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_MAX_PERIOD:
					if (nla_len(iter1) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN))
						return -EINVAL;
					nl_bucket[j].max_period = nla_get_u32(iter1);
					break;
				default:
					SLSI_ERR_NODEV("No ATTR_BUKTS_type - %x\n", type);
					break;
				}
			}
			j++;
			break;
		default:
			SLSI_ERR_NODEV("No GSCAN_ATTR_CH_BUKT_type - %x\n", type);
			break;
		}
	}

	return 0;
}

int slsi_gscan_add_verify_params(struct slsi_nl_gscan_param *nl_gscan_param)
{
	int i;

	if ((nl_gscan_param->max_ap_per_scan < 0) || (nl_gscan_param->max_ap_per_scan > SLSI_GSCAN_MAX_AP_CACHE_PER_SCAN)) {
		SLSI_ERR_NODEV("Invalid max_ap_per_scan: %d\n", nl_gscan_param->max_ap_per_scan);
		return -EINVAL;
	}

	if ((nl_gscan_param->report_threshold_percent < 0) || (nl_gscan_param->report_threshold_percent > SLSI_GSCAN_MAX_SCAN_REPORTING_THRESHOLD)) {
		SLSI_ERR_NODEV("Invalid report_threshold_percent: %d\n", nl_gscan_param->report_threshold_percent);
		return -EINVAL;
	}

	if ((nl_gscan_param->num_buckets <= 0) || (nl_gscan_param->num_buckets > SLSI_GSCAN_MAX_BUCKETS)) {
		SLSI_ERR_NODEV("Invalid num_buckets: %d\n", nl_gscan_param->num_buckets);
		return -EINVAL;
	}

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		if ((nl_gscan_param->nl_bucket[i].band == WIFI_BAND_UNSPECIFIED) && (nl_gscan_param->nl_bucket[i].num_channels == 0)) {
			SLSI_ERR_NODEV("No band/channels provided for gscan: band = %d, num_channel = %d\n",
				       nl_gscan_param->nl_bucket[i].band, nl_gscan_param->nl_bucket[i].num_channels);
			return -EINVAL;
		}

		if (nl_gscan_param->nl_bucket[i].report_events > 4) {
			SLSI_ERR_NODEV("Unsupported report event: report_event = %d\n", nl_gscan_param->nl_bucket[i].report_events);
			return -EINVAL;
		}
	}

	return 0;
}

void slsi_gscan_add_to_list(struct slsi_gscan **sdev_gscan, struct slsi_gscan *gscan)
{
	gscan->next = *sdev_gscan;
	*sdev_gscan = gscan;
}

int slsi_gscan_alloc_buckets(struct slsi_dev *sdev, struct slsi_gscan *gscan, int num_buckets)
{
	int i;
	int bucket_index = 0;
	int free_buckets = 0;

	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		if (!sdev->bucket[i].used)
			free_buckets++;

	if (num_buckets > free_buckets) {
		SLSI_ERR_NODEV("Not enough free buckets, num_buckets = %d, free_buckets = %d\n",
			       num_buckets, free_buckets);
		return -EINVAL;
	}

	/* Allocate free buckets for the current gscan */
	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		if (!sdev->bucket[i].used) {
			sdev->bucket[i].used = true;
			sdev->bucket[i].gscan = gscan;
			gscan->bucket[bucket_index] = &sdev->bucket[i];
			bucket_index++;
			if (bucket_index == num_buckets)
				break;
		}

	return 0;
}

static void slsi_gscan_free_buckets(struct slsi_gscan *gscan)
{
	struct slsi_bucket *bucket;
	int                i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "gscan = %p, num_buckets = %d\n", gscan, gscan->num_buckets);

	for (i = 0; i < gscan->num_buckets; i++) {
		bucket = gscan->bucket[i];

		SLSI_DBG2_NODEV(SLSI_GSCAN, "bucket = %p, used = %d, report_events = %d, scan_id = %#x, gscan = %p\n",
				bucket, bucket->used, bucket->report_events, bucket->scan_id, bucket->gscan);
		if (bucket->used) {
			bucket->used = false;
			bucket->report_events = 0;
			bucket->gscan = NULL;
		}
	}
}

void slsi_gscan_flush_scan_results(struct slsi_dev *sdev)
{
	struct netdev_vif        *ndev_vif;
	struct slsi_gscan_result *temp;
	int                      i;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL");
		return;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		while (sdev->gscan_hash_table[i]) {
			temp = sdev->gscan_hash_table[i];
			sdev->gscan_hash_table[i] = sdev->gscan_hash_table[i]->hnext;
			sdev->num_gscan_results--;
			sdev->buffer_consumed -= temp->scan_res_len;
			kfree(temp);
		}

	SLSI_DBG2(sdev, SLSI_GSCAN, "num_gscan_results: %d, buffer_consumed = %d\n",
		  sdev->num_gscan_results, sdev->buffer_consumed);

	if (sdev->num_gscan_results != 0)
		SLSI_WARN_NODEV("sdev->num_gscan_results is not zero\n");

	if (sdev->buffer_consumed != 0)
		SLSI_WARN_NODEV("sdev->buffer_consumedis not zero\n");

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

static int slsi_gscan_add_mlme(struct slsi_dev *sdev, struct slsi_nl_gscan_param *nl_gscan_param, struct slsi_gscan *gscan)
{
	struct slsi_gscan_param      gscan_param;
	struct net_device            *dev;
	int                          ret = 0;
	int                          i;
#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
	u8 mac_addr_mask[ETH_ALEN] = {0xFF};
#endif

	dev = slsi_gscan_get_netdev(sdev);

	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		u16 report_mode = 0;

		gscan_param.nl_bucket = &nl_gscan_param->nl_bucket[i]; /* current bucket */
		gscan_param.bucket = gscan->bucket[i];

		if (gscan_param.bucket->report_events) {
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_EACH_SCAN)
				report_mode |= FAPI_REPORTMODE_END_OF_SCAN_CYCLE;
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_FULL_RESULTS)
				report_mode |= FAPI_REPORTMODE_REAL_TIME;
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_NO_BATCH)
				report_mode |= FAPI_REPORTMODE_NO_BATCH;
		} else {
			report_mode = FAPI_REPORTMODE_RESERVED;
		}

		if (report_mode == 0) {
			SLSI_NET_ERR(dev, "Invalid report event value: %d\n", gscan_param.bucket->report_events);
			return -EINVAL;
		}

		/* In case of epno no_batch mode should be set. */
		if (sdev->epno_active)
			report_mode |= FAPI_REPORTMODE_NO_BATCH;

#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
		if (sdev->scan_addr_set == 1) {
			memset(mac_addr_mask, 0xFF, ETH_ALEN);
			for (i = 3; i < ETH_ALEN; i++)
				mac_addr_mask[i] = 0x00;
			ret = slsi_set_mac_randomisation_mask(sdev, mac_addr_mask);
			if (ret)
				sdev->scan_addr_set = 0;
		} else {
			memset(mac_addr_mask, 0xFF, ETH_ALEN);
			slsi_set_mac_randomisation_mask(sdev, mac_addr_mask);
		}
#endif
		ret = slsi_mlme_add_scan(sdev,
					 dev,
					 FAPI_SCANTYPE_GSCAN,
					 report_mode,
					 0,     /* n_ssids */
					 NULL,  /* ssids */
					 nl_gscan_param->nl_bucket[i].num_channels,
					 NULL,  /* ieee80211_channel */
					 &gscan_param,
					 NULL,  /* ies */
					 0,     /* ies_len */
					 false /* wait_for_ind */);

		if (ret != 0) {
			SLSI_NET_ERR(dev, "Failed to add bucket: %d\n", i);

			/* Delete the scan those are already added */
			for (i = (i - 1); i >= 0; i--)
				slsi_mlme_del_scan(sdev, dev, gscan->bucket[i]->scan_id, false);
			break;
		}
	}

	return ret;
}

static int slsi_gscan_add(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int                        ret = 0;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_nl_gscan_param *nl_gscan_param = NULL;
	struct slsi_gscan          *gscan;
	struct netdev_vif          *ndev_vif;
	int                        buffer_threshold;
	int                        i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_ADD_GSCAN\n");

	if (!sdev) {
		SLSI_WARN_NODEV("sdev is NULL\n");
		return -EINVAL;
	}

	if (!slsi_dev_gscan_supported())
		return -ENOTSUPP;

	ndev_vif = slsi_gscan_get_vif(sdev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	/* Allocate memory for the received scan params */
	nl_gscan_param = kzalloc(sizeof(*nl_gscan_param), GFP_KERNEL);
	if (nl_gscan_param == NULL) {
		SLSI_ERR_NODEV("Failed for allocate memory for gscan params\n");
		ret = -ENOMEM;
		goto exit;
	}

	slsi_gscan_add_read_params(nl_gscan_param, data, len);

#ifdef CONFIG_SCSC_WLAN_DEBUG
	slsi_gscan_add_dump_params(nl_gscan_param);
#endif

	ret = slsi_gscan_add_verify_params(nl_gscan_param);
	if (ret) {
		/* After adding a hotlist a new gscan is added with 0 buckets - return success */
		if (nl_gscan_param->num_buckets == 0) {
			kfree(nl_gscan_param);
			SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
			return 0;
		}

		goto exit;
	}

	/* Allocate Memory for the new gscan */
	gscan = kzalloc(sizeof(*gscan), GFP_KERNEL);
	if (gscan == NULL) {
		SLSI_ERR_NODEV("Failed to allocate memory for gscan\n");
		ret = -ENOMEM;
		goto exit;
	}

	gscan->num_buckets = nl_gscan_param->num_buckets;
	gscan->report_threshold_percent = nl_gscan_param->report_threshold_percent;
	gscan->report_threshold_num_scans = nl_gscan_param->report_threshold_num_scans;
	gscan->nl_bucket = nl_gscan_param->nl_bucket[0];

	/* If multiple gscan is added; consider the lowest report_threshold_percent */
	buffer_threshold = (SLSI_GSCAN_MAX_SCAN_CACHE_SIZE * nl_gscan_param->report_threshold_percent) / 100;
	if ((sdev->buffer_threshold == 0) || (buffer_threshold < sdev->buffer_threshold))
		sdev->buffer_threshold = buffer_threshold;

	ret = slsi_gscan_alloc_buckets(sdev, gscan, nl_gscan_param->num_buckets);
	if (ret)
		goto exit_with_gscan_free;

	for (i = 0; i < nl_gscan_param->num_buckets; i++)
		gscan->bucket[i]->report_events = nl_gscan_param->nl_bucket[i].report_events;

	ret = slsi_gscan_add_mlme(sdev, nl_gscan_param, gscan);
	if (ret) {
		/* Free the buckets */
		slsi_gscan_free_buckets(gscan);

		goto exit_with_gscan_free;
	}

	slsi_gscan_add_to_list(&sdev->gscan, gscan);

	goto exit;

exit_with_gscan_free:
	kfree(gscan);
exit:
	kfree(nl_gscan_param);

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return ret;
}

static int slsi_gscan_del(struct wiphy *wiphy,
			  struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev;
	struct netdev_vif *ndev_vif;
	struct slsi_gscan *gscan;
	int               ret = 0;
	int               i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_DEL_GSCAN\n");

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	while (sdev->gscan != NULL) {
		gscan = sdev->gscan;

		SLSI_DBG3(sdev, SLSI_GSCAN, "gscan = %p, num_buckets = %d\n", gscan, gscan->num_buckets);

		for (i = 0; i < gscan->num_buckets; i++)
			if (gscan->bucket[i]->used)
				slsi_mlme_del_scan(sdev, dev, gscan->bucket[i]->scan_id, false);
		slsi_gscan_free_buckets(gscan);
		sdev->gscan = gscan->next;
		kfree(gscan);
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);

	slsi_gscan_flush_scan_results(sdev);

	sdev->buffer_threshold = 0;

	return ret;
}

static int slsi_gscan_get_scan_results(struct wiphy *wiphy,
				       struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct sk_buff           *skb;
	struct slsi_gscan_result *scan_res;
	struct nlattr            *scan_hdr;
	struct netdev_vif        *ndev_vif;
	int                      num_results = 0;
	int                      mem_needed;
	const struct nlattr      *attr;
	int                      nl_num_results = 0;
	int                      ret = 0;
	int                      temp;
	int                      type;
	int                      i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_SCAN_RESULTS\n");

	/* Read the number of scan results need to be given */
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case GSCAN_ATTRIBUTE_NUM_OF_RESULTS:
			if (nla_len(attr) != SLSI_NL_ATTRIBUTE_U32_LEN)
				return -EINVAL;
			nl_num_results = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			break;
		}
	}

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL\n");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	num_results = sdev->num_gscan_results;

	SLSI_DBG3(sdev, SLSI_GSCAN, "nl_num_results: %d, num_results = %d\n", nl_num_results, sdev->num_gscan_results);

	if (num_results == 0) {
		SLSI_DBG1(sdev, SLSI_GSCAN, "No scan results available\n");
		/* Return value should be 0 for this scenario */
		goto exit;
	}

	/* Find the number of results to return */
	if (num_results > nl_num_results)
		num_results = nl_num_results;

	/* 12 bytes additional for scan_id, flags and num_resuls */
	mem_needed = num_results * sizeof(struct slsi_nl_scan_result_param) + 12;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, mem_needed);
	if (skb == NULL) {
		SLSI_ERR_NODEV("skb alloc failed");
		ret = -ENOMEM;
		goto exit;
	}

	scan_hdr = nla_nest_start(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS);
	if (scan_hdr == NULL) {
		kfree_skb(skb);
		SLSI_ERR_NODEV("scan_hdr is NULL.\n");
		ret = -ENOMEM;
		goto exit;
	}

	nla_put_u32(skb, GSCAN_ATTRIBUTE_SCAN_ID, 0);
	nla_put_u8(skb, GSCAN_ATTRIBUTE_SCAN_FLAGS, 0);
	nla_put_u32(skb, GSCAN_ATTRIBUTE_NUM_OF_RESULTS, num_results);

	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		while (sdev->gscan_hash_table[i]) {
			scan_res = sdev->gscan_hash_table[i];
			sdev->gscan_hash_table[i] = sdev->gscan_hash_table[i]->hnext;
			sdev->num_gscan_results--;
			sdev->buffer_consumed -= scan_res->scan_res_len;
			/* TODO: If IE is included then HAL is not able to parse the results */
			nla_put(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS, sizeof(struct slsi_nl_scan_result_param), &scan_res->nl_scan_res);
			kfree(scan_res);
			num_results--;
			if (num_results == 0)
				goto out;
		}
out:
	nla_nest_end(skb, scan_hdr);

	ret = cfg80211_vendor_cmd_reply(skb);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return ret;
}

void slsi_rx_rssi_report_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_rssi_monitor_evt event_data;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	SLSI_ETHER_COPY(event_data.bssid, fapi_get_buff(skb, u.mlme_rssi_report_ind.bssid));
	event_data.rssi = fapi_get_s16(skb, u.mlme_rssi_report_ind.rssi);
	SLSI_DBG3(sdev, SLSI_GSCAN, "RSSI threshold breached, Current RSSI for %pM= %d\n", event_data.bssid, event_data.rssi);
	slsi_vendor_event(sdev, SLSI_NL80211_RSSI_REPORT_EVENT, &event_data, sizeof(event_data));
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
static int slsi_key_mgmt_set_pmk(struct wiphy *wiphy,
				 struct wireless_dev *wdev, const void *pmk, int pmklen)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif  *ndev_vif;
	int r = 0;

	if (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "Not required to set PMK for P2P client\n");
		return r;
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_PMK Received\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	r = slsi_mlme_set_pmk(sdev, net_dev, pmk, (u16)pmklen);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}
#endif

static int slsi_set_bssid_blacklist(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;
	int                      temp1;
	int                      type;
	const struct nlattr      *attr;
	u32 num_bssids = 0;
	u8 i = 0;
	int ret;
	u8 *bssid = NULL;
	struct cfg80211_acl_data *acl_data = NULL;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_SET_BSSID_BLACK_LIST\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (!net_dev) {
		SLSI_WARN_NODEV("net_dev is NULL\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(net_dev);
	/*This subcmd can be issued in either connected or disconnected state.
	  * Hence using scan_mutex and not vif_mutex
	  */
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	nla_for_each_attr(attr, data, len, temp1) {
		if (!attr) {
			ret = -EINVAL;
			break;
		}

		type = nla_type(attr);

		switch (type) {
		case GSCAN_ATTRIBUTE_NUM_BSSID:
			if (acl_data)
				break;

			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN)) {
				ret = -EINVAL;
				goto exit;
			}

			num_bssids = nla_get_u32(attr);
			if (num_bssids == 0 || (num_bssids > (u32)((ULONG_MAX - sizeof(*acl_data))/sizeof(struct mac_address)))) {
				ret = -EINVAL;
				goto exit;
			}
			acl_data = kmalloc(sizeof(*acl_data) + (sizeof(struct mac_address) * num_bssids), GFP_KERNEL);
			if (!acl_data) {
				ret = -ENOMEM;
				goto exit;
			}
			acl_data->n_acl_entries = num_bssids;
			break;

		case GSCAN_ATTRIBUTE_BLACKLIST_BSSID:
			if (!acl_data) {
				ret = -EINVAL;
				goto exit;
			}

			if (nla_len(attr) != 6) { /*Attribute length should be equal to length of mac address which is 6 bytes.*/
				ret = -EINVAL;
				goto exit;
			}

			if (i >= num_bssids) {
				ret = -EINVAL;
				goto exit;
			}

			bssid = (u8 *)nla_data(attr);

			SLSI_ETHER_COPY(acl_data->mac_addrs[i].addr, bssid);
			SLSI_DBG3_NODEV(SLSI_GSCAN, "mac_addrs[%d]:%pM)\n", i, acl_data->mac_addrs[i].addr);
			i++;
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (acl_data) {
		acl_data->acl_policy = FAPI_ACLPOLICY_BLACKLIST;
		ret = slsi_mlme_set_acl(sdev, net_dev, 0, acl_data);
		if (ret)
			SLSI_ERR_NODEV("Failed to set bssid blacklist\n");
	} else {
		ret =  -EINVAL;
	}
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	kfree(acl_data);
	return ret;
}

static int slsi_start_keepalive_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;

	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u16 ip_pkt_len = 0;
	u8 *ip_pkt = NULL, *src_mac_addr = NULL, *dst_mac_addr = NULL;
	u32 period = 0;
	struct slsi_peer *peer;
	struct sk_buff *skb;
	struct ethhdr *ehdr;
	int r = 0;
	u16 host_tag = 0;
	u8 index = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "SUBCMD_START_KEEPALIVE_OFFLOAD received\n");
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN_NODEV("ndev_vif is not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_WARN_NODEV("ndev_vif->vif_type is not FAPI_VIFTYPE_STATION\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_WARN_NODEV("ndev_vif->sta.vif_status is not SLSI_VIF_STATUS_CONNECTED\n");
		r = -EINVAL;
		goto exit;
	}

	peer = slsi_get_peer_from_qs(sdev, net_dev, SLSI_STA_PEER_QUEUESET);
	if (!peer) {
		SLSI_WARN_NODEV("peer is NULL\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN:
			ip_pkt_len = nla_get_u16(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT:
			ip_pkt = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC:
			period = nla_get_u32(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR:
			dst_mac_addr = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR:
			src_mac_addr = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_ID:
			index = nla_get_u8(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	/* Stop any existing request. This may fail if no request exists
	  * so ignore the return value
	  */
	slsi_mlme_send_frame_mgmt(sdev, net_dev, NULL, 0,
				  FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME,
				  FAPI_MESSAGETYPE_ANY_OTHER,
				  ndev_vif->sta.keepalive_host_tag[index - 1], 0, 0, 0);

	skb = slsi_alloc_skb_headroom(sizeof(struct ethhdr) + ip_pkt_len, GFP_KERNEL);
	if (!skb) {
		SLSI_WARN_NODEV("Memory allocation failed for skb\n");
		r = -ENOMEM;
		goto exit;
	}

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, sizeof(struct ethhdr));

	/* Ethernet Header */
	ehdr = (struct ethhdr *)skb_put(skb, sizeof(struct ethhdr));

	if (dst_mac_addr)
		SLSI_ETHER_COPY(ehdr->h_dest, dst_mac_addr);
	if (src_mac_addr)
		SLSI_ETHER_COPY(ehdr->h_source, src_mac_addr);
	ehdr->h_proto = cpu_to_be16(ETH_P_IP);
	if (ip_pkt)
		memcpy(skb_put(skb, ip_pkt_len), ip_pkt, ip_pkt_len);

	skb->dev = net_dev;
	skb->protocol = ETH_P_IP;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* Queueset 0 AC 0 */
	skb->queue_mapping = slsi_netif_get_peer_queue(0, 0);

	/* Enabling the "Don't Fragment" Flag in the IP Header */
	ip_hdr(skb)->frag_off |= htons(IP_DF);

	/* Calculation of IP header checksum */
	ip_hdr(skb)->check = 0;
	ip_send_check(ip_hdr(skb));

	host_tag = slsi_tx_mgmt_host_tag(sdev);
	r = slsi_mlme_send_frame_data(sdev, net_dev, skb, FAPI_MESSAGETYPE_ANY_OTHER, host_tag,
				      0, (period * 1000));
	if (r == 0)
		ndev_vif->sta.keepalive_host_tag[index - 1] = host_tag;
	else
		slsi_kfree_skb(skb);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
#else
	SLSI_DBG3_NODEV(SLSI_MLME, "SUBCMD_START_KEEPALIVE_OFFLOAD received\n");
	SLSI_DBG3_NODEV(SLSI_MLME, "NAT Keep Alive Feature is disabled\n");
	return -EOPNOTSUPP;

#endif
}

static int slsi_stop_keepalive_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;
	int r = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 index = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "SUBCMD_STOP_KEEPALIVE_OFFLOAD received\n");
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "VIF is not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_WARN(sdev, "Not a STA VIF\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_WARN(sdev, "VIF is not connected\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_ID:
			index = nla_get_u8(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	r = slsi_mlme_send_frame_mgmt(sdev, net_dev, NULL, 0, FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME,
				      FAPI_MESSAGETYPE_ANY_OTHER, ndev_vif->sta.keepalive_host_tag[index - 1], 0, 0, 0);
	ndev_vif->sta.keepalive_host_tag[index - 1] = 0;

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
#else
	SLSI_DBG3_NODEV(SLSI_MLME, "SUBCMD_STOP_KEEPALIVE_OFFLOAD received\n");
	SLSI_DBG3_NODEV(SLSI_MLME, "NAT Keep Alive Feature is disabled\n");
	return -EOPNOTSUPP;

#endif
}

static inline int slsi_epno_ssid_list_get(struct slsi_dev *sdev,
					  struct slsi_epno_ssid_param *epno_ssid_params, const struct nlattr *outer)
{
	int type, tmp;
	u8  epno_auth;
	u8  len = 0;
	const struct nlattr *inner;

	nla_for_each_nested(inner, outer, tmp) {
		type = nla_type(inner);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_FLAGS:
			epno_ssid_params->flags |= nla_get_u16(inner);
			break;
		case SLSI_ATTRIBUTE_EPNO_AUTH:
			epno_auth = nla_get_u8(inner);
			if (epno_auth & SLSI_EPNO_AUTH_FIELD_WEP_OPEN)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_OPEN;
			else if (epno_auth & SLSI_EPNO_AUTH_FIELD_WPA_PSK)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_PSK;
			else if (epno_auth & SLSI_EPNO_AUTH_FIELD_WPA_EAP)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_EAPOL;
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_LEN:
			len = nla_get_u8(inner);
			if (len <= 32) {
				epno_ssid_params->ssid_len = len;
			} else {
				SLSI_ERR(sdev, "SSID too long %d\n", len);
				return -EINVAL;
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID:
			memcpy(epno_ssid_params->ssid, nla_data(inner), len);
			break;
		default:
			SLSI_WARN(sdev, "Ignoring unknown type:%d\n", type);
		}
	}
	return 0;
}

static int slsi_set_epno_ssid(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev             *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device           *net_dev;
	struct netdev_vif           *ndev_vif;
	int                         r = 0;
	int                         tmp, tmp1, type, num = 0;
	const struct nlattr         *outer, *iter;
	u8                          i = 0;
	struct slsi_epno_ssid_param *epno_ssid_params;
	struct slsi_epno_param *epno_params;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_EPNO_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	epno_params = kmalloc((sizeof(*epno_params) + (sizeof(*epno_ssid_params) * SLSI_GSCAN_MAX_EPNO_SSIDS)),
			      GFP_KERNEL);
	if (!epno_params) {
		SLSI_ERR(sdev, "Mem alloc fail\n");
		return -ENOMEM;
	}
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);
	nla_for_each_attr(iter, data, len, tmp1) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_MINIMUM_5G_RSSI:
			epno_params->min_5g_rssi = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_MINIMUM_2G_RSSI:
			epno_params->min_2g_rssi = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_INITIAL_SCORE_MAX:
			epno_params->initial_score_max = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_CUR_CONN_BONUS:
			epno_params->current_connection_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SAME_NETWORK_BONUS:
			epno_params->same_network_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SECURE_BONUS:
			epno_params->secure_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_5G_BONUS:
			epno_params->band_5g_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_LIST:
			nla_for_each_nested(outer, iter, tmp) {
				epno_ssid_params = &epno_params->epno_ssid[i];
				epno_ssid_params->flags = 0;
				r = slsi_epno_ssid_list_get(sdev, epno_ssid_params, outer);
				if (r)
					goto exit;
				i++;
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_NUM:
			num = nla_get_u8(iter);
			if (num > SLSI_GSCAN_MAX_EPNO_SSIDS) {
				SLSI_ERR(sdev, "Cannot support %d SSIDs. max %d\n", num, SLSI_GSCAN_MAX_EPNO_SSIDS);
				r = -EINVAL;
				goto exit;
			}
			epno_params->num_networks = num;
			break;
		default:
			SLSI_ERR(sdev, "Invalid attribute %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	if (i != num) {
		SLSI_ERR(sdev, "num_ssid %d does not match ssids sent %d\n", num, i);
		r = -EINVAL;
		goto exit;
	}
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, num, epno_params, NULL);
	if (r == 0)
		sdev->epno_active = (num != 0);
	else
		sdev->epno_active = false;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	kfree(epno_params);
	return r;
}

static int slsi_set_hs_params(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device          *net_dev;
	struct netdev_vif          *ndev_vif;
	int                        r = 0;
	int                        tmp, tmp1, tmp2, type, num = 0;
	const struct nlattr        *outer, *inner, *iter;
	u8                         i = 0;
	struct slsi_epno_hs2_param *epno_hs2_params_array;
	struct slsi_epno_hs2_param *epno_hs2_params;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_HS_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	epno_hs2_params_array = kmalloc(sizeof(*epno_hs2_params_array) * SLSI_GSCAN_MAX_EPNO_HS2_PARAM, GFP_KERNEL);
	if (!epno_hs2_params_array) {
		SLSI_ERR(sdev, "Mem alloc fail\n");
		return -ENOMEM;
	}

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	nla_for_each_attr(iter, data, len, tmp2) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_HS_PARAM_LIST:
			nla_for_each_nested(outer, iter, tmp) {
				epno_hs2_params = &epno_hs2_params_array[i];
				i++;
				nla_for_each_nested(inner, outer, tmp1) {
					type = nla_type(inner);

					switch (type) {
					case SLSI_ATTRIBUTE_EPNO_HS_ID:
						epno_hs2_params->id = (u32)nla_get_u32(inner);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_REALM:
						memcpy(epno_hs2_params->realm, nla_data(inner), 256);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_CONSORTIUM_IDS:
						memcpy(epno_hs2_params->roaming_consortium_ids, nla_data(inner), 16 * 8);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_PLMN:
						memcpy(epno_hs2_params->plmn, nla_data(inner), 3);
						break;
					default:
						SLSI_WARN(sdev, "Ignoring unknown type:%d\n", type);
					}
				}
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_HS_NUM:
			num = nla_get_u8(iter);
			if (num > SLSI_GSCAN_MAX_EPNO_HS2_PARAM) {
				SLSI_ERR(sdev, "Cannot support %d SSIDs. max %d\n", num, SLSI_GSCAN_MAX_EPNO_SSIDS);
				r = -EINVAL;
				goto exit;
			}
			break;
		default:
			SLSI_ERR(sdev, "Invalid attribute %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}
	if (i != num) {
		SLSI_ERR(sdev, "num_ssid %d does not match ssids sent %d\n", num, i);
		r = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, num, NULL, epno_hs2_params_array);
	if (r == 0)
		sdev->epno_active = true;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	kfree(epno_hs2_params_array);
	return r;
}

static int slsi_reset_hs_params(struct wiphy *wiphy,
				struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif  *ndev_vif;
	int                r;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_RESET_HS_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, 0, NULL, NULL);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	sdev->epno_active = false;
	return r;
}

static int slsi_set_rssi_monitor(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device          *net_dev;
	struct netdev_vif          *ndev_vif;
	int                        r = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	s8 min_rssi = 0, max_rssi = 0;
	u16 enable = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "Recd RSSI monitor command\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev == NULL) {
		SLSI_ERR(sdev, "netdev is NULL!!\n");
		return -ENODEV;
	}

	ndev_vif = netdev_priv(net_dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_ERR(sdev, "Vif not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_ERR(sdev, "Not a STA vif\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_ERR(sdev, "STA vif not connected\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_RSSI_MONITOR_ATTRIBUTE_START:
			enable = (u16)nla_get_u8(attr);
			break;
		case SLSI_RSSI_MONITOR_ATTRIBUTE_MIN_RSSI:
			min_rssi = nla_get_s8(attr);
			break;
		case SLSI_RSSI_MONITOR_ATTRIBUTE_MAX_RSSI:
			max_rssi = nla_get_s8(attr);
			break;
		default:
			r = -EINVAL;
			goto exit;
		}
	}
	if (min_rssi > max_rssi) {
		SLSI_ERR(sdev, "Invalid params, min_rssi= %d ,max_rssi = %d\n", min_rssi, max_rssi);
		r = -EINVAL;
		goto exit;
	}
	r = slsi_mlme_set_rssi_monitor(sdev, net_dev, enable, min_rssi, max_rssi);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}

#ifdef CONFIG_SCSC_WLAN_DEBUG
void slsi_lls_debug_dump_stats(struct slsi_dev *sdev, struct slsi_lls_radio_stat *radio_stat,
			       struct slsi_lls_iface_stat *iface_stat, u8 *buf, int buf_len, int num_of_radios)
{
	int i, j;

	for (j = 0; j < num_of_radios; j++) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "radio_stat====\n");
		SLSI_DBG3(sdev, SLSI_GSCAN, "\tradio_id : %d, on_time : %d, tx_time : %d, rx_time : %d,"
			  "on_time_scan : %d, num_channels : %d\n", radio_stat->radio, radio_stat->on_time,
			  radio_stat->tx_time, radio_stat->rx_time, radio_stat->on_time_scan,
			  radio_stat->num_channels);

		radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat + sizeof(struct slsi_lls_radio_stat) +
			     (sizeof(struct slsi_lls_channel_stat) * radio_stat->num_channels));
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "iface_stat====\n");
	SLSI_DBG3(sdev, SLSI_GSCAN, "\tiface %p info : (mode : %d, mac_addr : %pM, state : %d, roaming : %d,"
		  " capabilities : %d, ssid : %s, bssid : %pM, ap_country_str : [%d%d%d])\trssi_data : %d\n",
		  iface_stat->iface, iface_stat->info.mode, iface_stat->info.mac_addr, iface_stat->info.state,
		  iface_stat->info.roaming, iface_stat->info.capabilities, iface_stat->info.ssid,
		  iface_stat->info.bssid, iface_stat->info.ap_country_str[0], iface_stat->info.ap_country_str[1],
		  iface_stat->info.ap_country_str[2], iface_stat->rssi_data);

	SLSI_DBG3(sdev, SLSI_GSCAN, "\tnum_peers %d\n", iface_stat->num_peers);
	for (i = 0; i < iface_stat->num_peers; i++) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "\t\tpeer_mac_address %pM\n", iface_stat->peer_info[i].peer_mac_address);
	}

	SLSI_DBG_HEX(sdev, SLSI_GSCAN, buf, buf_len, "return buffer\n");
}
#endif

static int slsi_lls_set_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device   *net_dev = NULL;
	struct netdev_vif   *ndev_vif = NULL;
	int                 temp;
	int                 type;
	const struct nlattr *attr;
	u32                 mpdu_size_threshold = 0;
	u32                 aggr_stat_gathering = 0;
	int                 r = 0, i;

	if (!slsi_dev_lls_supported())
		return -EOPNOTSUPP;

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case LLS_ATTRIBUTE_SET_MPDU_SIZE_THRESHOLD:
			mpdu_size_threshold = nla_get_u32(attr);
			break;

		case LLS_ATTRIBUTE_SET_AGGR_STATISTICS_GATHERING:
			aggr_stat_gathering = nla_get_u32(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
		}
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	/* start Statistics measurements in Firmware */
	(void)slsi_mlme_start_link_stats_req(sdev, mpdu_size_threshold, aggr_stat_gathering);

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev) {
		ndev_vif = netdev_priv(net_dev);
		for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
			ndev_vif->rx_packets[i] = 0;
			ndev_vif->tx_packets[i] = 0;
			ndev_vif->tx_no_ack[i] = 0;
		}
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}

static int slsi_lls_clear_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u32 stats_clear_req_mask = 0;
	u32 stop_req             = 0;
	int r = 0, i;
	struct net_device *net_dev = NULL;
	struct netdev_vif *ndev_vif = NULL;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case LLS_ATTRIBUTE_CLEAR_STOP_REQUEST_MASK:
			stats_clear_req_mask = nla_get_u32(attr);
			SLSI_DBG3(sdev, SLSI_GSCAN, "stats_clear_req_mask:%u\n", stats_clear_req_mask);
			break;

		case LLS_ATTRIBUTE_CLEAR_STOP_REQUEST:
			stop_req = nla_get_u32(attr);
			SLSI_DBG3(sdev, SLSI_GSCAN, "stop_req:%u\n", stop_req);
			break;

		default:
			SLSI_ERR(sdev, "Unknown attribute:%d\n", type);
			r = -EINVAL;
		}
	}

	/* stop_req = 0 : clear the stats which are flaged 0
	 * stop_req = 1 : clear the stats which are flaged 1
	 */
	if (!stop_req)
		stats_clear_req_mask = ~stats_clear_req_mask;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	(void)slsi_mlme_stop_link_stats_req(sdev, stats_clear_req_mask);
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev) {
		ndev_vif = netdev_priv(net_dev);
		for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
			ndev_vif->rx_packets[i] = 0;
			ndev_vif->tx_packets[i] = 0;
			ndev_vif->tx_no_ack[i] = 0;
		}
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}

static u32 slsi_lls_ie_to_cap(const u8 *ies, int ies_len)
{
	u32 capabilities = 0;
	const u8 *ie_data;
	const u8 *ie;
	int ie_len;

	if (!ies || ies_len == 0) {
		SLSI_ERR_NODEV("no ie[&%p %d]\n", ies, ies_len);
		return 0;
	}
	ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ies, ies_len);
	if (ie) {
		ie_len = ie[1];
		ie_data = &ie[2];
		if ((ie_len >= 4) && (ie_data[3] & SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED))
			capabilities |= SLSI_LLS_CAPABILITY_INTERWORKING;
		if ((ie_len >= 7) && (ie_data[6] & 0x01)) /* Bit48: UTF-8 ssid */
			capabilities |= SLSI_LLS_CAPABILITY_SSID_UTF8;
	}

	ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, SLSI_WLAN_OUI_TYPE_WFA_HS20_IND, ies, ies_len);
	if (ie)
		capabilities |= SLSI_LLS_CAPABILITY_HS20;
	return capabilities;
}

static void slsi_lls_iface_sta_stats(struct slsi_dev *sdev, struct netdev_vif *ndev_vif,
				     struct slsi_lls_iface_stat *iface_stat)
{
	int                       i;
	struct slsi_lls_interface_link_layer_info *lls_info = &iface_stat->info;
	enum slsi_lls_peer_type   peer_type;
	struct slsi_peer          *peer;
	const u8                  *ie_data, *ie;
	u8                        ie_len;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	if (ndev_vif->ifnum == SLSI_NET_INDEX_WLAN) {
		lls_info->mode = SLSI_LLS_INTERFACE_STA;
		peer_type = SLSI_LLS_PEER_AP;
	} else {
		lls_info->mode = SLSI_LLS_INTERFACE_P2P_CLIENT;
		peer_type = SLSI_LLS_PEER_P2P_GO;
	}

	switch (ndev_vif->sta.vif_status) {
	case SLSI_VIF_STATUS_CONNECTING:
		lls_info->state = SLSI_LLS_AUTHENTICATING;
		break;
	case SLSI_VIF_STATUS_CONNECTED:
		lls_info->state = SLSI_LLS_ASSOCIATED;
		break;
	default:
		lls_info->state = SLSI_LLS_DISCONNECTED;
	}
	lls_info->roaming = ndev_vif->sta.roam_in_progress ?
				SLSI_LLS_ROAMING_ACTIVE : SLSI_LLS_ROAMING_IDLE;

	iface_stat->info.capabilities = 0;
	lls_info->ssid[0] = 0;
	if (ndev_vif->sta.sta_bss) {
		ie = cfg80211_find_ie(WLAN_EID_SSID, ndev_vif->sta.sta_bss->ies->data,
				      ndev_vif->sta.sta_bss->ies->len);
		if (ie) {
			ie_len = ie[1];
			ie_data = &ie[2];
			memcpy(lls_info->ssid, ie_data, ie_len);
			lls_info->ssid[ie_len] = 0;
		}
		SLSI_ETHER_COPY(lls_info->bssid, ndev_vif->sta.sta_bss->bssid);
		ie = cfg80211_find_ie(WLAN_EID_COUNTRY, ndev_vif->sta.sta_bss->ies->data,
				      ndev_vif->sta.sta_bss->ies->len);
		if (ie) {
			ie_data = &ie[2];
			memcpy(lls_info->ap_country_str, ie_data, 3);
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_COUNTRY;
		}
	}

	peer = ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]; /* connected AP */
	if (peer && peer->valid && peer->assoc_ie && peer->assoc_resp_ie) {
		iface_stat->info.capabilities |= slsi_lls_ie_to_cap(peer->assoc_ie->data, peer->assoc_ie->len);
		if (peer->capabilities & WLAN_CAPABILITY_PRIVACY) {
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
			iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
		}
		if (peer->qos_enabled) {
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_QOS;
			iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_QOS;
		}
		iface_stat->peer_info[0].capabilities |= slsi_lls_ie_to_cap(peer->assoc_resp_ie->data, peer->assoc_resp_ie->len);

		SLSI_ETHER_COPY(iface_stat->peer_info[0].peer_mac_address, peer->address);
		iface_stat->peer_info[0].type = peer_type;
		iface_stat->num_peers = 1;
	}

	for (i = MAP_AID_TO_QS(SLSI_TDLS_PEER_INDEX_MIN); i <= MAP_AID_TO_QS(SLSI_TDLS_PEER_INDEX_MAX); i++) {
		peer = ndev_vif->peer_sta_record[i];
		if (peer && peer->valid) {
			SLSI_ETHER_COPY(iface_stat->peer_info[iface_stat->num_peers].peer_mac_address, peer->address);
			iface_stat->peer_info[iface_stat->num_peers].type = SLSI_LLS_PEER_TDLS;
			if (peer->qos_enabled)
				iface_stat->peer_info[iface_stat->num_peers].capabilities |= SLSI_LLS_CAPABILITY_QOS;
			iface_stat->peer_info[iface_stat->num_peers].num_rate = 0;
			iface_stat->num_peers++;
		}
	}
}

static void slsi_lls_iface_ap_stats(struct slsi_dev *sdev, struct netdev_vif *ndev_vif, struct slsi_lls_iface_stat *iface_stat)
{
	enum slsi_lls_peer_type peer_type = SLSI_LLS_PEER_INVALID;
	struct slsi_peer        *peer;
	int                     i;
	struct net_device *dev;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	/* We are AP/GO, so we advertize our own country. */
	memcpy(iface_stat->info.ap_country_str, iface_stat->info.country_str, 3);

	if (ndev_vif->ifnum == SLSI_NET_INDEX_WLAN) {
		iface_stat->info.mode = SLSI_LLS_INTERFACE_SOFTAP;
		peer_type = SLSI_LLS_PEER_STA;
	} else if (ndev_vif->ifnum == SLSI_NET_INDEX_P2PX_SWLAN) {
		dev = sdev->netdev[SLSI_NET_INDEX_P2PX_SWLAN];
		if (SLSI_IS_VIF_INDEX_P2P_GROUP(sdev, ndev_vif)) {
			iface_stat->info.mode = SLSI_LLS_INTERFACE_P2P_GO;
			peer_type = SLSI_LLS_PEER_P2P_CLIENT;
		}
	}

	for (i = MAP_AID_TO_QS(SLSI_PEER_INDEX_MIN); i <= MAP_AID_TO_QS(SLSI_PEER_INDEX_MAX); i++) {
		peer = ndev_vif->peer_sta_record[i];
		if (peer && peer->valid) {
			SLSI_ETHER_COPY(iface_stat->peer_info[iface_stat->num_peers].peer_mac_address, peer->address);
			iface_stat->peer_info[iface_stat->num_peers].type = peer_type;
			iface_stat->peer_info[iface_stat->num_peers].num_rate = 0;
			if (peer->qos_enabled)
				iface_stat->peer_info[iface_stat->num_peers].capabilities = SLSI_LLS_CAPABILITY_QOS;
			iface_stat->num_peers++;
		}
	}

	memcpy(iface_stat->info.ssid, ndev_vif->ap.ssid, ndev_vif->ap.ssid_len);
	iface_stat->info.ssid[ndev_vif->ap.ssid_len] = 0;
	if (ndev_vif->ap.privacy)
		iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
	if (ndev_vif->ap.qos_enabled)
		iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_QOS;
}

static void slsi_lls_iface_stat_fill(struct slsi_dev *sdev,
				     struct net_device *net_dev,
				     struct slsi_lls_iface_stat *iface_stat)
{
	int                       i;
	struct netdev_vif         *ndev_vif;
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_BE + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_BK + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_VI + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_VO + 1, 0 } },
						 { SLSI_PSID_UNIFI_BEACON_RECEIVED, {0, 0} },
						 { SLSI_PSID_UNIFI_PS_LEAKY_AP, {0, 0} },
						 { SLSI_PSID_UNIFI_RSSI, {0, 0} } };

	iface_stat->iface = NULL;
	iface_stat->info.mode = SLSI_LLS_INTERFACE_UNKNOWN;
	iface_stat->info.country_str[0] = sdev->device_config.domain_info.regdomain->alpha2[0];
	iface_stat->info.country_str[1] = sdev->device_config.domain_info.regdomain->alpha2[1];
	iface_stat->info.country_str[2] = ' '; /* 3rd char of our country code is ASCII<space> */

	for (i = 0; i < SLSI_LLS_AC_MAX; i++)
		iface_stat->ac[i].ac = SLSI_LLS_AC_MAX;

	if (!net_dev)
		return;

	ndev_vif = netdev_priv(net_dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated)
		goto exit;

	if (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) {
		slsi_lls_iface_sta_stats(sdev, ndev_vif, iface_stat);
	} else if (ndev_vif->vif_type == FAPI_VIFTYPE_AP) {
		slsi_lls_iface_ap_stats(sdev, ndev_vif, iface_stat);
		SLSI_ETHER_COPY(iface_stat->info.bssid, net_dev->dev_addr);
	}
	SLSI_ETHER_COPY(iface_stat->info.mac_addr, net_dev->dev_addr);

	mibrsp.dataLength = 10 * sizeof(get_values) / sizeof(get_values[0]);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes for interface MIBs\n", mibrsp.dataLength);
		goto exit;
	}

	values = slsi_read_mibs(sdev, net_dev, get_values, sizeof(get_values) / sizeof(get_values[0]), &mibrsp);
	if (!values)
		goto exit;

	for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			iface_stat->ac[i].ac = slsi_fapi_to_android_traffic_q(i);
			iface_stat->ac[i].retries = values[i].u.uintValue;
			iface_stat->ac[i].rx_mpdu = ndev_vif->rx_packets[i];
			iface_stat->ac[i].tx_mpdu = ndev_vif->tx_packets[i];
			iface_stat->ac[i].mpdu_lost = ndev_vif->tx_no_ack[i];
		} else {
			SLSI_WARN(sdev, "LLS: expected datatype 1 but received %d\n", values[i].type);
		}
	}

	if (values[4].type == SLSI_MIB_TYPE_UINT)
		iface_stat->beacon_rx = values[4].u.uintValue;

	if (values[5].type == SLSI_MIB_TYPE_UINT) {
		iface_stat->leaky_ap_detected = values[5].u.uintValue;
		iface_stat->leaky_ap_guard_time = 5; /* 5 milli sec. As mentioned in lls document */
	}

	if (values[6].type == SLSI_MIB_TYPE_INT)
		iface_stat->rssi_data = values[6].u.intValue;

exit:
	kfree(values);
	kfree(mibrsp.data);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

void slsi_check_num_radios(struct slsi_dev *sdev)
{
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_RADIO_SCAN_TIME, { 1, 0 } } };

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return;
	}

	/* Expect each mib length in response is <= 15 So assume 15 bytes for each MIB */
	mibrsp.dataLength = 15 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		sdev->lls_num_radio = 0;
		return;
	}

	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (!values) {
		sdev->lls_num_radio = 0;
	} else {
		sdev->lls_num_radio = values[0].type == SLSI_MIB_TYPE_NONE ? 1 : 2;
		kfree(values);
	}

	kfree(mibrsp.data);
}

static void slsi_lls_radio_stat_fill(struct slsi_dev *sdev, struct net_device *dev,
				     struct slsi_lls_radio_stat *radio_stat,
				     int max_chan_count, int radio_index, int twoorfive)
{
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_data      supported_chan_mib = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_RADIO_SCAN_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_RX_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_TX_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_ON_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_SUPPORTED_CHANNELS, { 0, 0 } } };
	u32                       *radio_data[] = {&radio_stat->on_time_scan, &radio_stat->rx_time,
						   &radio_stat->tx_time, &radio_stat->on_time};
	int                       i, j, chan_count, chan_start, k;

	radio_stat->radio = radio_index;

	/* Expect each mib length in response is <= 15 So assume 15 bytes for each MIB */
	mibrsp.dataLength = 15 * sizeof(get_values) / sizeof(get_values[0]);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (mibrsp.data == NULL) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		return;
	}
	values = slsi_read_mibs(sdev, NULL, get_values, sizeof(get_values) / sizeof(get_values[0]), &mibrsp);
	if (!values)
		goto exit_with_mibrsp;

	for (i = 0; i < (sizeof(get_values) / sizeof(get_values[0])) - 1; i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			*radio_data[i] = values[i].u.uintValue;
		} else {
			SLSI_ERR(sdev, "invalid type. iter:%d", i);
		}
	}
	if (values[4].type != SLSI_MIB_TYPE_OCTET) {
		SLSI_ERR(sdev, "Supported_Chan invalid type.");
		goto exit_with_values;
	}

	supported_chan_mib = values[4].u.octetValue;
	for (j = 0; j < supported_chan_mib.dataLength / 2; j++) {
		struct slsi_lls_channel_info *radio_chan;

		chan_start = supported_chan_mib.data[j * 2];
		chan_count = supported_chan_mib.data[j * 2 + 1];
		if (radio_stat->num_channels + chan_count > max_chan_count)
			chan_count = max_chan_count - radio_stat->num_channels;
		if (chan_start == 1 && (twoorfive & BIT(0))) { /* for 2.4GHz */
			for (k = 0; k < chan_count; k++) {
				radio_chan = &radio_stat->channels[radio_stat->num_channels + k].channel;
				if (k + chan_start == 14)
					radio_chan->center_freq = 2484;
				else
					radio_chan->center_freq = 2407 + (chan_start + k) * 5;
				radio_chan->width = SLSI_LLS_CHAN_WIDTH_20;
			}
			radio_stat->num_channels += chan_count;
		} else if (chan_start != 1 && (twoorfive & BIT(1))) {
			/* for 5GHz */
			for (k = 0; k < chan_count; k++) {
				radio_chan = &radio_stat->channels[radio_stat->num_channels + k].channel;
				radio_chan->center_freq = 5000 + (chan_start + (k * 4)) * 5;
				radio_chan->width = SLSI_LLS_CHAN_WIDTH_20;
			}
			radio_stat->num_channels += chan_count;
		}
	}
exit_with_values:
	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
}

static int slsi_lls_fill(struct slsi_dev *sdev, u8 **src_buf)
{
	struct net_device          *net_dev = NULL;
	struct slsi_lls_radio_stat *radio_stat;
	struct slsi_lls_radio_stat *radio_stat_temp;
	struct slsi_lls_iface_stat *iface_stat;
	int                        buf_len = 0;
	int                        max_chan_count = 0;
	u8                         *buf;
	int                        num_of_radios_supported;
	int i = 0;
	int radio_type[2] = {BIT(0), BIT(1)};

	if (sdev->lls_num_radio == 0) {
		slsi_check_num_radios(sdev);
		if (sdev->lls_num_radio == 0)
			return -EIO;
	}

	num_of_radios_supported = sdev->lls_num_radio;
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);

	if (sdev->wiphy->bands[NL80211_BAND_2GHZ])
		max_chan_count = sdev->wiphy->bands[NL80211_BAND_2GHZ]->n_channels;
	if (sdev->wiphy->bands[NL80211_BAND_5GHZ])
		max_chan_count += sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels;
	buf_len = (int)((num_of_radios_supported * sizeof(struct slsi_lls_radio_stat))
			+ sizeof(struct slsi_lls_iface_stat)
			+ sizeof(u8)
			+ (sizeof(struct slsi_lls_peer_info) * SLSI_ADHOC_PEER_CONNECTIONS_MAX)
			+ (sizeof(struct slsi_lls_channel_stat) * max_chan_count));
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		SLSI_ERR(sdev, "No mem. Size:%d\n", buf_len);
		return -ENOMEM;
	}
	buf[0] = num_of_radios_supported;
	*src_buf = buf;
	iface_stat = (struct slsi_lls_iface_stat *)(buf + sizeof(u8));
	slsi_lls_iface_stat_fill(sdev, net_dev, iface_stat);

	radio_stat = (struct slsi_lls_radio_stat *)(buf + sizeof(u8) + sizeof(struct slsi_lls_iface_stat) +
		     (sizeof(struct slsi_lls_peer_info) * iface_stat->num_peers));
	radio_stat_temp = radio_stat;
	if (num_of_radios_supported == 1) {
		radio_type[0] = BIT(0) | BIT(1);
		slsi_lls_radio_stat_fill(sdev, net_dev, radio_stat, max_chan_count, 0, radio_type[0]);
		radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat + sizeof(struct slsi_lls_radio_stat) +
				     (sizeof(struct slsi_lls_channel_stat) * radio_stat->num_channels));
	} else {
		for (i = 1; i <= num_of_radios_supported ; i++) {
			slsi_lls_radio_stat_fill(sdev, net_dev, radio_stat, max_chan_count, i, radio_type[i - 1]);
			radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat +
					     sizeof(struct slsi_lls_radio_stat) + (sizeof(struct slsi_lls_channel_stat)
					     * radio_stat->num_channels));
		}
	}
#ifdef CONFIG_SCSC_WLAN_DEBUG
	if (slsi_dev_llslogs_supported())
		slsi_lls_debug_dump_stats(sdev, radio_stat_temp, iface_stat, buf, buf_len, num_of_radios_supported);
#endif
	return buf_len;
}

static int slsi_lls_get_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev        *sdev = SDEV_FROM_WIPHY(wiphy);
	int                    ret;
	u8                     *buf = NULL;
	int                    buf_len;

	if (!slsi_dev_lls_supported())
		return -EOPNOTSUPP;

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	if (!sdev) {
		SLSI_ERR(sdev, "sdev is Null\n");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	/* In case of lower layer failure do not read LLS MIBs */
	if (sdev->mlme_blocked)
		buf_len = -EIO;
	else
		buf_len = slsi_lls_fill(sdev, &buf);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	if (buf_len > 0) {
		ret = slsi_vendor_cmd_reply(wiphy, buf, buf_len);
		if (ret)
			SLSI_ERR_NODEV("vendor cmd reply failed (err:%d)\n", ret);
	} else {
		ret = buf_len;
	}
	kfree(buf);
	return ret;
}

static int slsi_gscan_set_oui(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	int ret = 0;

#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION

	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 scan_oui[6];

	memset(&scan_oui, 0, 6);

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	sdev->scan_addr_set = 0;

	nla_for_each_attr(attr, data, len, temp) {
		if (!attr) {
			ret = -EINVAL;
			break;
		}

		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_PNO_RANDOM_MAC_OUI:
		{
			if (nla_len(attr) != 3) {
				ret = -EINVAL;
				break;
			}
			memcpy(&scan_oui, nla_data(attr), 3);
			memcpy(sdev->scan_mac_addr, scan_oui, 6);
			sdev->scan_addr_set = 1;
			break;
		}
		default:
			ret = -EINVAL;
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			break;
		}
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
#endif
	return ret;
}

static int slsi_get_feature_set(struct wiphy *wiphy,
				struct wireless_dev *wdev, const void *data, int len)
{
	u32 feature_set = 0;
	int ret = 0;

	SLSI_DBG3_NODEV(SLSI_GSCAN, "\n");

	feature_set |= SLSI_WIFI_HAL_FEATURE_RSSI_MONITOR;
	feature_set |= SLSI_WIFI_HAL_FEATURE_CONTROL_ROAMING;
	feature_set |= SLSI_WIFI_HAL_FEATURE_TDLS | SLSI_WIFI_HAL_FEATURE_TDLS_OFFCHANNEL;
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	feature_set |= SLSI_WIFI_HAL_FEATURE_MKEEP_ALIVE;
#endif
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
		feature_set |= SLSI_WIFI_HAL_FEATURE_LOGGER;
#endif
	if (slsi_dev_gscan_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_GSCAN;
	if (slsi_dev_lls_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_LINK_LAYER_STATS;
	if (slsi_dev_epno_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_HAL_EPNO;
	if (slsi_dev_nan_supported(SDEV_FROM_WIPHY(wiphy)))
		feature_set |= SLSI_WIFI_HAL_FEATURE_NAN;
	if (slsi_dev_rtt_supported()) {
		feature_set |= SLSI_WIFI_HAL_FEATURE_D2D_RTT;
		feature_set |= SLSI_WIFI_HAL_FEATURE_D2AP_RTT;
	}

	ret = slsi_vendor_cmd_reply(wiphy, &feature_set, sizeof(feature_set));

	return ret;
}

static int slsi_set_country_code(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	int                      ret = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	char country_code[SLSI_COUNTRY_CODE_LEN];

	SLSI_DBG3(sdev, SLSI_GSCAN, "Received country code command\n");

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_COUNTRY_CODE:
		{
			if (nla_len(attr) < (SLSI_COUNTRY_CODE_LEN - 1)) {
				ret = -EINVAL;
				SLSI_ERR(sdev, "Insufficient Country Code Length : %d\n", nla_len(attr));
				return ret;
			}
			memcpy(country_code, nla_data(attr), (SLSI_COUNTRY_CODE_LEN - 1));
			break;
		}
		default:
			ret = -EINVAL;
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			return ret;
		}
	}
	ret = slsi_set_country_update_regd(sdev, country_code, SLSI_COUNTRY_CODE_LEN);
	if (ret < 0)
		SLSI_ERR(sdev, "Set country failed ret:%d\n", ret);
	return ret;
}

static int slsi_rtt_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_rtt_capabilities rtt_cap;
	int                               ret = 0;
	struct slsi_dev                   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_RTT_CAPABILITIES\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_WARN(sdev, "RTT not supported.\n");
		return -ENOTSUPP;
	}
	memset(&rtt_cap, 0, sizeof(struct slsi_rtt_capabilities));

	ret = slsi_mib_get_rtt_cap(sdev, dev, &rtt_cap);
	if (ret != 0) {
		SLSI_ERR(sdev, "Failed to read mib\n");
		return ret;
	}
	ret = slsi_vendor_cmd_reply(wiphy, &rtt_cap, sizeof(struct slsi_rtt_capabilities));
	if (ret)
		SLSI_ERR_NODEV("rtt_get_capabilities vendor cmd reply failed (err = %d)\n", ret);
	return ret;
}

static int slsi_rtt_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int r, type, j = 0;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
#ifdef CONFIG_SCSC_WIFI_NAN_ENABLE
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
#endif
	struct slsi_rtt_config *nl_rtt_params;
	const struct nlattr *iter, *outer, *inner;
	u8 source_addr[ETH_ALEN];
	int tmp, tmp1, tmp2;
	u16 rtt_id = 0;
	u8 num_devices = 0;
	u16 rtt_peer = SLSI_RTT_PEER_AP;
	u16 vif_idx = 0;
	u16 channel_freq = 0;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_RTT_RANGE_START\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_ERR(sdev, "RTT not supported.\n");
		return WIFI_HAL_ERROR_NOT_SUPPORTED;
	}
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_CNT:
			num_devices = nla_get_u8(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target cnt %d\n", num_devices);
			break;
		case SLSI_RTT_ATTRIBUTE_TARGET_ID:
			rtt_id = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target id %d\n", rtt_id);
			break;
		default:
			SLSI_ERR_NODEV("Unexpected RTT attribute:type - %d\n", type);
			break;
		}
	}
	if (!num_devices) {
		SLSI_ERR_NODEV("No device found for rtt configuration!\n");
		return -EINVAL;
	}
	/* Allocate memory for the received config params */
	nl_rtt_params = kcalloc(num_devices, sizeof(*nl_rtt_params), GFP_KERNEL);
	if (!nl_rtt_params) {
		SLSI_ERR_NODEV("Failed for allocate memory for config rtt_param\n");
		return -ENOMEM;
	}
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_INFO:
			nla_for_each_nested(outer, iter, tmp1) {
				nla_for_each_nested(inner, outer, tmp2) {
					switch (nla_type(inner)) {
					case SLSI_RTT_ATTRIBUTE_TARGET_MAC:
						memcpy(nl_rtt_params[j].peer_addr, nla_data(inner), ETH_ALEN);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_TYPE:
						nl_rtt_params[j].type = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PEER:
						rtt_peer = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_CHAN_FREQ:
						channel_freq = nla_get_u16(inner);
						nl_rtt_params[j].channel_freq = channel_freq * 2;
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PERIOD:
						nl_rtt_params[j].burst_period = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_BURST:
						nl_rtt_params[j].num_burst = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_FTM_BURST:
						nl_rtt_params[j].num_frames_per_burst = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTMR:
						nl_rtt_params[j].num_retries_per_ftmr = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_BURST_DURATION:
						nl_rtt_params[j].burst_duration = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PREAMBLE:
						nl_rtt_params[j].preamble = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_BW:
						nl_rtt_params[j].bw = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_LCI:
						nl_rtt_params[j].LCI_request = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_LCR:
						nl_rtt_params[j].LCR_request = nla_get_u16(inner);
						break;
					default:
						SLSI_ERR_NODEV("Unknown RTT INFO ATTRIBUTE type: %d\n", type);
						break;
					}
				}
				j++;
			}
			break;
		default:
			SLSI_ERR_NODEV("No ATTRIBUTE_Target cnt - %d\n", type);
			break;
		}
	}
	if (rtt_peer == SLSI_RTT_PEER_NAN) {
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= 4
		SLSI_ETHER_COPY(source_addr, sdev->netdev_addresses[SLSI_NET_INDEX_NAN]);
#else
		SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
#endif
	} else {
		SLSI_ETHER_COPY(source_addr, sdev->netdev_addresses[SLSI_NET_INDEX_WLAN]);
	}
	if (rtt_peer == SLSI_RTT_PEER_AP) {
		vif_idx = 0;
	} else if (rtt_peer == SLSI_RTT_PEER_NAN) {
#ifdef CONFIG_SCSC_WIFI_NAN_ENABLE
		if (!slsi_dev_nan_supported(sdev)) {
			SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
			kfree(nl_rtt_params);
			return WIFI_HAL_ERROR_NOT_SUPPORTED;
		}
		ndev_vif = netdev_priv(dev);
		if (ndev_vif->activated) {
			vif_idx = ndev_vif->vif_type;
		} else {
			SLSI_ERR(sdev, "NAN vif not activated\n");
			kfree(nl_rtt_params);
			return -EINVAL;
		}
#else
		SLSI_ERR(sdev, "NAN not enabled\n");
		return -ENOTSUPP;
#endif
	}
	r = slsi_mlme_add_range_req(sdev, num_devices, nl_rtt_params, rtt_id, vif_idx, source_addr);
	if (r) {
		r = -EINVAL;
		SLSI_ERR_NODEV("Failed to set rtt config\n");
	} else {
		sdev->rtt_vif[rtt_id] = vif_idx;
		SLSI_DBG1_NODEV(SLSI_GSCAN, "Successfully set rtt config\n");
	}
	kfree(nl_rtt_params);
	return r;
}

int slsi_tx_rate_calc(struct sk_buff *nl_skb, u16 fw_rate, int res, bool tx_rate)
{
	u8 preamble;
	const u32 fw_rate_idx_to_80211_rate[] = { 0, 10, 20, 55, 60, 90, 110, 120, 180, 240, 360, 480, 540 };
	u32 data_rate = 0;
	u32 mcs = 0, nss = 0;
	u32 chan_bw_idx = 0;
	int gi_idx;

	preamble = (fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) >> 14;
	if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_NON_HT_SELECTED) {
		u16 fw_rate_idx = fw_rate & SLSI_FW_API_RATE_INDEX_FIELD;

		if (fw_rate > 0 && fw_rate_idx < ARRAY_SIZE(fw_rate_idx_to_80211_rate))
			data_rate = fw_rate_idx_to_80211_rate[fw_rate_idx];
	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_HT_SELECTED) {
		nss = (SLSI_FW_API_RATE_HT_NSS_FIELD & fw_rate) >> 6;
		chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
		gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
		mcs = SLSI_FW_API_RATE_HT_MCS_FIELD & fw_rate;
		if ((chan_bw_idx < 2) && (mcs <= 7)) {
			data_rate = (nss + 1) * slsi_rates_table[chan_bw_idx][gi_idx][mcs];
		} else if (mcs == 32 && chan_bw_idx == 1) {
			if (gi_idx == 1)
				data_rate = (nss + 1) * 67;
			else
				data_rate = (nss + 1) * 60;
		} else {
			SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x, nss : %d\n",
					fw_rate, chan_bw_idx, mcs, nss);
		}
	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_VHT_SELECTED) {
		/* report vht rate in legacy units and not as mcs index. reason: upper layers may still be not
		 * updated with vht msc table.
		 */
		chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
		gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
		/* Calculate  NSS --> bits 6 to 4*/
		nss = (SLSI_FW_API_RATE_VHT_NSS_FIELD & fw_rate) >> 4;
		mcs = SLSI_FW_API_RATE_VHT_MCS_FIELD & fw_rate;
		/* Bandwidth (BW): 0x0= 20 MHz, 0x1= 40 MHz, 0x2= 80 MHz, 0x3= 160/ 80+80 MHz. 0x3 is not supported */
		if ((chan_bw_idx <= 2) && (mcs <= 9))
			data_rate = (nss + 1) * slsi_rates_table[chan_bw_idx][gi_idx][mcs];
		else
			SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x,nss : %d\n",
					fw_rate, chan_bw_idx, mcs, nss);
		if (nss > 1)
			nss += 1;
	}

	if (tx_rate) {
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_PREAMBLE, preamble);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_NSS, nss);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_BW, chan_bw_idx);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_MCS, mcs);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_RATE, data_rate);
	} else {
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_PREAMBLE, preamble);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_NSS, nss);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_BW, chan_bw_idx);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_MCS, mcs);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_RATE, data_rate);
	}
	return res;
}

void slsi_rx_range_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32 i, tm;
	u16 rtt_entry_count = fapi_get_u16(skb, u.mlme_range_ind.entries);
	u16 rtt_id = fapi_get_u16(skb, u.mlme_range_ind.rtt_id);
	u32 tmac = fapi_get_u32(skb, u.mlme_range_ind.spare_3);
	int data_len = fapi_get_datalen(skb);
	u8                *ip_ptr, *start_ptr;
	u16 tx_data, rx_data;
	struct sk_buff *nl_skb;
	int res = 0;
	struct nlattr *nlattr_nested;
	struct timespec          ts;
	u64 tkernel;
	u8 rep_cnt = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_RTT_RESULT_EVENT, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_RTT_RESULT_EVENT,
					     GFP_KERNEL);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_RESULT_EVENT), SLSI_NL80211_RTT_RESULT_EVENT);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
		}

	ip_ptr = fapi_get_data(skb);
	start_ptr = fapi_get_data(skb);
	res |= nla_put_u16(nl_skb, SLSI_RTT_ATTRIBUTE_RESULT_CNT, rtt_entry_count);
	res |= nla_put_u16(nl_skb, SLSI_RTT_ATTRIBUTE_TARGET_ID, rtt_id);
	res |= nla_put_u8(nl_skb, SLSI_RTT_ATTRIBUTE_RESULTS_PER_TARGET, 1);
	for (i = 0; i < rtt_entry_count; i++) {
		nlattr_nested = nla_nest_start(nl_skb, SLSI_RTT_ATTRIBUTE_RESULT);
		if (!nlattr_nested) {
			SLSI_ERR(sdev, "Error in nla_nest_start\n");
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			goto exit;
		}
		ip_ptr += 7;             /*skip first 7 bytes for fapi_ie_generic */
		res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_ADDR, ETH_ALEN, ip_ptr);
		ip_ptr += 6;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_MEASUREMENT_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_SUCCESS_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NUM_PER_BURST_PEER, *ip_ptr++);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_STATUS, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_RETRY_AFTER_DURATION, *ip_ptr++);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_TYPE, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI_SPREAD, *ip_ptr);
		ip_ptr += 2;
		memcpy(&tx_data, ip_ptr, 2);
		res = slsi_tx_rate_calc(nl_skb, tx_data, res, 1);
		ip_ptr += 2;
		memcpy(&rx_data, ip_ptr, 2);
		res = slsi_tx_rate_calc(nl_skb, rx_data, res, 0);
		ip_ptr += 2;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RTT, *ip_ptr);
		ip_ptr += 4;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SD, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SPREAD, *ip_ptr);
		ip_ptr += 2;
		get_monotonic_boottime(&ts);
		tkernel = (u64)TIMESPEC_TO_US(ts);
		tm = *ip_ptr;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TIMESTAMP_US, tkernel - (tmac - tm));
		ip_ptr += 4;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_DISTANCE_MM, *ip_ptr);
		ip_ptr += 4;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_DISTANCE_SD_MM, *ip_ptr);
		ip_ptr += 4;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_DURATION_MSN, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NEGOTIATED_BURST_NUM, *ip_ptr++);
		for (rep_cnt = 0; rep_cnt < 2; rep_cnt++) {
			if (ip_ptr - start_ptr < data_len && ip_ptr[0] == WLAN_EID_MEASURE_REPORT) {
				if (ip_ptr[4] == 8)  /*LCI Element*/
					res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_LCI,
						       ip_ptr[1] + 2, ip_ptr);
				else if (ip_ptr[4] == 11)   /*LCR element */
					res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_LCR,
						       ip_ptr[1] + 2, ip_ptr);
				ip_ptr += ip_ptr[1] + 2;
			}
		}
		nla_nest_end(nl_skb, nlattr_nested);
	}
	SLSI_DBG_HEX(sdev, SLSI_GSCAN, fapi_get_data(skb), fapi_get_datalen(skb), "range indication skb buffer:\n");
	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree_skb(nl_skb);
		goto exit;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
exit:
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

void slsi_rx_range_done_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 rtt_id = fapi_get_u16(skb, u.mlme_range_ind.rtt_id);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_COMPLETE_EVENT), SLSI_NL80211_RTT_COMPLETE_EVENT);
#endif
	slsi_vendor_event(sdev, SLSI_NL80211_RTT_COMPLETE_EVENT, &rtt_id, sizeof(rtt_id));
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static int slsi_rtt_cancel_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int temp, ret, r = 1, j = 0, type;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	u8 *addr;
	const struct nlattr *iter;
	u16  num_devices = 0, rtt_id = 0;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "RTT_SUBCMD_CANCEL_CONFIG\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_WARN(sdev, "RTT not supported.\n");
		return -ENOTSUPP;
	}
	nla_for_each_attr(iter, data, len, temp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_CNT:
			num_devices = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target cnt %d\n", num_devices);
			break;
		case SLSI_RTT_ATTRIBUTE_TARGET_ID:
			rtt_id = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target id %d\n", rtt_id);
			break;
		default:
			SLSI_ERR_NODEV("No ATTRIBUTE_Target cnt - %d\n", type);
			break;
		}
	}
	/* Allocate memory for the received mac addresses */
	if (num_devices) {
		addr = kzalloc(ETH_ALEN * num_devices, GFP_KERNEL);
		if (!addr) {
			SLSI_ERR_NODEV("Failed for allocate memory for mac addresses\n");
			ret = -ENOMEM;
			return ret;
		}
		nla_for_each_attr(iter, data, len, temp) {
			type = nla_type(iter);
			if (type == SLSI_RTT_ATTRIBUTE_TARGET_MAC) {
				memcpy(&addr[j], nla_data(iter), ETH_ALEN);
				j++;
			} else {
				SLSI_ERR_NODEV("No ATTRIBUTE_MAC - %d\n", type);
			}
		}

		r = slsi_mlme_del_range_req(sdev, dev, num_devices, addr, rtt_id);
		kfree(addr);
	}
	if (r)
		SLSI_ERR_NODEV("Failed to cancel rtt config\n");
	return r;
}

static int slsi_configure_nd_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      ret = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 nd_offload_enabled = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "Received nd_offload command\n");

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated || (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) ||
	    (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED)) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "vif error\n");
		ret = -EPERM;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_ND_OFFLOAD_VALUE:
		{
			nd_offload_enabled = nla_get_u8(attr);
			break;
		}
		default:
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ndev_vif->sta.nd_offload_enabled = nd_offload_enabled;
	ret = slsi_mlme_set_ipv6_address(sdev, dev);
	if (ret < 0) {
		SLSI_ERR(sdev, "Configure nd_offload failed ret:%d nd_offload_enabled: %d\n", ret, nd_offload_enabled);
		ret = -EINVAL;
		goto exit;
	}
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

static int slsi_get_roaming_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      ret = 0;
	struct slsi_mib_value *values = NULL;
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_ROAM_BLACKLIST_SIZE, { 0, 0 } } };
	u32    max_blacklist_size = 0;
	u32    max_whitelist_size = 0;
	struct sk_buff *nl_skb;
	struct nlattr *nlattr_start;

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		ret = -ENOMEM;
		goto exit;
	}
	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (values && (values[0].type == SLSI_MIB_TYPE_UINT ||  values[0].type == SLSI_MIB_TYPE_INT))
		max_blacklist_size = values[0].u.uintValue;
	nl_skb = cfg80211_vendor_cmd_alloc_reply_skb(sdev->wiphy, NLMSG_DEFAULT_SIZE);
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		ret = -ENOMEM;
		goto exit_with_mib_resp;
	}

	nlattr_start = nla_nest_start(nl_skb, NL80211_ATTR_VENDOR_DATA);
	if (!nlattr_start) {
		SLSI_ERR(sdev, "failed to put NL80211_ATTR_VENDOR_DATA\n");
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		ret = -EINVAL;
		goto exit_with_mib_resp;
	}

	ret = nla_put_u32(nl_skb, SLSI_NL_ATTR_MAX_BLACKLIST_SIZE, max_blacklist_size);
	ret |= nla_put_u32(nl_skb, SLSI_NL_ATTR_MAX_WHITELIST_SIZE, max_whitelist_size);
	if (ret) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", ret);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit_with_mib_resp;
	}

	ret = cfg80211_vendor_cmd_reply(nl_skb);
	if (ret)
		SLSI_ERR(sdev, "cfg80211_vendor_cmd_reply failed :%d\n", ret);
exit_with_mib_resp:
	kfree(mibrsp.data);
	kfree(values);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

static int slsi_set_roaming_state(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device   *dev = wdev->netdev;
	int                 temp = 0;
	int                 type = 0;
	const struct nlattr *attr;
	int                 ret = 0;
	int                 roam_state = 0;

	if (!dev) {
		SLSI_WARN_NODEV("net_dev is NULL\n");
		return -EINVAL;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTR_ROAM_STATE:
			roam_state = nla_get_u8(attr);
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_SET_ROAMING_STATE roam_state = %d\n", roam_state);
	ret = slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAMING_ENABLED, roam_state);
	if (ret < 0)
		SLSI_ERR_NODEV("Failed to set roaming state\n");

exit:
	return ret;
}

char *slsi_get_roam_reason_str(int roam_reason)
{
	switch (roam_reason) {
	case SLSI_WIFI_ROAMING_SEARCH_REASON_RESERVED:
		return "WIFI_ROAMING_SEARCH_REASON_RESERVED";
	case SLSI_WIFI_ROAMING_SEARCH_REASON_LOW_RSSI:
		return "WIFI_ROAMING_SEARCH_REASON_LOW_RSSI";
	case SLSI_WIFI_ROAMING_SEARCH_REASON_LINK_LOSS:
		return "WIFI_ROAMING_SEARCH_REASON_LINK_LOSS";
	case SLSI_WIFI_ROAMING_SEARCH_REASON_BTM_REQ:
		return "WIFI_ROAMING_SEARCH_REASON_BTM_REQ";
	case SLSI_WIFI_ROAMING_SEARCH_REASON_CU_TRIGGER:
		return "WIFI_ROAMING_SEARCH_REASON_CU_TRIGGER";
	default:
		return "UNKNOWN_REASON";
	}
}

void slsi_rx_event_log_indication(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 event_id = 0;
	u64 timestamp = 0;
	u8 *tlv_data;
	u32 roam_reason = 0, chan_utilisation = 0, btm_request_mode = 0, btm_response = 0, eapol_msg_type = 0;
	u32 deauth_reason = 0, eapol_retry_count = 0, roam_rssi, status_code = 0;
	u16 vendor_len, tag_id, tag_len, vtag_id, eapol_key_type = 0;
	u32 tag_value, vtag_value, rssi_bits = 0;
	int roam_rssi_val = 0;
	__le16               *le16_ptr = NULL;
	int tlv_buffer__len = fapi_get_datalen(skb), i = 0;

	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	event_id = fapi_get_s16(skb, u.mlme_event_log_ind.event);
	timestamp = fapi_get_u64(skb, u.mlme_event_log_ind.timestamp);
	tlv_data = fapi_get_data(skb);

	SLSI_DBG3(sdev, SLSI_GSCAN,
		  "slsi_rx_event_log_indication, event id = %d, len = %d\n", event_id, tlv_buffer__len);

#ifdef CONFIG_SCSC_WIFILOGGER
	SCSC_WLOG_FW_EVENT(WLOG_NORMAL, event_id, timestamp, fapi_get_data(skb), fapi_get_datalen(skb));
#endif
	while (i + 4 < tlv_buffer__len) {
		le16_ptr = (__le16 *)&tlv_data[i];
		tag_id = le16_to_cpu(*le16_ptr);
		le16_ptr = (__le16 *)&tlv_data[i + 2];
		tag_len = le16_to_cpu(*le16_ptr);
		i += 4;
		if (i + tag_len > tlv_buffer__len) {
			SLSI_INFO(sdev, "Incorrect fapi bulk data\n");
			slsi_kfree_skb(skb);
			SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
			return;
		}
		tag_value = slsi_convert_tlv_data_to_value(&tlv_data[i], tag_len);
		switch (tag_id) {
		case SLSI_WIFI_TAG_RSSI:
			roam_rssi = tag_value;
			while (roam_rssi) {
				rssi_bits++;
				roam_rssi >>= 1;
			}
			roam_rssi_val = ((1 << rssi_bits) - 1) ^ tag_value;
			roam_rssi_val = -(roam_rssi_val + 1);
			break;
		case SLSI_WIFI_TAG_REASON_CODE:
			deauth_reason = tag_value;
			break;
		case SLSI_WIFI_TAG_VENDOR_SPECIFIC:
			vendor_len = tag_len - 2;
			le16_ptr = (__le16 *)&tlv_data[i];
			vtag_id = le16_to_cpu(*le16_ptr);
			vtag_value = slsi_convert_tlv_data_to_value(&tlv_data[i + 2], vendor_len);
			switch (vtag_id) {
			case SLSI_WIFI_TAG_VD_CHANNEL_UTILISATION:
				chan_utilisation = vtag_value;
				break;
			case SLSI_WIFI_TAG_VD_ROAMING_REASON:
				roam_reason = vtag_value;
				break;
			case SLSI_WIFI_TAG_VD_BTM_REQUEST_MODE:
				btm_request_mode = vtag_value;
				break;
			case SLSI_WIFI_TAG_VD_BTM_RESPONSE_STATUS:
				btm_response = vtag_value;
				break;
			case SLSI_WIFI_TAG_VD_RETRY_COUNT:
				eapol_retry_count = vtag_value;
				break;
			case SLSI_WIFI_TAG_VD_EAPOL_KEY_TYPE:
				eapol_key_type = vtag_value;
				break;
			}
			break;
		case SLSI_WIFI_TAG_EAPOL_MESSAGE_TYPE:
			eapol_msg_type = tag_value;
			break;
		case SLSI_WIFI_TAG_STATUS:
			status_code = tag_value;
			break;
		}
		i += tag_len;
	}
	switch (event_id) {
	case FAPI_EVENT_WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_START:
		if (eapol_key_type == SLSI_WIFI_EAPOL_KEY_TYPE_GTK)
			SLSI_INFO(sdev, "WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_START, Send GTK, G%d\n", eapol_msg_type);
		else if (eapol_key_type == SLSI_WIFI_EAPOL_KEY_TYPE_PTK)
			SLSI_INFO(sdev, "WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_START, Send 4way-H/S, M%d\n",
				  eapol_msg_type);
		break;
	case FAPI_EVENT_WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_STOP:
		SLSI_INFO(sdev, "WIFI_EVENT_FW_EAPOL_FRAME_TRANSMIT_STOP,Result Code:%d, Retry Count:%d\n",
			  status_code, eapol_retry_count);
		break;
	case FAPI_EVENT_WIFI_EVENT_FW_EAPOL_FRAME_RECEIVED:
		if (eapol_key_type == SLSI_WIFI_EAPOL_KEY_TYPE_GTK)
			SLSI_INFO(sdev, "WIFI_EVENT_FW_EAPOL_FRAME_RECEIVED, Received GTK, G%d\n", eapol_msg_type);
		else if (eapol_key_type == SLSI_WIFI_EAPOL_KEY_TYPE_PTK)
			SLSI_INFO(sdev, "WIFI_EVENT_FW_EAPOL_FRAME_RECEIVED, Received 4way-H/S, M%d\n", eapol_msg_type);
		break;
	case WIFI_EVENT_FW_BTM_FRAME_REQUEST:
		SLSI_INFO(sdev, "WIFI_EVENT_FW_BTM_FRAME_REQUEST,Request Mode:%d\n", btm_request_mode);
		break;
	case WIFI_EVENT_FW_BTM_FRAME_RESPONSE:
		SLSI_INFO(sdev, "WIFI_EVENT_FW_BTM_FRAME_RESPONSE,Status code:%d\n", btm_response);
		break;
	case FAPI_EVENT_WIFI_EVENT_ROAM_SEARCH_STARTED:
		SLSI_INFO(sdev, "WIFI_EVENT_ROAM_SEARCH_STARTED, RSSI:%d, Deauth Reason:0x%04x, Channel Utilisation:%d,"
			  "Roam Reason: %s\n", roam_rssi_val, deauth_reason, chan_utilisation,
			  slsi_get_roam_reason_str(roam_reason));
		break;
	case FAPI_EVENT_WIFI_EVENT_FW_AUTH_STARTED:
		SLSI_INFO(sdev, "WIFI_EVENT_FW_AUTH_STARTED\n");
		break;
	case FAPI_EVENT_WIFI_EVENT_AUTH_COMPLETE:
		SLSI_INFO(sdev, "WIFI_EVENT_AUTH_COMPLETE,Status code:%d\n", status_code);
		break;
	case FAPI_EVENT_WIFI_EVENT_ROAM_ASSOC_COMPLETE:
		SLSI_INFO(sdev, "Received Association Response\n");
		break;
	case WIFI_EVENT_FW_NR_FRAME_REQUEST:
		SLSI_INFO(sdev, "Send Radio Measurement Frame (Neighbor Report Req)\n");
		break;
	case WIFI_EVENT_FW_RM_FRAME_RESPONSE:
		SLSI_INFO(sdev, "Received Radio Measurement Frame (Radio Measurement Rep)\n");
		break;
	}

	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
}

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
static void slsi_on_ring_buffer_data(char *ring_name, char *buffer, int buffer_size,
				     struct scsc_wifi_ring_buffer_status *buffer_status, void *ctx)
{
	struct sk_buff *skb;
	int event_id = SLSI_NL80211_LOGGER_RING_EVENT;
	struct slsi_dev *sdev = ctx;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, buffer_size, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, buffer_size, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		return;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_STATUS, sizeof(*buffer_status), buffer_status) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_DATA, buffer_size, buffer)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		return;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
}

static void slsi_on_alert(char *buffer, int buffer_size, int err_code, void *ctx)
{
	struct sk_buff *skb;
	int event_id = SLSI_NL80211_LOGGER_FW_DUMP_EVENT;
	struct slsi_dev *sdev = ctx;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, buffer_size, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, buffer_size, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN, buffer_size) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_DATA, buffer_size, buffer)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		goto exit;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
}

static void slsi_on_firmware_memory_dump(char *buffer, int buffer_size, void *ctx)
{
	SLSI_ERR_NODEV("slsi_on_firmware_memory_dump\n");
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	mem_dump_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!mem_dump_buffer) {
		SLSI_ERR_NODEV("Failed to allocate memory for mem_dump_buffer\n");
		return;
	}
	mem_dump_buffer_size = buffer_size;
	memcpy(mem_dump_buffer, buffer, mem_dump_buffer_size);
}

static void slsi_on_driver_memory_dump(char *buffer, int buffer_size, void *ctx)
{
	SLSI_ERR_NODEV("slsi_on_driver_memory_dump\n");
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	mem_dump_buffer_size = buffer_size;
	mem_dump_buffer = kmalloc(mem_dump_buffer_size, GFP_KERNEL);
	if (!mem_dump_buffer) {
		SLSI_ERR_NODEV("Failed to allocate memory for mem_dump_buffer\n");
		return;
	}
	memcpy(mem_dump_buffer, buffer, mem_dump_buffer_size);
}

static int slsi_enable_logging(struct slsi_dev *sdev, bool enable)
{
	int                  status = 0;
#ifdef ENABLE_WIFI_LOGGER_MIB_WRITE
	struct slsi_mib_data mib_data = { 0, NULL };

	SLSI_DBG3(sdev, SLSI_GSCAN, "Value of enable is : %d\n", enable);
	status = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_LOGGER_ENABLED, enable, 0);
	if (status != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "slsi_enable_logging failed: no mem for MIB\n");
		status = -ENOMEM;
		goto exit;
	}
	status = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
	kfree(mib_data.data);
	if (status)
		SLSI_ERR(sdev, "Err setting unifiLoggerEnabled MIB. error = %d\n", status);

exit:
	return status;
#else
	SLSI_DBG3(sdev, SLSI_GSCAN, "UnifiLoggerEnabled MIB write disabled\n");
	return status;
#endif
}

static int slsi_start_logging(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	char                ring_name[32] = {0};
	int                 verbose_level = 0;
	int                 ring_flags = 0;
	int                 max_interval_sec = 0;
	int                 min_data_size = 0;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		if (!attr) {
			ret = -EINVAL;
			goto exit;
		}

		type = nla_type(attr);

		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NAME:
			strncpy(ring_name, nla_data(attr), MIN(sizeof(ring_name) - 1, nla_len(attr)));
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_VERBOSE_LEVEL:
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN)) {
				ret = -EINVAL;
				goto exit;
			}
			verbose_level = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_FLAGS:
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN)) {
				ret = -EINVAL;
				goto exit;
			}
			ring_flags = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_LOG_MAX_INTERVAL:
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN)) {
				ret = -EINVAL;
				goto exit;
			}
			max_interval_sec = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_LOG_MIN_DATA_SIZE:
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U32_LEN - NLA_HDRLEN)) {
				ret = -EINVAL;
				goto exit;
			}
			min_data_size = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}
	ret = scsc_wifi_set_log_handler(slsi_on_ring_buffer_data, sdev);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_set_log_handler failed ret: %d\n", ret);
		goto exit;
	}
	ret = scsc_wifi_set_alert_handler(slsi_on_alert, sdev);
	if (ret < 0) {
		SLSI_ERR(sdev, "Warning : scsc_wifi_set_alert_handler failed ret: %d\n", ret);
	}
	ret = slsi_enable_logging(sdev, 1);
	if (ret < 0) {
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 1, failed ret: %d\n", ret);
		goto exit_with_reset_alert_handler;
	}
	ret = scsc_wifi_start_logging(verbose_level, ring_flags, max_interval_sec, min_data_size, ring_name);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_start_logging failed ret: %d\n", ret);
		goto exit_with_disable_logging;
	} else {
		goto exit;
	}
exit_with_disable_logging:
	ret = slsi_enable_logging(sdev, 0);
	if (ret < 0)
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 0, failed ret: %d\n", ret);
exit_with_reset_alert_handler:
	ret = scsc_wifi_reset_alert_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "Warning : scsc_wifi_reset_alert_handler failed ret: %d\n", ret);
	ret = scsc_wifi_reset_log_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "scsc_wifi_reset_log_handler failed ret: %d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_reset_logging(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = slsi_enable_logging(sdev, 0);
	if (ret < 0)
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 0, failed ret: %d\n", ret);
	ret = scsc_wifi_reset_log_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "scsc_wifi_reset_log_handler failed ret: %d\n", ret);
	ret = scsc_wifi_reset_alert_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "Warning : scsc_wifi_reset_alert_handler failed ret: %d\n", ret);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_trigger_fw_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	struct sk_buff  *skb = NULL;
	int             length = 100;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);

	ret = scsc_wifi_get_firmware_memory_dump(slsi_on_firmware_memory_dump, sdev);
	if (ret) {
		SLSI_ERR(sdev, "scsc_wifi_get_firmware_memory_dump failed : %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, length);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN, mem_dump_buffer_size)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);

exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_fw_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buf_len = 0;
	void __user         *user_buf = NULL;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN:
			buf_len = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
			return -EINVAL;
		}
	}
	if (buf_len > 0 && user_buf) {
		ret = copy_to_user(user_buf, mem_dump_buffer, buf_len);
		if (ret) {
			SLSI_ERR(sdev, "failed to copy memdump into user buffer : %d\n", ret);
			goto exit;
		}

		/* Alloc the SKB for vendor_event */
		skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 100);
		if (!skb) {
			SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
			ret = -ENOMEM;
			goto exit;
		}

		/* Indicate the memdump is successfully copied */
		if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_DATA, sizeof(ret), &ret)) {
			SLSI_ERR_NODEV("Failed nla_put\n");
			slsi_kfree_skb(skb);
			ret = -EINVAL;
			goto exit;
		}

		ret = cfg80211_vendor_cmd_reply(skb);

		if (ret)
			SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
	}

exit:
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_trigger_driver_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	struct sk_buff  *skb = NULL;
	int             length = 100;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);

	ret = scsc_wifi_get_driver_memory_dump(slsi_on_driver_memory_dump, sdev);
	if (ret) {
		SLSI_ERR(sdev, "scsc_wifi_get_driver_memory_dump failed : %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, length);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_LEN, mem_dump_buffer_size)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);

exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_driver_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buf_len = 0;
	void __user         *user_buf = NULL;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_LEN:
			buf_len = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
			return -EINVAL;
		}
	}
	if (buf_len > 0 && user_buf) {
		ret = copy_to_user(user_buf, mem_dump_buffer, buf_len);
		if (ret) {
			SLSI_ERR(sdev, "failed to copy memdump into user buffer : %d\n", ret);
			goto exit;
		}

		/* Alloc the SKB for vendor_event */
		skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 100);
		if (!skb) {
			SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
			ret = -ENOMEM;
			goto exit;
		}

		/* Indicate the memdump is successfully copied */
		if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_DATA, sizeof(ret), &ret)) {
			SLSI_ERR_NODEV("Failed nla_put\n");
			slsi_kfree_skb(skb);
			ret = -EINVAL;
			goto exit;
		}

		ret = cfg80211_vendor_cmd_reply(skb);

		if (ret)
			SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
	}

exit:
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_version(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buffer_size = 1024;
	bool                log_version = false;
	char                *buffer;
	const struct nlattr *attr;

	buffer = kzalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		SLSI_ERR(sdev, "No mem. Size:%d\n", buffer_size);
		return -ENOMEM;
	}
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_VERSION:
			log_version = true;
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_VERSION:
			log_version = false;
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (log_version)
		ret = scsc_wifi_get_driver_version(buffer, buffer_size);
	else
		ret = scsc_wifi_get_firmware_version(buffer, buffer_size);

	if (ret < 0) {
		SLSI_ERR(sdev, "failed to get the version %d\n", ret);
		goto exit;
	}

	ret = slsi_vendor_cmd_reply(wiphy, buffer, strlen(buffer));
exit:
	kfree(buffer);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_ring_buffers_status(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev                     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                                 ret = 0;
	int                                 num_rings = 10;
	struct sk_buff                      *skb;
	struct scsc_wifi_ring_buffer_status status[num_rings];

	SLSI_DBG1(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	memset(status, 0, sizeof(struct scsc_wifi_ring_buffer_status) * num_rings);
	ret = scsc_wifi_get_ring_buffers_status(&num_rings, status);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_ring_buffers_status failed ret:%d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 700);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* Indicate that the ring count and ring buffers status is successfully copied */
	if (nla_put_u8(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NUM, num_rings) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_STATUS, sizeof(status[0]) * num_rings, status)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_ring_data(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	char                ring_name[32] = {0};
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		if (!attr) {
			ret = -EINVAL;
			goto exit;
		}
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NAME:
			strncpy(ring_name, nla_data(attr), MIN(sizeof(ring_name) - 1, nla_len(attr)));
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			goto exit;
		}
	}

	ret = scsc_wifi_get_ring_data(ring_name);
	if (ret < 0)
		SLSI_ERR(sdev, "trigger_get_data failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_logger_supported_feature_set(struct wiphy *wiphy, struct wireless_dev *wdev,
						 const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	u32             supported_features = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = scsc_wifi_get_logger_supported_feature_set(&supported_features);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_logger_supported_feature_set failed ret:%d\n", ret);
		goto exit;
	}
	ret = slsi_vendor_cmd_reply(wiphy, &supported_features, sizeof(supported_features));
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_start_pkt_fate_monitoring(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int                  ret = 0;
#ifdef ENABLE_WIFI_LOGGER_MIB_WRITE
	struct slsi_dev      *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_mib_data mib_data = { 0, NULL };

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_TX_DATA_CONFIRM, 1, 0);
	if (ret != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "Failed to set UnifiTxDataConfirm MIB : no mem for MIB\n");
		ret = -ENOMEM;
		goto exit;
	}

	ret = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);

	if (ret) {
		SLSI_ERR(sdev, "Err setting UnifiTxDataConfirm MIB. error = %d\n", ret);
		goto exit;
	}

	ret = scsc_wifi_start_pkt_fate_monitoring();
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_start_pkt_fate_monitoring failed, ret=%d\n", ret);

		// Resetting the SLSI_PSID_UNIFI_TX_DATA_CONFIRM mib back to 0.
		mib_data.dataLength = 0;
		mib_data.data = NULL;
		ret = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_TX_DATA_CONFIRM, 1, 0);
		if (ret != SLSI_MIB_STATUS_SUCCESS) {
			SLSI_ERR(sdev, "Failed to set UnifiTxDataConfirm MIB : no mem for MIB\n");
			ret = -ENOMEM;
			goto exit;
		}

		ret = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);

		if (ret) {
			SLSI_ERR(sdev, "Err setting UnifiTxDataConfirm MIB. error = %d\n", ret);
			goto exit;
		}
	}
exit:
	kfree(mib_data.data);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
#else
	SLSI_ERR_NODEV("slsi_start_pkt_fate_monitoring : UnifiTxDataConfirm MIB write disabled\n");
	return ret;
#endif
}

static int slsi_get_tx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	void __user         *user_buf = NULL;
	u32                 req_count = 0;
	size_t              provided_count = 0;
	struct sk_buff      *skb;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM:
			req_count = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = scsc_wifi_get_tx_pkt_fates(user_buf, req_count, &provided_count);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_tx_pkt_fates failed ret: %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 200);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM, sizeof(provided_count), &provided_count)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_rx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	void __user         *user_buf = NULL;
	u32                 req_count = 0;
	size_t              provided_count = 0;
	struct sk_buff      *skb;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM:
			req_count = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = scsc_wifi_get_rx_pkt_fates(user_buf, req_count, &provided_count);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_rx_pkt_fates failed ret: %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 200);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM, sizeof(provided_count), &provided_count)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_wake_reason_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_wlan_driver_wake_reason_cnt wake_reason_count;
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	// Initialising the wake_reason_count structure values to 0.
	memset(&wake_reason_count, 0, sizeof(struct slsi_wlan_driver_wake_reason_cnt));

	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_CMD_EVENT_WAKE_CNT_SZ:
			wake_reason_count.cmd_event_wake_cnt_sz = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_SZ:
			wake_reason_count.driver_fw_local_wake_cnt_sz = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (ret < 0) {
		SLSI_ERR(sdev, "Failed to get wake reason stats :  %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 700);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_CMD_EVENT_WAKE,
			wake_reason_count.total_cmd_event_wake)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_CMD_EVENT_WAKE_CNT_PTR, 0,
		    wake_reason_count.cmd_event_wake_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_DRIVER_FW_LOCAL_WAKE,
			wake_reason_count.total_driver_fw_local_wake)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_PTR, 0,
		    wake_reason_count.driver_fw_local_wake_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_RX_DATA_WAKE,
			wake_reason_count.total_rx_data_wake) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_UNICAST_CNT,
			wake_reason_count.rx_wake_details.rx_unicast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_MULTICAST_CNT,
			wake_reason_count.rx_wake_details.rx_multicast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_BROADCAST_CNT,
			wake_reason_count.rx_wake_details.rx_broadcast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP_PKT,
			wake_reason_count.rx_wake_pkt_classification_info.icmp_pkt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_PKT,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_pkt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_RA,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_ra) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_NA,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_na) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_NS,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_ns) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP4_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.ipv4_rx_multicast_addr_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.ipv6_rx_multicast_addr_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_OTHER_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.other_rx_multicast_addr_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

#endif /* CONFIG_SCSC_WLAN_ENHANCED_LOGGING */

static int slsi_acs_validate_width_hw_mode(struct slsi_acs_request *request)
{
	if (request->hw_mode != SLSI_ACS_MODE_IEEE80211A && request->hw_mode != SLSI_ACS_MODE_IEEE80211B &&
	    request->hw_mode != SLSI_ACS_MODE_IEEE80211G)
		return -EINVAL;
	if (request->ch_width != 20 && request->ch_width != 40 && request->ch_width != 80)
		return -EINVAL;
	return 0;
}

static int slsi_acs_init(struct wiphy *wiphy,
			 struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif  *ndev_vif;
	struct slsi_acs_request *request;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	int r = 0;
	u32 *freq_list = NULL;
	int freq_list_len = 0;

	SLSI_INFO(sdev, "SUBCMD_ACS_INIT Received\n");
	if (slsi_is_test_mode_enabled()) {
		SLSI_ERR(sdev, "Not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}
	if (wdev->iftype != NL80211_IFTYPE_AP) {
		SLSI_ERR(sdev, "Invalid iftype: %d\n", wdev->iftype);
		return -EINVAL;
	}
	if (!dev) {
		SLSI_ERR(sdev, "Dev not found!\n");
		return -ENODEV;
	}
	request = kcalloc(1, sizeof(*request), GFP_KERNEL);
	if (!request) {
		SLSI_ERR(sdev, "No memory for request!");
		return -ENOMEM;
	}
	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		if (!attr) {
			kfree(request);
			r = -EINVAL;
			goto exit;
		}

		type = nla_type(attr);

		switch (type) {
		case SLSI_ACS_ATTR_HW_MODE:
		{
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U8_LEN - NLA_HDRLEN)) {
				kfree(request);
				r = -EINVAL;
				goto exit;
			}
			request->hw_mode = nla_get_u8(attr);
			SLSI_INFO(sdev, "ACS hw mode: %d\n", request->hw_mode);
			break;
		}
		case SLSI_ACS_ATTR_CHWIDTH:
		{
			if (nla_len(attr) != (SLSI_NL_ATTRIBUTE_U16_LEN - NLA_HDRLEN)) {
				kfree(request);
				r = -EINVAL;
				goto exit;
			}
			request->ch_width = nla_get_u16(attr);
			SLSI_INFO(sdev, "ACS ch_width: %d\n", request->ch_width);
			break;
		}
		case SLSI_ACS_ATTR_FREQ_LIST:
		{
			if (freq_list) /* This check is to avoid Prevent Issue */
				break;

			freq_list =  kmalloc(nla_len(attr), GFP_KERNEL);
			if (!freq_list) {
				SLSI_ERR(sdev, "No memory for frequency list!");
				kfree(request);
				SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
				return -ENOMEM;
			}
			memcpy(freq_list, nla_data(attr), nla_len(attr));
			freq_list_len = nla_len(attr) / sizeof(u32);
			SLSI_INFO(sdev, "ACS freq_list_len: %d\n", freq_list_len);
			break;
		}
		default:
			if (type > SLSI_ACS_ATTR_MAX)
				SLSI_ERR(sdev, "Invalid type : %d\n", type);
			break;
		}
	}

	r = slsi_acs_validate_width_hw_mode(request);
	if (r == 0 && freq_list_len) {
		struct ieee80211_channel *channels[freq_list_len];
		struct slsi_acs_chan_info ch_info[MAX_CHAN_VALUE_ACS];
		struct slsi_acs_selected_channels acs_selected_channels;
		int i = 0, num_channels = 0;
		int idx;
		u32 chan_flags = (IEEE80211_CHAN_INDOOR_ONLY | IEEE80211_CHAN_RADAR |
					      IEEE80211_CHAN_DISABLED |
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 13)
					      IEEE80211_CHAN_PASSIVE_SCAN
#else
					      IEEE80211_CHAN_NO_IR
#endif
					     );

		memset(channels, 0, sizeof(channels));
		memset(&ch_info, 0, sizeof(ch_info));
		for (i = 0; i < freq_list_len; i++) {
			channels[num_channels] = ieee80211_get_channel(wiphy, freq_list[i]);
			if (!channels[num_channels]) {
				SLSI_INFO(sdev, "Ignore invalid freq:%d in freq list\n", freq_list[i]);
			} else if (channels[num_channels]->flags & chan_flags) {
				SLSI_INFO(sdev, "Skip invalid channel:%d for ACS\n", channels[num_channels]->hw_value);
			} else {
				idx = slsi_find_chan_idx(channels[num_channels]->hw_value, request->hw_mode);
				ch_info[idx].chan = channels[num_channels]->hw_value;
				num_channels++;
			}
		}

		if (num_channels == 1) {
			memset(&acs_selected_channels, 0, sizeof(acs_selected_channels));
			acs_selected_channels.ch_width = 20;
			acs_selected_channels.hw_mode = request->hw_mode;
			acs_selected_channels.pri_channel = channels[0]->hw_value;
			r = slsi_send_acs_event(sdev, acs_selected_channels);
			sdev->acs_channel_switched = true;
			kfree(freq_list);
			kfree(request);
			SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
			return r;
		}

		if (request->hw_mode == SLSI_ACS_MODE_IEEE80211A)
			request->ch_list_len = 25;
		else
			request->ch_list_len = 14;
		memcpy(&request->acs_chan_info[0], &ch_info[0], sizeof(ch_info));
		ndev_vif->scan[SLSI_SCAN_HW_ID].acs_request = request;
		ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = false;
		r = slsi_mlme_add_scan(sdev,
				       dev,
				       FAPI_SCANTYPE_AP_AUTO_CHANNEL_SELECTION,
				       FAPI_REPORTMODE_REAL_TIME,
				       0,    /* n_ssids */
				       NULL, /* ssids */
				       num_channels,
				       channels,
				       NULL,
				       NULL,                   /* ie */
				       0,                      /* ie_len */
				       ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan);
	} else {
		SLSI_ERR(sdev, "Invalid freq_list len:%d or ch_width:%d or hw_mode:%d\n", freq_list_len,
			 request->ch_width, request->hw_mode);
		r = -EINVAL;
		kfree(request);
	}
exit:
	kfree(freq_list);
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return r;
}

static const struct  nl80211_vendor_cmd_info slsi_vendor_events[] = {
	/**********Deprecated now due to fapi updates.Do not remove*/
	{ OUI_GOOGLE, SLSI_NL80211_SIGNIFICANT_CHANGE_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_HOTLIST_AP_FOUND_EVENT },
	/******************************************/
	{ OUI_GOOGLE, SLSI_NL80211_SCAN_RESULTS_AVAILABLE_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_FULL_SCAN_RESULT_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_SCAN_EVENT },
	/**********Deprecated now due to fapi updates.Do not remove*/
	{ OUI_GOOGLE, SLSI_NL80211_HOTLIST_AP_LOST_EVENT },
	/******************************************/
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH },
#endif
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_HANGED_EVENT },
	{ OUI_GOOGLE,  SLSI_NL80211_EPNO_EVENT },
	{ OUI_GOOGLE,  SLSI_NL80211_HOTSPOT_MATCH },
	{ OUI_GOOGLE,  SLSI_NL80211_RSSI_REPORT_EVENT},
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	{ OUI_GOOGLE,  SLSI_NL80211_LOGGER_RING_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_LOGGER_FW_DUMP_EVENT},
#endif
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_RESPONSE_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_MATCH_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_FOLLOWUP_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_DISABLED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_RTT_RESULT_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_RTT_COMPLETE_EVENT},
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_ACS_EVENT},
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_FORWARD_BEACON},
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_FORWARD_BEACON_ABORT}
};

static const struct wiphy_vendor_command     slsi_vendor_cmd[] = {
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_VALID_CHANNELS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_valid_channel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_ADD_GSCAN
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_add
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_DEL_GSCAN
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_del
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_SCAN_RESULTS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_scan_results
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_GSCAN_OUI
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_set_oui
	},
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	{
		{
			.vendor_id = OUI_SAMSUNG,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_SET_KEY
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_key_mgmt_set_pmk
	},
#endif
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_BSSID_BLACKLIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_bssid_blacklist
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_KEEP_ALIVE_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_keepalive_offload
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_STOP_KEEP_ALIVE_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_stop_keepalive_offload
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_EPNO_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_epno_ssid
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_HS_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_hs_params
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_HS_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_reset_hs_params
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_RSSI_MONITOR
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_rssi_monitor
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_SET_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_set_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_GET_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_get_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_CLEAR_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_clear_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FEATURE_SET
		},
		.flags = 0,
		.doit = slsi_get_feature_set
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_COUNTRY_CODE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_country_code
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_CONFIGURE_ND_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_configure_nd_offload
	},
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_logging
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_reset_logging
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_TRIGGER_FW_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_trigger_fw_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FW_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_fw_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_TRIGGER_DRIVER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_trigger_driver_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_DRIVER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_driver_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_VERSION
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_version
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RING_STATUS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_ring_buffers_status
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RING_DATA
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_ring_data
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FEATURE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_logger_supported_feature_set
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_PKT_FATE_MONITORING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_pkt_fate_monitoring
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_TX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_tx_pkt_fates
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_rx_pkt_fates
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_wake_reason_stats
	},
#endif /* CONFIG_SCSC_WLAN_ENHANCED_LOGGING */
#ifdef CONFIG_SCSC_WIFI_NAN_ENABLE
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_ENABLE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_enable
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_DISABLE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_disable
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_PUBLISH
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_publish
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_PUBLISHCANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_publish_cancel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_SUBSCRIBE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_subscribe
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_SUBSCRIBECANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_subscribe_cancel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_TXFOLLOWUP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_transmit_followup
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_set_config
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_get_capabilities
	},
#endif
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_ROAMING_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_roaming_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_ROAMING_STATE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_roaming_state
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_get_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_RANGE_START
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_set_config
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_RANGE_CANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_cancel_config
	},
	{
		{
			.vendor_id = OUI_SAMSUNG,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_ACS_INIT
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_acs_init
	},
};

void slsi_nl80211_vendor_deinit(struct slsi_dev *sdev)
{
	SLSI_DBG2(sdev, SLSI_GSCAN, "De-initialise vendor command and events\n");
	sdev->wiphy->vendor_commands = NULL;
	sdev->wiphy->n_vendor_commands = 0;
	sdev->wiphy->vendor_events = NULL;
	sdev->wiphy->n_vendor_events = 0;

	SLSI_DBG2(sdev, SLSI_GSCAN, "Gscan cleanup\n");
	slsi_gscan_flush_scan_results(sdev);

}

void slsi_nl80211_vendor_init(struct slsi_dev *sdev)
{
	int i;

	SLSI_DBG2(sdev, SLSI_GSCAN, "Init vendor command and events\n");

	sdev->wiphy->vendor_commands = slsi_vendor_cmd;
	sdev->wiphy->n_vendor_commands = ARRAY_SIZE(slsi_vendor_cmd);
	sdev->wiphy->vendor_events = slsi_vendor_events;
	sdev->wiphy->n_vendor_events = ARRAY_SIZE(slsi_vendor_events);

	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		sdev->bucket[i].scan_id = (SLSI_GSCAN_SCAN_ID_START + i);

	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		sdev->gscan_hash_table[i] = NULL;

	INIT_LIST_HEAD(&sdev->hotlist_results);
}
