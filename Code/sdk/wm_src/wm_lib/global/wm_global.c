/**
  ******************************************************************************
  * @file    wm_global.c
  * @brief   Global Variables.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

#include "wm_global.h"

/* Customer App */
char WM_SDK_VERSION[100] = { 0 };
char WM_CUS_APP_VERSION[100] = CUS_APP_VERSION;
volatile UINT32 g32_tm_out_NOCON = (UINT32)(15 * 60 * 1000);
volatile UINT32 g32_tm_out_SIM = (UINT32)(10*60*1000);
const char* g_config_file_path = "C:/config.json";
char g_res_file_path[250] = "D:/res.ver"; // updating in config update
char g_APP_FILE_Path[250] = "C:/customer_app.bin";
char g_DFOTA_FILE_Path[250] = "C:/system_patch.bin";
BOOL g_is_dfota = TRUE;
char g_DFOTA_FILE_SHA[65] = {0};
char g_APPOTA_FILE_SHA[65] = {0};
BOOL g_Enable_Hlt_Pub = TRUE;
WM_LEDS_INDICATOR WM_SB_POWER_OFF_LED = WM_LED_OFF;
BOOL WM_SB_POWER_OFF_LED_BLINK_ON = FALSE;
BOOL g_WM_DBG_PROFILER = FALSE;
BOOL WALNUT_MQTT = TRUE;
volatile BOOL g_ota_http_direct_mode = FALSE;
BOOL g_update_started=FALSE;
BOOL g_updated_payment_history=FALSE;
/* Paytm_WDT (watchdog flag required by the prebuilt lib_wmsrc.a) is defined in
 * wm_sdk_wm.c alongside the other prebuilt-lib requirements. */

/* Customer App AUDIO */
char audio_dir_path[50] = {0};
char audio_blank_file_path[100] = {0};

/* Customer App MQTT */
int MQTT_RCNT_LONG_INT = 30; // Reconnection retry interval in seconds
int ONBOARD_RETRY_AFTER = 2; // Retry after 10 seconds
int MQTT_RCNT_SHORT_CNT = 10;            // Reconnection retry count 
int MQTT_RCNT_SHORT_INT = 10;            // Reconnection retry interval 
int WM_PUB_RETRY_COUNT = 5;
int WM_PUB_RETRY_DELAY = 5;
int MQTT_KEEP_ALIVE_TIME = 120;
char M_HOST[WM_MAX_M_HOST_LEN] = "";
char M_HOST_M2M[WM_MAX_M_HOST_LEN] = "";
char M_CID_NAME[WM_MAX_M_CID_LEN] = "";
int MQ_SSL_ID = 4;
int MQ_CID = 0;
int MQ_QOS_SUB = 1;
int MQ_QOS_PUB = 1;
BOOL g_time_sync_https_success = FALSE;

/* LOGGING */
BOOL g_file_log_enabled = FALSE;
char g_log_file1[WM_MAX_FILE_PATH_LEN] = "C:/sb_logs1.log";
char g_log_file2[WM_MAX_FILE_PATH_LEN] = "C:/sb_logs2.log";
char* g_current_log_file = NULL;

/* Debug Control */
BOOL TASK_DEBUG = FALSE;
BOOL CONFIG_DATA_DIAG = FALSE;
BOOL MQTT_DIAG = FALSE;
BOOL MQTT_PUB_DIAG = FALSE; 
BOOL MQTT_SUB_DIAG = FALSE;
BOOL FILE_DIAG = FALSE;
BOOL HLT_DIAG = FALSE;
BOOL ONBOARD_DIAG = FALSE;
BOOL AUD_DIAG = FALSE;
BOOL NETWORK_DIAG = FALSE;
BOOL HTTP_DIAG = FALSE;
BOOL WM_PING_DIAG = FALSE;
BOOL GPIO_DIAG = FALSE;
BOOL TIMER_DIAG = FALSE;
BOOL COMM_DIAG = TRUE;

/* System Control */
volatile BOOL g_is_powering_off = FALSE;
volatile int32_t current_ticks = 0;
volatile BOOL g_POWER_BTN_CHK = FALSE;
volatile BOOL g_POWER_RST = FALSE;
char gs_build_time_fr[80];   /* FIRMWARE BUILD TIME STRING */
unsigned int g32_cpuUsedRate;
unsigned int g32_heapFreeSize;
sTaskInfo gs_task_info;
BOOL f_sys_state_on;
volatile int MUTEX_FILE_WRITE_TIMEOUT = 500;
sMutexRef MutexRef_g_DataWrite = NULL;
sMutexRef MutexRef_g_DataRead = NULL;

/* Network Control */
UINT32 g_ping_counts = 10;
UINT32 g_ping_min_success_counts = 7;

/* Power Control */
volatile unsigned int g_ui_median_batt_voltage;  /* Median values */
volatile UINT8 g_u8_median_batt_level;           /* Median values */
volatile int g_BATT_LOW_VOL_SET_TMP = 0;

/* LED Control */
struct ledStrucVar ledcv = {0, 0, 0, 1};
volatile BOOL gf_blinnk_tmr_created;

/* RTC Control */
t_rtc gs_currUtcTime;  /* RTC date and time structure */
sTimeval gs_high_res_time; /* Read high resolution time */
char g_time_sync_rcvd[100] = { 0 };
char g_time_sync_stored[100] = { 0 };
BOOL SKIP_RTC_SET_FOR_DEBUG = FALSE;
const char* weekdays_abbr[] =
{
    "MON",
    "TUE",
    "WED",
    "THU",
    "FRI",
    "SAT",
    "SUN"
};

const char* months_abbr[] =
{
    "JAN",
    "FEB",
    "MAR",
    "APR",
    "MAY",
    "JUN",
    "JUL",
    "AUG",
    "SEP",
    "OCT",
    "NOV",
    "DEC"
};

/* HTTP Control */
volatile UINT8 g8_dnl_file_perc = 0;

/* Audio Control */
volatile BOOL g_amp_internal_control = TRUE;
volatile BOOL g_audio_play_enabled;
AUD_PLAY_STATUS g_audio_status;
volatile AUD_Volume g_uc8_audio_amp = MIN_VOLUME;
volatile UINT8 g_audio_wakeup_delay;  /* Control click and pop min2ms max50ms */
const char* g_uc8_volume_names[] =
{
        "0",//"AUDIO_VOLUME_MUTE",
        "1",//"AUDIO_VOLUME_1",
        "2",//"AUDIO_VOLUME_2",
        "3",//"AUDIO_VOLUME_3",
        "4",//"AUDIO_VOLUME_4",
        "5",//"AUDIO_VOLUME_5",
        "6",//"AUDIO_VOLUME_6",
        "7",//"AUDIO_VOLUME_7",
        "8",//"AUDIO_VOLUME_8",
        "9",//"AUDIO_VOLUME_9",
        "10",//"AUDIO_VOLUME_10",
        "11",//"AUDIO_VOLUME_11"
};

/* USB Control */
BOOL gf_usb_plugged;    /* Updated globally on USB event */
const char* gf_usb_plugged_names[] =
{
    "N",//"Disconnected",
    "Y"//"Connected"
};

/* Unzip */
volatile BOOL gf_unzip_success;
volatile BOOL gf_loop_qa_test;
char g_unzip_async_path[300] = "D:/data.zip";

/* Timers */
//timer_param t_param[51];  /* UPTO 50 TIMERS ARE HANDLED ENUM IS INDEXED FROM 1 TO 51 */    

/* LIB_SRC Variables */
volatile BOOL gf_EN_DUMP_KERNEL_LOGS = TRUE;
volatile int t_time_NOCON;
UINT8 g_u8_cfun;
UINT8 g_u8_csq;
UINT8 g_u8_cpin;
int g_int_cnmp;
int g_int_cgatt;
int g_pdp_cid;
SCcpsiParm gs_scpsi = {0};
SCApnParmGet gs_cgdcont_get = {0};
SCApnParmSet gs_cgdcont_set = {0};
SCcgpaddrParm g_cgpaddr = {0};
char g_imei_value[64] = {0}; 
char g_imsi[64] = {0};     
char g_str_iccid[32] = {0};
char g_str_operator[100] = {0};  
char gs_timezone[31] = {0};  
char g_isdn[40] = {0};  
char g_mf_srno[33] = {0};
char g_mnum[20] = {0};
BOOL gf_debug;
volatile BOOL gf_sim_ready;
volatile BOOL gf_net_ready;
volatile BOOL gf_pdp_ready;

void wm_initialize_var(void)
{
    g_audio_wakeup_delay = 0;
    g_uc8_audio_amp = DEFAULT_VOLUME;
    g_audio_play_enabled = FALSE;

    g_ui_median_batt_voltage = 0; /*First update by reading of level */
    g_u8_median_batt_level = 0; /*battery level in % */

    g_u8_cfun = 0;
    g_u8_csq = 0;
    g_u8_cpin = 0;
    g_int_cnmp = 0;
    g_int_cgatt = 0;
	g_pdp_cid = 0;

    memset(&gs_scpsi, 0, sizeof(gs_scpsi));
	memset(&gs_cgdcont_get, 0, sizeof(gs_cgdcont_get));
	memset(&gs_cgdcont_set, 0, sizeof(gs_cgdcont_set));
	memset(&g_cgpaddr, 0, sizeof(g_cgpaddr));
    memset(g_imei_value, 0, sizeof(g_imei_value));
    memset(g_imsi, 0, sizeof(g_imsi));
    memset(g_str_iccid, 0, sizeof(g_str_iccid));
    memset(g_str_operator, 0, sizeof(g_str_operator));
	memset(gs_timezone, 0, sizeof(gs_timezone));
    memset(g_isdn, 0, sizeof(g_isdn));
    memset(g_mf_srno, 0, sizeof(g_mf_srno));
    memset(g_mnum, 0, sizeof(g_mnum));
}
