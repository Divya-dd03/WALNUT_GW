/**
 * @file urc_processor.h
 * @brief URC (Unsolicited Result Code) processor for weware platform
 *
 * Receives URCs from the modem SDK on a dedicated queue, then fans each URC
 * out to the consumer module's urc_q (queue_manager ring).
 *
 * Flow (CG reference adapted - walnut URCs are bare urcEvent_e codes with no
 * mask or payload, and the kernel emits no SMS/GNSS URCs, so the reference's
 * SMS inlining and NMEA assembler have no counterpart here):
 * - URC processor task is the SOLE sdk_urc_register() registrant (mask = all)
 * - Task receives the UINT32 event code from its SDK message queue
 * - Event code is mapped to a module (net/PDP -> NETWORK, SIM_* -> SIM) and
 *   queue_push()ed to that module's config.urc_q when non-NULL
 *
 * Modules that consume URCs set config.urc_q_config in module_config.c
 * (element_size = sizeof(UINT32)) and create config.urc_q with
 * queue_manager_create in their init, same pattern as msg_q. Events arriving
 * before the consumer's urc_q exists are dropped with a warning.
 */

#ifndef WEWARE_URC_PROCESSOR_H
#define WEWARE_URC_PROCESSOR_H

#include <stdbool.h>

#include "sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize URC processor
 * @return SDK_RESULT_SUCCESS on success, SDK_RESULT_ERROR on failure
 * @note Creates modem URC queue and processing task. Task registers modem URC feed.
 */
SdkResult urc_processor_init(void);

/**
 * @brief Deinitialize URC processor
 * @return SDK_RESULT_SUCCESS on success
 * @note Stops task, deletes queue, clears bindings
 */
SdkResult urc_processor_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_URC_PROCESSOR_H */
