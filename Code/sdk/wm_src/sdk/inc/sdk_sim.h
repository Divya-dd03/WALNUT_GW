/**
 ******************************************************************************
 * @file    sdk_sim.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - SIM API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_SIM_H__
#define __SDK_SIM_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Get SIM presence/ready status.
 * @param  status  [out] SIM state (present/absent/ready/error).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sim_get_status(SdkSimStatus *status);

/**
 * @brief  Get SIM PIN lock state.
 * @param  pin_status  [out] pin-status code (0 = ready, no PIN).
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sim_get_pin_status(UINT8 *pin_status);

/**
 * @brief  Read the SIM ICCID (unique SIM serial number).
 * @param  iccid       [out] buffer for the ICCID string.
 * @param  iccid_size  size of the buffer in bytes.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sim_get_iccid(char *iccid, UINT32 iccid_size);

/**
 * @brief  Configure SIM hot-swap (insert/remove) detection; events async.
 * @param  command  hot-swap command/config selector.
 * @param  value    command value.
 * @param  msgq     message queue that will receive hot-swap events.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sim_hotswap_msg(UINT32 command, UINT32 value, void *msgq);

/**
 * @brief  Poll the hot-swap message queue for SIM insert/remove events.
 * @param  msgq  the message queue to poll.
 * @return SdkResult - 0 success; negative on failure.
 */
SdkResult sdk_sim_hotswap_poll(void *msgq);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_SIM_H__ */
