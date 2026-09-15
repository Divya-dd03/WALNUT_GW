/**
 ******************************************************************************
 * @file    wm_sdk_urc.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - URC (unsolicited result code) API.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __WM_SDK_URC_H__
#define __WM_SDK_URC_H__

#include "wm_sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief  Register a message queue to receive unsolicited result codes (URCs).
 *         The queue receives the urcEvent_e code (as UINT32) of each event
 *         selected by the mask.
 * @param  msgq  queue to receive URCs (created via wm_sdk_msgq_create).
 * @param  mask  bitmask of event types: OR of (1u << urcEvent_e), or
 *               0xFFFFFFFF for all events.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_urc_register(void *msgq, UINT32 mask);

/**
 * @brief  Unregister a previously registered URC queue.
 * @param  msgq  the queue to unregister.
 * @return wm_SdkResult - 0 success; negative on failure.
 */
wm_SdkResult wm_sdk_urc_unregister(void *msgq);

#ifdef __cplusplus
}
#endif

#endif /* __WM_SDK_URC_H__ */
