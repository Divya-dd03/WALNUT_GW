/**
 * @file module_config.c
 * @brief Module registry - definitions of all application modules and g_modules[] array.
 *
 * Walnut port of the reference module_config.c. The ModuleId enum keeps the
 * full reference order (module_manager.h); modules not yet ported to walnut
 * (UART, BLE) keep their registry slots but are disabled with NULL
 * init functions, so module_manager treats them as trivially initialized.
 *
 * Init/deinit/config function pointers use the same cast convention as the
 * reference: SdkResult-returning walnut inits are numerically compatible with
 * Result (see common/types.h).
 */

/*---------------------------------------------------------------
 * Includes
 *--------------------------------------------------------------*/
#include "module/module_config.h"
#include "sdk_platform.h"

#include "module/log/log_manager.h"
#include "module/log/log_config.h"
#include "module/gps/gps_manager.h"
#include "module/gps/gps_config.h"
#include "module/gps/gps_urc_queue_types.h"
#include "module/network/network.h"
#include "module/network/network_config.h"
#include "module/sim/sim.h"
#include "module/sms/sms_manager.h"
#include "module/sms/sms_config.h"
#include "module/urc/urc_sms_queue_types.h"
#include "module/tcp/tcp.h"
#include "module/uart/uart_manager.h"
#include "module/command/command_manager.h"
#include "module/urc/urc_processor.h"
#include "system/system_config.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "MODULE_CFG"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Module Definitions (order must match ModuleId enum)
 *--------------------------------------------------------------*/

static Module g_module_log = {
    .config = {
        .module_id = MODULE_ID_LOG,
        .name = "Log Manager",
        .enabled = TRUE,
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = FALSE,
        .init_fn = (ModuleInitFn)log_module_init,
        .deinit_fn = (ModuleDeinitFn)logger_deinit,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)log_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)log_config_validate,
        .config_ptr = &g_log_config,
        .config_size = sizeof(LogConfig),
        .config_stored = FALSE,
        .msg_q_config = {0},
        .msg_q = NULL,
        .urc_q_config = {0},
        .urc_q = NULL
    },
    .status = {0}
};

/* UART manager: enabled, but see the blocker note at the top of
 * src/module/uart/uart_manager.c - the SDK's sdk_uart_set_config() can only ask
 * for drvUart port 0 (the CP debug console), which the driver always refuses,
 * so init currently fails with ERR_UART_CONFIG_ERROR. The board's free
 * full-duplex UART is drvUart port 2 (0xd401f000); reaching it needs an
 * sdk_uart.h implementation that honours the port argument. */
static Module g_module_uart = {
    .config = {
        .module_id = MODULE_ID_UART,
        .name = "UART Manager",
        .enabled = FALSE, /* currently unavailable as ports are not exposed */
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)uart_manager_init,
        .deinit_fn = (ModuleDeinitFn)uart_manager_deinit,
        .config_get_defaults_fn = NULL,
        .config_validate_fn = NULL,
        .config_ptr = NULL,
        .config_size = 0,
        .config_stored = FALSE,
        .msg_q_config = {
            .name = "UART_REQUEST_Q",
            .element_size = sizeof(ModuleMessage),
            .capacity = 5,
            .thread_safe = FALSE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .msg_q = NULL
    },
    .status = {
        .initialized = FALSE,
        .connected = FALSE,
        .task_uptime_sec = 0,
        .retries = 0,
        .last_error = 0
    }
};

static Module g_module_cmd = {
    .config = {
        .module_id = MODULE_ID_CMD,
        .name = "Command Manager",
        .enabled = TRUE,
        .required = TRUE,
        .continue_on_fail = FALSE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)command_manager_init,
        .deinit_fn = (ModuleDeinitFn)command_manager_deinit,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)command_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)command_config_validate,
        .config_ptr = &g_command_config,
        .config_size = sizeof(CommandConfig),
        .config_stored = TRUE,
        /* thread_safe deliberately TRUE, unlike the reference's FALSE: this
         * queue has TWO producers on other tasks (the SMS task via
         * command_manager_accept_request, the TCP task via its recv path) and
         * is drained by the command task. queue_manager only creates a mutex
         * when thread_safe is set (q->mutex stays NULL otherwise), so the
         * reference value leaves the ring buffer completely unlocked across
         * tasks - a latent CG bug, not a port artifact. Observed 2026-09-08 as
         * duplicated/corrupted SMS command replies. */
        .msg_q_config = {
            .name = "CMD_REQUEST_Q",
            .element_size = sizeof(ModuleMessage),
            .capacity = 5,
            .thread_safe = TRUE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .msg_q = NULL
    },
    .status = {
        .initialized = FALSE,
        .connected = FALSE,
        .task_uptime_sec = 0,
        .retries = 0,
        .last_error = 0
    }
};

static Module g_module_urc = {
    .config = {
        .module_id = MODULE_ID_URC,
        .name = "URC Processor",
        .enabled = TRUE,
        .required = TRUE,
        .continue_on_fail = FALSE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)urc_processor_init,
        .deinit_fn = (ModuleDeinitFn)urc_processor_deinit,
        .config_get_defaults_fn = NULL,
        .config_validate_fn = NULL,
        .config_ptr = NULL,
        .config_size = 0,
        .config_stored = FALSE
    },
    .status = {0}
};

static Module g_module_sim = {
    .config = {
        .module_id = MODULE_ID_SIM,
        .name = "SIM Manager",
        .enabled = TRUE,
        .required = TRUE,
        .continue_on_fail = FALSE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)weware_sim_init,
        .deinit_fn = (ModuleDeinitFn)weware_sim_deinit,
        .config_get_defaults_fn = NULL,
        .config_validate_fn = NULL,
        .config_ptr = NULL,
        .config_size = 0,
        .config_stored = FALSE,
        .msg_q_config = {0},
        .msg_q = NULL
    },
    .status = {0}
};

static Module g_module_network = {
    .config = {
        .module_id = MODULE_ID_NETWORK,
        .name = "Network Manager",
        .enabled = TRUE,
        .required = TRUE,
        .continue_on_fail = FALSE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)weware_network_init,
        .deinit_fn = (ModuleDeinitFn)weware_network_deinit,
        /* Persisted NetworkConfig (reference parity): apn/user/pass/cid/auto,
         * loaded by config_load_from_file before init, saved by
         * network_config_set/_set_apn. */
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)network_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)network_config_validate,
        .config_ptr = &g_network_config,
        .config_size = sizeof(NetworkConfig),
        .config_stored = TRUE,
        .msg_q_config = {0},
        .msg_q = NULL,
        /* URC fan-out landing queue (reference NET_URC_Q): urc_processor
         * queue_push()es the bare urcEvent_e code - walnut URCs carry no
         * payload, so the element is UINT32 vs the reference's sdk_msg_t.
         * Created/destroyed by weware_network_init/deinit. */
        .urc_q_config = {
            .name = "NET_URC_Q",
            .element_size = (UINT32)sizeof(UINT32),
            .capacity = 8U,
            .thread_safe = TRUE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .urc_q = NULL
    },
    .status = {0}
};

static Module g_module_tcp = {
    .config = {
        .module_id = MODULE_ID_TCP,
        .name = "TCP Client",
        .enabled = TRUE,
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)weware_tcp_init,
        .deinit_fn = (ModuleDeinitFn)weware_tcp_deinit,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)weware_tcp_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)weware_tcp_config_validate,
        .config_ptr = &g_tcp_config,
        .config_size = sizeof(WewareTcpConfig),
        .config_stored = TRUE,
        /* TCP store-and-forward send queue (reference TCP_SEND_Q): created by
         * weware_tcp_init, filled by GPS/producers via queue_push, drained in
         * ACK-committed batches of 5 by the TCP state machine. Persists across
         * soft reboot (module_manager_persist_reboot_data). */
        .msg_q_config = {
            .name = "TCP_SEND_Q",
            .element_size = sizeof(ModuleMessage),
            .capacity = 7,
            .thread_safe = TRUE,
            .max_file_size = 1024 * 200,
            .persist_on_reboot = TRUE
        },
        .msg_q = NULL
    },
    .status = {0}
};

static Module g_module_gps = {
    .config = {
        .module_id = MODULE_ID_GPS,
        .name = "GPS Manager",
        .enabled = TRUE,
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)gps_manager_init,
        .deinit_fn = (ModuleDeinitFn)gps_manager_deinit,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)gps_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)gps_config_validate,
        .config_ptr = &g_gps_config,
        .config_size = sizeof(GpsConfig),
        .config_stored = TRUE,
        .msg_q_config = {0},
        .msg_q = NULL,
        /* Combined RMC+GGA NMEA records (reference GPS_URC_Q): pushed by the
         * kernel-NMEA-callback pairing in gps_manager.c, drained and parsed
         * by the GPS task (HDOP + RMC/GGA cross-check). */
        .urc_q_config = {
            .name = "GPS_URC_Q",
            .element_size = (UINT32)sizeof(gps_urc_queued_t),
            .capacity = 4U,
            .thread_safe = TRUE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .urc_q = NULL
    },
    .status = {0}
};

static Module g_module_sms = {
    .config = {
        .module_id = MODULE_ID_SMS,
        .name = "SMS Manager",
        .enabled = TRUE,
        .required = TRUE,
        .continue_on_fail = FALSE,
        .has_task = TRUE,
        .init_fn = (ModuleInitFn)sms_manager_init,
        .deinit_fn = (ModuleDeinitFn)sms_manager_deinit,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)sms_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)sms_config_validate,
        .config_ptr = &g_sms_config,
        .config_size = sizeof(SmsConfig),
        .config_stored = TRUE,
        /* Outbound SMS + command replies routed back to the originating phone
         * number (reference SMS_SEND_Q): filled by sms_manager_send and by
         * utils_route_response_to_module, drained one per cycle by the SMS
         * task.
         * thread_safe deliberately TRUE, unlike the reference's FALSE: the
         * producer is the COMMAND task (utils_route_response_to_module) and the
         * consumer is the SMS task, and queue_manager only creates a mutex when
         * this flag is set - see the CMD_REQUEST_Q note above. */
        .msg_q_config = {
            .name = "SMS_SEND_Q",
            .element_size = sizeof(ModuleMessage),
            .capacity = 5,
            .thread_safe = TRUE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .msg_q = NULL,
        /* Inbound SMS landing queue (reference SMS_URC_Q): the reference's
         * urc_processor pushed the raw +CMTI line here. Walnut has no SMS URC
         * - sms_manager.c parks the SDK's SDK_SMS_EVT_INCOMING events (raw
         * +CMGR text inlined, see urc_sms_queue_types.h) so the same
         * pop -> sms_process_urc flow runs. */
        .urc_q_config = {
            .name = "SMS_URC_Q",
            .element_size = (UINT32)sizeof(sms_urc_queued_t),
            .capacity = 5U,
            .thread_safe = TRUE,
            .max_file_size = 0,
            .persist_on_reboot = FALSE
        },
        .urc_q = NULL
    },
    .status = {
        .initialized = FALSE,
        .connected = FALSE,
        .task_uptime_sec = 0,
        .retries = 0,
        .last_error = 0
    }
};

/* BLE manager not ported to walnut yet - slot kept, module disabled. */
static Module g_module_ble = {
    .config = {
        .module_id = MODULE_ID_BLE,
        .name = "BLE Manager",
        .enabled = FALSE,
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = FALSE,
        .init_fn = NULL,
        .deinit_fn = NULL,
        .config_get_defaults_fn = NULL,
        .config_validate_fn = NULL,
        .config_ptr = NULL,
        .config_size = 0,
        .config_stored = FALSE,
        .msg_q_config = {0},
        .msg_q = NULL
    },
    .status = {0}
};

static Module g_module_system = {
    .config = {
        .module_id = MODULE_ID_SYSTEM,
        .name = "System Manager",
        .enabled = TRUE,
        .required = FALSE,
        .continue_on_fail = TRUE,
        .has_task = FALSE,
        .init_fn = NULL,
        .deinit_fn = NULL,
        .config_get_defaults_fn = (ModuleConfigGetDefaultsFn)system_config_get_defaults,
        .config_validate_fn = (ModuleConfigValidateFn)system_config_validate,
        .config_ptr = &g_system_config,
        .config_size = sizeof(SystemConfig),
        .config_stored = TRUE,
        .msg_q_config = {0},
        .msg_q = NULL
    },
    .status = {0}
};

/* Order must match ModuleId enum (LOG first so sink is ready early in module init pass) */
Module *g_modules[] = {
    &g_module_log,      /* MODULE_ID_LOG */
    &g_module_uart,     /* MODULE_ID_UART (disabled - no usable drvUart port yet) */
    &g_module_cmd,      /* MODULE_ID_CMD */
    &g_module_urc,      /* MODULE_ID_URC */
    &g_module_sim,      /* MODULE_ID_SIM */
    &g_module_network,  /* MODULE_ID_NETWORK */
    &g_module_tcp,      /* MODULE_ID_TCP */
    &g_module_gps,      /* MODULE_ID_GPS */
    &g_module_sms,      /* MODULE_ID_SMS */
    &g_module_ble,      /* MODULE_ID_BLE (disabled) */
    &g_module_system,   /* MODULE_ID_SYSTEM */
    NULL,               /* MODULE_ID_FILE_TRANSFER - no Module slot (legacy) */
};

const UINT32 g_module_count = sizeof(g_modules) / sizeof(g_modules[0]);
