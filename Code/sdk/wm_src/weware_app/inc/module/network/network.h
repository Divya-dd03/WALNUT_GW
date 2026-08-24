/**
  ******************************************************************************
  * @file    network.h
  * @author  WheelsEye
  * @brief   Network module public interface for the weware application.
  *          State-machine driven network bring-up (SIM -> CTZU -> CREG ->
  *          CGREG -> CGATT -> PDP -> IP -> CONNECTED) with automatic
  *          reconnection, per-state timeouts and URC-driven transitions.
  ******************************************************************************
  */

#ifndef WEWARE_NETWORK_H
#define WEWARE_NETWORK_H

#include <stdbool.h>

#include "sdk_types.h"

/*---------------------------------------------------------------
 * Types
 *--------------------------------------------------------------*/
/* Network state machine states */
typedef enum {
    NETWORK_STATE_INIT = 0,
    NETWORK_STATE_CHECK_SIM,
    NETWORK_STATE_SIM_INSERT,
    NETWORK_STATE_SIM_REMOVE,
    NETWORK_STATE_CHECK_CTZU,
    NETWORK_STATE_SET_CTZU,
    NETWORK_STATE_CHECK_REGISTRATION,
    NETWORK_STATE_CHECK_GPRS_REGISTRATION,
    NETWORK_STATE_CHECK_LTE_ATTACHMENT,
    NETWORK_STATE_SETUP_PDP,
    NETWORK_STATE_ACTIVATE_PDP,
    NETWORK_STATE_GET_IP,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_ERROR,
    NETWORK_STATE_RESTART_CFUN,
} NetworkState;

/* Cached radio info (CSQ + serving cell) refreshed while connected */
typedef struct {
    int  signal_strength;   /* CSQ 0..31; 0 when unknown            */
    int  mcc;               /* mobile country code                  */
    int  mnc;               /* mobile network code                  */
    int  lac;               /* location / tracking area code        */
    int  cell_id;           /* serving cell id                      */
    bool valid;             /* true when the fields are populated   */
} NetworkRadioSnapshot;

/* Notified on every data-connection up/down transition */
typedef void (*weware_network_status_cb_t)(bool connected);

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/
/**
 * @brief  Create the URC queue and start the network state-machine task.
 *         Registers for network URCs and SIM insert/remove notifications.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_network_init(void);

/**
 * @brief  Stop the task, unregister the URC queue and reset module state.
 * @return SDK_RESULT_SUCCESS or negative error.
 */
SdkResult weware_network_deinit(void);

/**
 * @brief  Current data-connection status (PDP up with an IP address).
 * @return true when connected.
 */
bool weware_network_is_connected(void);

/**
 * @brief  Connected AND signal at or above the minimum CSQ threshold.
 * @return true when the connection is considered stable.
 */
bool weware_network_is_stable(void);

/**
 * @brief  Current network state-machine value (diagnostic).
 */
NetworkState weware_network_get_state(void);

/**
 * @brief  Human-readable name for a network state (static string).
 */
const char *weware_network_state_to_string(NetworkState state);

/**
 * @brief  Copy out the cached radio snapshot (CSQ + serving cell).
 * @param  out  [out] receives the snapshot; check out->valid.
 * @return true if @p out was written.
 */
bool weware_network_get_radio_snapshot(NetworkRadioSnapshot *out);

/**
 * @brief  Override the APN used for the PDP context (applied on the next
 *         SETUP_PDP pass). Pass NULL to keep the current value.
 * @param  apn       access point name, e.g. "wheelseye.com".
 * @param  pdp_type  PDP type string, e.g. "IP" / "IPV4V6"; NULL keeps current.
 * @return SDK_RESULT_SUCCESS or SDK_RESULT_INVALID_PARAM if @p apn is too long.
 */
SdkResult weware_network_set_apn(const char *apn, const char *pdp_type);

/**
 * @brief  Register a callback for connect/disconnect transitions (one slot;
 *         pass NULL to unregister).
 */
void weware_network_register_status_callback(weware_network_status_cb_t callback);

#endif /* WEWARE_NETWORK_H */
