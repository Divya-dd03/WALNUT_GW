/**
  ******************************************************************************
  * @file    sim.h
  * @author  WheelsEye
  * @brief   SIM module public interface for the weware application.
  *          SIM presence via SIM-detect GPIO; PIN/ICCID helpers.
  ******************************************************************************
  */

#ifndef WEWARE_SIM_H
#define WEWARE_SIM_H

#include <stdbool.h>

#include "sdk_types.h"

/* Minimum buffer size (bytes) for weware_sim_get_sim_number() */
#define WEWARE_SIM_ICCID_BUFFER_SIZE (32U)

/* Notified on every debounced SIM insert/remove transition */
typedef void (*weware_sim_status_cb_t)(bool sim_available);

/**
 * @brief  Configure the SIM-detect GPIO and start the detect/debounce task.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_sim_init(void);

/**
 * @brief  Stop the detect task and reset module state.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_sim_deinit(void);

/**
 * @brief  Last debounced SIM presence (from the detect GPIO).
 * @return true if a SIM is inserted.
 */
bool weware_sim_get_sim_status(void);

/**
 * @brief  Force the SIM presence state (normally driven by the detect task).
 */
void weware_sim_set_sim_available(bool sim_available);

/**
 * @brief  Read the current SIM status from the modem.
 * @return SDK_SIM_PRESENT / SDK_SIM_ABSENT / SDK_SIM_READY;
 *         SDK_SIM_ERROR if the query fails or is unsupported.
 */
SdkSimStatus weware_sim_read_status(void);

/**
 * @brief  Query the raw SIM status from the modem (Walnut modem path).
 * @param  status  [out] SDK_SIM_PRESENT / ABSENT / READY / ERROR.
 * @return SDK_RESULT_SUCCESS, SDK_RESULT_NOT_SUPPORTED,
 *         SDK_RESULT_INVALID_PARAM or negative error from the SDK.
 */
SdkResult weware_sim_query_modem_status(SdkSimStatus *status);

/**
 * @brief  Check SIM readiness via the modem PIN status (CPIN).
 * @param  api_error_out  [out] optional; true if the PIN-status API itself
 *                        failed (SIM/modem not answering), not a locked SIM.
 * @return SDK_RESULT_SUCCESS when SIM is ready (no PIN pending),
 *         SDK_RESULT_NOT_SUPPORTED if the platform lacks the API,
 *         SDK_RESULT_ERROR otherwise.
 */
SdkResult weware_sim_check_sim_ready(bool *api_error_out);

/**
 * @brief  Read the SIM ICCID.
 * @param  iccid  [out] buffer of at least WEWARE_SIM_ICCID_BUFFER_SIZE bytes;
 *                set to "" on failure.
 * @return SDK_RESULT_SUCCESS, SDK_RESULT_NOT_SUPPORTED,
 *         SDK_RESULT_INVALID_PARAM or SDK_RESULT_ERROR.
 */
SdkResult weware_sim_get_sim_number(char *iccid);

/**
 * @brief  Register a callback for SIM insert/remove transitions (one slot;
 *         pass NULL to unregister).
 */
void weware_sim_register_status_callback(weware_sim_status_cb_t callback);

#endif /* WEWARE_SIM_H */
