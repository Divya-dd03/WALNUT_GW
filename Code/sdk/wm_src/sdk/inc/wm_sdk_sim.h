/**
 ******************************************************************************
 * @file    wm_sdk_sim.h
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

#ifndef __WM_SDK_SIM_H__
#define __WM_SDK_SIM_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Get SIM presence/ready status.
 * @param  status  [out] SIM state (present/absent/ready/error). Left unchanged
 *                 when WM_SDK_RESULT_BUSY is returned.
 * @return wm_SdkResult - 0 success; WM_SDK_RESULT_BUSY if the modem AT channel was
 *                     held by another operation (e.g. an SMS send) so the SIM
 *                     could not be queried - this is NOT a SIM removal and must
 *                     not be treated as one; negative on failure.
 */
wm_SdkResult wm_sdk_sim_get_status(wm_SdkSimStatus *status);

/**
 * @brief  Get SIM PIN lock state.
 * @param  pin_status  [out] pin-status code (0 = ready, no PIN).
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_sim_get_pin_status(UINT8 *pin_status);

/**
 * @brief  Read the SIM ICCID (unique SIM serial number).
 * @param  iccid       [out] buffer for the ICCID string.
 * @param  iccid_size  size of the buffer in bytes.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_sim_get_iccid(char *iccid, UINT32 iccid_size);

/**
 * @brief  Configure SIM hot-swap (insert/remove) detection; events async.
 * @param  command  hot-swap command/config selector.
 * @param  value    command value.
 * @param  msgq     message queue that will receive hot-swap events.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_sim_hotswap_msg(UINT32 command, UINT32 value, void *msgq);

/**
 * @brief  Poll the hot-swap message queue for SIM insert/remove events.
 * @param  msgq  the message queue to poll.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_sim_hotswap_poll(void *msgq);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_SIM_H__ */
