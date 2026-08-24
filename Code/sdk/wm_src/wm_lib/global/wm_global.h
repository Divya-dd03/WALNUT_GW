 /**
  ******************************************************************************
  * @file    wm_global.h
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

#ifndef __WM_GLOBAL_H__
#define __WM_GLOBAL_H__

#include "wm_api.h"
#include "mbedtls/cmac.h"
#include "mbedtls/md5.h"
#include "wm_enum.h"
#include "wm_custom_def.h"
#include "wm_extern_fnc.h"
#include <math.h>
#include <ctype.h>


extern volatile int gi_hw_version;
extern BOOL WALNUT_MQTT;

#define PING_ADDRESS_PRIMARY "8.8.8.8"
#define PING_ADDRESS_SECONDARY "8.8.8.8"

/* Customer App System */
#define USB_LOG_COM TRUE
#define WM_DEVICE_MODEL  "GW1NS-4"
#define MODEL_ID  "WM_SB_1605"
#define CUS_APP_VERSION "WMZ_DEV_1.0.2"
#define MAX_CONFIG_FILE_SIZE 2048
#define BATT_FC_VOLT  4200             
#define BATT_LOW_VOLT 3300
#define BATT_LOW_SHUTDOWN 3150
#define BATT_LOW_VOL_LVL 5
#define BATT_LOW_ANC_REPEAT 100
#define BATT_FULL_ANC_REPEAT 100
extern WM_LEDS_INDICATOR WM_SB_POWER_OFF_LED;
extern BOOL WM_SB_POWER_OFF_LED_BLINK_ON;
extern char WM_SDK_VERSION[100];
extern char WM_CUS_APP_VERSION[100];
extern volatile BOOL g_M2M_SIM_MODE;
extern BOOL WALNUT_MQTT;
extern char g_APP_FILE_Path[250];
extern char g_DFOTA_FILE_Path[250];
extern char g_DFOTA_FILE_SHA[65];
extern char g_APPOTA_FILE_SHA[65];
extern BOOL g_Enable_Hlt_Pub;
extern BOOL g_is_dfota;
extern const char *g_config_file_path;
extern char g_res_file_path[250];
extern const char *const WM_SB_ENV_STR[];
extern BOOL g_update_started;
extern BOOL g_updated_payment_history;
/* NOTE: this external symbol (a watchdog-disable flag) is REQUIRED by the
 * prebuilt closed-source lib_wmsrc.a; its name is locked by that binary's ABI
 * and cannot be renamed without rebuilding the library. */
extern BOOL Paytm_WDT;

/* Customer App AUDIO */
#define AUD_FL_LENGTH 250
extern char audio_dir_path[50];

/* Customer App MQTT */
#define WM_MAX_M_HOST_LEN 128
#define WM_MAX_M_CID_LEN 128
#define MAX_MQTT_TOPIC_SIZE 150 
#define MAX_MQTT_CRT_SIZE 2048
#define MQTT_MAX_SUBSCRIPTIONS 20
extern int MQTT_RCNT_LONG_INT; // Reconnection retry interval in seconds
extern int ONBOARD_RETRY_AFTER;
extern int MQTT_RCNT_SHORT_CNT;            // Reconnection retry count 
extern int MQTT_RCNT_SHORT_INT;
extern int WM_PUB_RETRY_COUNT;
extern int WM_PUB_RETRY_DELAY;
extern int MQTT_KEEP_ALIVE_TIME;
extern int MQ_SSL_ID;
extern int MQ_CID;
extern int MQ_QOS_SUB;
extern int MQ_QOS_PUB;
extern BOOL g_time_sync_https_success;

/* LOGGING */
#define WM_MAX_LOG_FILE_SIZE 1024*2
#define WM_MAX_FILE_PATH_LEN 255
extern BOOL g_file_log_enabled;
extern char g_log_file1[WM_MAX_FILE_PATH_LEN];
extern char g_log_file2[WM_MAX_FILE_PATH_LEN];
extern char* g_current_log_file;

/* Debug Control */
#define DEBUG_EN
extern BOOL TASK_DEBUG;
extern BOOL CONFIG_DATA_DIAG;
extern BOOL MQTT_DIAG;
extern BOOL MQTT_PUB_DIAG;
extern BOOL MQTT_SUB_DIAG;
extern BOOL FILE_DIAG;
extern BOOL HLT_DIAG;
extern BOOL ONBOARD_DIAG;
extern BOOL AUD_DIAG;
extern BOOL NETWORK_DIAG;
extern BOOL HTTP_DIAG;
extern BOOL WM_PING_DIAG;
extern BOOL GPIO_DIAG;
extern BOOL g_WM_DBG_PROFILER;
extern BOOL TIMER_DIAG;
extern BOOL COMM_DIAG;

/* System Control */
extern volatile BOOL g_is_powering_off;
extern volatile int32_t current_ticks;
extern volatile BOOL g_POWER_BTN_CHK;
extern volatile BOOL g_POWER_RST;
extern volatile UINT32 g32_tm_out_SIM;
extern volatile UINT32 g32_tm_out_NOCON;
extern volatile BOOL gf_EN_DUMP_KERNEL_LOGS;
extern const char* gf_usb_plugged_names[];
extern sMsgQRef WM_SF_msgq;
extern sMsgQRef WM_UI_msgq;
extern  unsigned int g32_cpuUsedRate;
extern	unsigned int g32_heapFreeSize;
extern sTaskInfo gs_task_info;
extern BOOL f_sys_state_on;
extern BOOL gf_usb_plugged;    /* Updated globally on USB event */
extern char g_mf_srno[33];
#define SC_HIGHEST_TASK_PRIORITY    200
#define SC_MEDIUM_TASK_PRIORITY     210
#define SC_LOW_TASK_PRIORITY        220
#define TP_WDT_PROCESS              (SC_HIGHEST_TASK_PRIORITY)
#define TP_SIM_URC_PROCESS          (SC_HIGHEST_TASK_PRIORITY+1)
#define TP_AUDIO_STOP_RECEIVER      (SC_HIGHEST_TASK_PRIORITY+2)
#define TP_HP_AUDIO_RECEIVER        (SC_HIGHEST_TASK_PRIORITY+3)
#define TP_LP_AUDIO_RECEIVER        (SC_HIGHEST_TASK_PRIORITY+4)
#define TP_MQTT_MNGR                (SC_HIGHEST_TASK_PRIORITY+5) // task priority slot
#define TP_MQTT_PUBLISHER           (SC_HIGHEST_TASK_PRIORITY+6) // task priority slot
#define TP_StateFLow                (SC_HIGHEST_TASK_PRIORITY+7) // task priority slot
#define TP_OTA_UPDATE               (SC_HIGHEST_TASK_PRIORITY+8) // task priority slot
// task priority slot
#define TP_SB_MNGR                  (SC_MEDIUM_TASK_PRIORITY)
#define TP_UI_TOP_THREAD            (SC_MEDIUM_TASK_PRIORITY+1)
#define TP_MSG_URC                  (SC_MEDIUM_TASK_PRIORITY+2)
#define TP_TIMED_ACTIVITY           (SC_MEDIUM_TASK_PRIORITY+3)
#define TP_DEBUG_TASK               (SC_MEDIUM_TASK_PRIORITY+4)
#define TP_SMS_TASK                 (SC_MEDIUM_TASK_PRIORITY+5)
#define TP_KEY1_SCAN                (SC_LOW_TASK_PRIORITY)
#define TP_KEY2_SCAN                (SC_LOW_TASK_PRIORITY+1)
#define TP_KEY3_SCAN                (SC_LOW_TASK_PRIORITY+2)
#define TP_KEY4_SCAN                (SC_LOW_TASK_PRIORITY+3)
#define TP_KEY5_SCAN                (SC_LOW_TASK_PRIORITY+4)
#define TP_TimeSync                 (SC_LOW_TASK_PRIORITY+5)
#define TP_GPS_TASK                 (SC_LOW_TASK_PRIORITY+6)
#define KEY_CODE_POWER 10
#define KEY_CODE_VUP   11
#define KEY_CODE_VDN   12
#define KEY_CODE_TRN   13
#define KEY_SINGLE     20
#define KEY_DOUBLE     21
#define KEY_LONG       22
#define KEY_SIMPRESS   23
#define LOOP_COUNTER_DBL 220   /* Loop counter to wait for debounce Max value can be 250 */
#define SINGLE_DEB_TIME  5    /* SINGLE DEBOUNCE TIME */
#define DELAY_DBL_PRESS 70    /* For Vol up and Dn key only */
#define DELAY_LNG_PRESS  120   /* For Vol up and Dn key only REDUCE FOR FASTER DETECTION OF LONG KEY */
#define RECEIVE_FLAG_MASK_SYNC (0x01 << 0)
#define RECEIVE_FLAG_MASK_SUCCESS (0x01 << 1)
#define RECEIVE_FLAG_MASK_FAIL (0x01 << 2)
#define MUTEX_HLT_PRM_TMOUT 5 // systicks
extern volatile int MUTEX_FILE_WRITE_TIMEOUT;
extern UINT8 KEY1_DBNC;
extern UINT8 KEY2_DBNC;
extern UINT8 KEY3_DBNC;
extern UINT8 KEY4_DBNC;
extern sMutexRef MutexRef_g_DataWrite;
extern sMutexRef MutexRef_g_DataRead;
extern char* sc_status_str[];
#define MAX_FILE_IN_DIR_1605 512

/* Network Control */
extern UINT8 g_u8_cfun;
extern UINT8 g_u8_csq;
extern UINT8 g_u8_cpin;
extern int g_int_cnmp;
extern int g_int_cgatt;  
extern int g_pdp_cid;
extern SCcpsiParm gs_scpsi;
extern SCApnParmGet gs_cgdcont_get;
extern SCApnParmSet gs_cgdcont_set; 
extern SCcgpaddrParm g_cgpaddr; 
extern char g_isdn[40];
extern char g_imei_value[64]; 
extern char g_imsi[64];    
extern char g_str_iccid[32];
extern char g_str_operator[100];
extern char gs_timezone[31];
extern char g_mnum[20];
extern UINT32 g_ping_counts;
extern UINT32 g_ping_min_success_counts;

/* Power Control */
extern volatile unsigned int g_ui_median_batt_voltage;  /* Median values */
extern volatile UINT8 g_u8_median_batt_level;           /* Median values */
extern volatile int g_BATT_LOW_ANC_REPEAT_INTV;
extern volatile int g_BATT_FULL_ANC_REPEAT_INTV;
extern volatile int g_BATT_LOW_ANC_REPEAT_CNT;
extern volatile int g_BATT_FULL_ANC_REPEAT_CNT;
extern volatile int g_BATT_LOW_VOL_SET_TMP;

/* LED Control */
#define WM_LED_DEFAULT_INTENSITY 80
extern volatile BOOL gf_blinnk_tmr_created;
extern struct ledStrucVar ledcv;


/* RTC Control */
#define get_rtc_date_time_msec get_rtc_date_time
extern t_rtc gs_currUtcTime;  /* RTC date and time */
extern sTimeval gs_high_res_time; /* Read high-resolution time */
extern const char* weekdays_abbr[];
extern const char* months_abbr[];
extern char gs_build_time_fr[80];
extern char g_time_sync_rcvd[100];
extern char g_time_sync_stored[100];
extern BOOL SKIP_RTC_SET_FOR_DEBUG;

/* MQTT Control */
extern volatile BOOL gf_mqtt_connected;
extern volatile INT8 wm_auto_connect_SUBSCRIBE_MQTT_ENABLED;
extern CHAR mqtt_topic[256];
extern CHAR mqtt_payload[200];
extern volatile BOOL g_start_mqtt_connect; // set to true for autoconnect
extern volatile BOOL g_mqtt_configured_or_started;
extern volatile BOOL g_mqtt_acquired;
extern volatile BOOL g_mqtt_certs_loaded;
extern volatile BOOL g_mqtt_subscribed;
extern char json_data[250];
extern char mqtt_default_topics[MAX_CONFIG_FILE_SIZE];
extern char g_mqtt_thing_name[32];
extern char g_mqtt_otp_topic[250];
extern char g_mqtt_settings_topic[250];
extern mqtt_topic_config_t g_mqtt_health_topic;
extern mqtt_topic_config_t g_mqtt_otp_ack_topic;
extern mqtt_topic_config_t g_mqtt_global_ack;
extern mqtt_topic_config_t g_mqtt_transaction_ack_topic;
extern char g_mqtt_transaction_topic[250];
extern volatile int g_wm_hltpub_intv;
extern volatile int g_wm_icons_intv;

/* HTTP Control */
#define SC_APP_DOWNLOAD_BUF_SIZE    2048
#define MAX_PARA_STR_SIZE 256
#define MAX_CA_CERT_SIZE  1446
extern SC_HTTP_RETURNCODE g_http_error_code;
extern int g_ssl_connect_time_out;
extern int g_http_connect_time_out;
extern volatile BOOL gf_sim_ready;
extern volatile BOOL gf_net_ready;
extern volatile BOOL gf_pdp_ready;
extern volatile int gint_http_get_status;      /* HTTP status on get */
extern volatile UINT8 g8_dnl_file_perc;

/* Audio Control */
#define MIN_VOLUME 1
#define DEFAULT_VOLUME 3
#define MUTE_VOLUME 2
#define MAX_VOLUME 11
#define WM_ACOMP_EVENT            (1 << 0)  // 0x01
#define WM_ASTOP_EVENT            (1 << 1)  // 0x02
#define WM_ASTOP_COMP_EVENT       (1 << 2)  // 0x04
#define WM_LPA_REL_EVENT          (1 << 3)  // 0x08
#define WM_AUD_LAST_FILE_EVENT    (1 << 4)  // 0x10
#define WM_AUD_IDLE_EVENT         (1 << 5)  // 0x20


extern volatile BOOL g_amp_internal_control;
extern sFlagRef AUD_flagRef;
extern volatile BOOL g_HPA_PROC;
extern const char* g_uc8_volume_names[];
extern volatile BOOL g_audio_play_enabled;
extern AUD_PLAY_STATUS g_audio_status;
extern volatile  AUD_Volume g_uc8_audio_amp;
extern volatile UINT8 g_audio_wakeup_delay;

/* USB Control */

/* Unzip */
extern volatile BOOL gf_unzip_success;
extern volatile BOOL gf_loop_qa_test;
extern char g_unzip_async_path[300];

#endif