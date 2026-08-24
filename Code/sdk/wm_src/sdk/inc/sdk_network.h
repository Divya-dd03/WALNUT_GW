/**
 ******************************************************************************
 * @file    sdk_network.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - NETWORK API (radio, registration, PDP, RTC).
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_NETWORK_H__
#define __SDK_NETWORK_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Set the modem radio functionality level (AT+CFUN).
 * @param  cfun  0=minimum, 1=full, 4=airplane/RF-off.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_set_cfun(UINT32 cfun);

/**
 * @brief  Read the current CFUN level.
 * @param  cfun  [out] receives the current level.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_cfun(UINT32 *cfun);

/**
 * @brief  Get circuit-switched (voice) registration status (AT+CREG).
 * @param  status  [out] registration state.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_creg(SdkNetRegStatus *status);

/**
 * @brief  Get GPRS/packet-switched registration status (AT+CGREG).
 * @param  status  [out] PS registration state.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_cgreg(SdkNetRegStatus *status);

/**
 * @brief  Get packet-domain attach status (AT+CGATT).
 * @param  status  [out] attached / detached.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_cgatt(SdkNetAttStatus *status);

/**
 * @brief  Define a PDP context (APN) used for the data session (AT+CGDCONT).
 * @param  cid       context id (e.g. 1).
 * @param  apn_type  PDP type string, e.g. "IP" / "IPV4V6".
 * @param  apn       access point name (e.g. "wheelseye.com").
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_set_pdp_context(UINT32 cid, const char *apn_type, const char *apn);

/**
 * @brief  Activate or deactivate a PDP context (AT+CGACT).
 * @param  activate  1=activate, 0=deactivate.
 * @param  cid       context id to (de)activate.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_activate_pdp(UINT32 activate, UINT32 cid);

/**
 * @brief  Get the IP address assigned to a PDP context (AT+CGPADDR).
 * @param  cid      context id.
 * @param  ip_addr  [out] assigned IP address.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_ip_address(UINT32 cid, SdkIpAddress *ip_addr);

/**
 * @brief  Query overall network status.
 * @return SdkResult - 0 = up/registered; negative otherwise.
 */
SdkResult sdk_network_get_network_status(void);

/**
 * @brief  Get SIM PIN/lock status via the network stack (AT+CPIN).
 * @param  pin_status  [out] pin-status code (0 = ready).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_sim_pin_status(UINT8 *pin_status);

/**
 * @brief  Get radio info for the GPS packet (signal + serving cell).
 * @param  info  [out] CSQ, RSRP/RSRQ, MCC, MNC, LAC, cell id, valid flag.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_gps_radio_info(SdkNetworkGpsRadioInfo *info);

/**
 * @brief  Get network-provided time (NITZ).
 * @param  time  [out] network date/time.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_time(SdkNetworkTime *time);

/**
 * @brief  Set the modem real-time clock.
 * @param  time  time value to write.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_set_rtc(const SdkNetworkTime *time);

/**
 * @brief  Read the modem RTC in local time.
 * @param  time  [out] local time.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_rtc_get_local_time(SdkNetworkTime *time);

/**
 * @brief  Read the modem RTC in UTC.
 * @param  time  [out] UTC time.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_rtc_get_utc_time(SdkNetworkTime *time);

/**
 * @brief  Get automatic time-zone-update mode (AT+CTZU).
 * @param  ctzu  [out] 0=off, 1=on.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_get_ctzu(UINT32 *ctzu);

/**
 * @brief  Enable/disable automatic time-zone update (AT+CTZU).
 * @param  enable  1=enable, 0=disable.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_network_set_ctzu(UINT32 enable);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_NETWORK_H__ */
