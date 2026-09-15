/**
 * @file sdk_functionality_os.h
 * @brief Walnut compat shim: the reference includes this for its task,
 *        mutex, message-queue, heap and system-stats API. Walnut exposes
 *        the same functions natively as wm_sdk_task_*, wm_sdk_mutex_*,
 *        wm_sdk_msgq_*, wm_sdk_memory_* and wm_sdk_system_get_stats()
 *        (wm_sdk_os.h).
 */

#ifndef WEWARE_WALNUT_SDK_FUNCTIONALITY_OS_H
#define WEWARE_WALNUT_SDK_FUNCTIONALITY_OS_H

#include "wm_sdk_os.h"

/* Generic URC/queue message header (CG sdk_platform_platform.h layout).
 * On walnut nothing posts these - CG code passes a queue of this element
 * size to vendor APIs (e.g. sdk_https_download_init) as the SIMCOM URC
 * transport, which the walnut backend accepts and ignores. */
#ifndef SDK_MSG_T_DEFINED
#define SDK_MSG_T_DEFINED 1
typedef struct { UINT32 msg_id; UINT32 arg1; UINT32 arg2; void* arg3; } sdk_msg_t;
#endif

#endif /* WEWARE_WALNUT_SDK_FUNCTIONALITY_OS_H */
