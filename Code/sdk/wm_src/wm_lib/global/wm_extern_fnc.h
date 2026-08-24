/**
  ******************************************************************************
  * @file    wm_extern_fnc.h
  * @brief   External function calls.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024  Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_EXTERN_FNC_H__
#define __WM_EXTERN_FNC_H__

#include "wm_api.h"
#include "wm_enum.h"
#include "wm_custom_def.h"

/* Logger control */
extern void wm_logger_mode(BOOL t_mode);
extern void PrintfResp(char* format);
extern SC_STATUS wm_log_file(const char* fileData);
extern void wm_post_boot_kernel_logs_en(void);
extern void WM_TIC(char* prefix);
extern void WM_TOC(char* prefix);

/* Kernel start */
extern void sc_module_init(void);
extern void sAPP_wm_urc_init(void);
extern void wm_sb_on(void);

/* Global variables */
extern void wm_initialize_var(void);

/* UI App */
extern void wm_UIAPP_init(void);
extern void sendMsgToUIDemo(SIM_MSG_T UartMsg);
extern void WM_Entry_Task_Top_Most(void);

/* HAL */
extern void wm_config_key_gpio(void);
extern void wm_ev_power_key_state(UINT8 pwr_press_state);
extern void wm_ev_volup_key_state(UINT8 vup_press_state);
extern void wm_ev_voldown_key_state(UINT8 vdn_press_state);
extern void PowerKeyIntCBFunc(void);

/* LED control */
extern void wm_led_task_init(void);
extern int  wm_led_mutex_init(void);
extern int  wm_led_mutex_wait(void);
extern int  wm_led_mutex_release(void);
extern void wm_change_led_state(char led_red_intensity, char led_green_intensity, char led_blue_intensity, char blink_on);
extern void wm_set_led_state(char led_red_intensity, char led_green_intensity, char led_blue_intensity, char blink_freq, char blink_count);
extern void wm_set_led_single(WM_LEDS_INDICATOR led, BOOL blink_on);
extern void wm_aux_led_on(void);
extern void wm_aux_led_off(void);

/* Audio functions */
extern void wm_audio_init(void);
extern void wm_audio_aplifier_disabled(void);
extern void wm_audio_aplifier_enabled(void);
extern int wm_audio_play_vol_key_fb(void);
extern void wm_play_audio_min(void);
extern void wm_play_audio_max(void);
extern void wm_play_update_lang(void);
extern SC_STATUS WM_Audio_START(char* t_audio_path, char* t_audio_file, AUD_SampleRate sampleRate, AUD_Volume volume);
extern BOOL WM_Audio_STOP(void);
extern int mqtt_amount_decoder(char* json_data);
extern BOOL is_audio_playing(void);
extern BOOL wm_HPA_Play(SCmqttData* sub_data, const char* audio_file_names);
extern BOOL wm_LPA_Play(char* audio_file_names);
extern void SYS_Play_Audio(char* audio_file_names);
extern BOOL wm_TRNX_Play(int t_amount, const char* audio_file_names);

/* Power control */
extern void wm_pre_boot_init(void);
extern void wm_configure_module_hw(void);
extern void wm_sb_on(void);
extern void wm_powering_causes(void);
extern void wm_batt_anc(void);
extern void wm_nw_strength_anc(void);

/* Network functions */
extern INT16 wm_get_imei(void);
extern INT16 wm_get_sim_imsi(void);  
extern INT16 wm_get_sim_iccid(void);
extern INT16 wm_get_net_csq(void);
extern INT16 wm_get_net_cpsi(void);
extern INT16 wm_get_net_mnum(void);
extern INT16 wm_get_pdp_cgpaddr(void);
extern INT16 wm_get_pdp_cgdcont(void);
extern INT16 wm_set_pdp_cgdcont(SCApnParmSet* apnSet);
extern BOOL wm_get_operator(void);
extern SC_STATUS wm_reset_network(void);
extern SC_STATUS wm_radio_on(void);
extern SC_STATUS wm_radio_off(void);
extern SC_simcard_err_e wm_Simcardstatus(void);

/* Date and time RTC */
extern void get_rtc_date_time(t_rtc *t_val);
extern INT16 set_rtc_date_time(t_rtc *s_val);
extern void get_hi_res_timezone(void);
extern void set_hi_res_timezone(void);
extern void auto_update_date_time_enable(void);
extern void auto_update_date_time_disable(void);
extern int wm_ntp_update(char *ntp_server_addr,char* back_server, int timeout);
extern int wm_ntp_m_update(char* ntp_server_addrs[], int num_servers);
extern int wm_get_rtc_string(char* current_time);
extern int wm_update_rtc(void);
extern int wm_get_epoch_time_string_ms(char* epoch_time_str);
extern void sAPI_SetSysLocalTime(char* timeStr);

/* HTTP functions */
extern SC_STATUS wm_get_mqtt_dyn_certs(void);
extern int wm_mqtt_onboarding(void);
extern int wm_http_get_data(const char* url, char* response, int max_response_size);
extern int wm_http_post_data(const char* url, const char* post_data, const char* user_header_data, char* response, int max_response_size, int* t_post_status);
extern char *wm_build_http_header(const char *hdr_input);

/* File System */
extern void create_dir_struct(void);
extern void wm_list_curr_dir(char * dir_path);
extern BOOL wm_checkFilesInCSVFast(const char* csvFilePath);
extern BOOL wm_snd_file_validation(void);
extern void wm_update_config(void);
extern SC_STATUS wm_write_file_wb(const char* fileData, size_t dataLength, const char* filePath, WriteFileOption file_option);
extern SC_STATUS wm_append_file_ab(const char* fileData, size_t dataLength, const char* filePath);
extern SC_STATUS wm_read_file_rb(const char* filepath, char* buff);
extern UINT32 wm_get_file_size(const char* file_path);
extern SC_STATUS wm_rename_file(const char* oldpath, const char* newpath);
extern SC_STATUS wm_create_folders(char *Folders_json_str);
extern SC_STATUS wm_create_folder(char *t_folder);
extern SC_STATUS wm_bin_unpack(char *t_buff, int t_buff_len, BOOL reset);
extern int wm_rmDir_recu(const char *dirPath , int depth);
extern void wm_init_json_hooks(void);
extern void wm_intrmem_chk(int* t_total, int* t_free, int* t_used);
extern void wm_extrmem_chk(int *t_total,int *t_free,int *t_used);
extern int wm_get_disk_info(const char* dirPath, int* num_files, int* total_size);
extern void wm_get_partition_info(const char* ptable_name);
extern void wm_check_languages(void);
extern BOOL wm_check_language_files(const char *lang_name);
extern int wm_snd_OTA(char *t_lang, char *t_lang_url, char *t_SHA);
extern void wm_reset_all_lang_versions(void);
extern BOOL wm_folder_exists(const char *foldername);

/* File Download and Update */
extern int wm_local_app_update(void);
extern void wm_local_app_update_restart(void);
extern int wm_local_DFOTA(void);
extern void wm_local_DFOTA_restart(void);
extern int wm_download_file(char* t_URL, char* t_file_path, WriteFileOption file_option);
extern int wm_download_bin_packed(char* t_URL);
extern int wm_set_ssl_ver(int t_ssl_id, WM_SSL_SELECTION ssl_sel, BOOL SNI_EN);
extern WM_SSL_SELECTION wm_get_ssl_ver(int t_ssl_id);
extern void wm_init_time_sync(void);

/* Unzip */
extern void wm_unz_file_task(void);
extern void wm_unzip_async(void);
extern BOOL wm_unzip_file(char* path);

/* System and Utilities */
extern void wm_GetTaskStack(void);
extern void wm_display_cpu_heapm(void);
extern void wm_update_batt_startup(void);
extern void printi(int data);
extern int wm_file_remove(const char* t_path);
extern int wm_format_D(void);
extern INT16 wm_get_serialno(void);
extern unsigned long wm_get_task_mem(sTaskRef taskRef);
extern void wm_Get_modelid(char t_sdk_version[100]);
extern UINT32 wm_tick_elapsed(UINT32 *p_last_tick);
extern BOOL wm_tick_passed(UINT32 *p_last_tick, UINT32 threshold);
extern void wm_batt_test(BOOL blocking, UINT32 sleep_ms, const char *load_audio, BOOL stop_load_on_charge);
extern int wm_sdk_ver_read(void);
extern void wm_print_system_config(void);
extern void wm_trim_edge_slashes(const char *input, char *output, size_t output_size);
extern void wm_ReadSystemInfo(void);

/* JSON functions */
extern void wm_replaceOrAddStringInObject(cJSON* object, const char* key, const char* newValue);
extern void wm_replaceOrAddIntInObject(cJSON* object, const char* key, int newValue);
extern void wm_replaceOrAddBoolInObject(cJSON* object, const char* key, int newValue);
extern int wm_cJSON_CompareKey(const char* key, const char* expectedValue, cJSON* root);

/* System Information and Testing */
extern void wm_RAM_info(void);
extern void wm_CPU_info(void);
extern void sAPP_ReadSystemInfoDemo(void);

/* URC Message handling */
extern void urc_msg_init_task(void);
extern void sendMsgToUrc(SIM_MSG_T UartMsg);

/* SIM Task */
extern void wm_APP_SIMUrcTask(void);
extern void wm_app_sim_init(void);

/* Kernel Logs Functions */
extern void wm_start_NvrLogs(void);
extern void wm_stop_NvrLogs(void);

// Wm timer config
extern int wm_timer_init(void);
extern int File_Download_Task_Processor(char* t_URL);

// Health Matrx
extern char* wm_get_health_parm(void);
extern int wm_hlt_timer_update(int t_time);
extern int wm_icons_timer_update(int t_time);
extern BOOL wm_file_exists(const char* filename);

/* Debug Functions */
extern int wm_debug_port_init(void);
extern void wm_debug_write(UINT8* bt_buff, UINT32 datLen);
extern void wm_debug_packet(void);

/* Encryption Functions */
extern int wm_decrypt_cert(const char* enc_cert_data, size_t  cert_strlen, char* r_buff_A, const char* key);
extern int wm_encrypt_cert(const char* cert_data, size_t  cert_strlen, char* r_buff_A, const char* key);
extern INT32 wm_Base64Encode(UINT8* dst, INT32 dlen, INT32* olen, const UINT8* src, INT32 slen);
extern INT32 wm_Base64Decode(UINT8* dst, INT32 dlen, INT32* olen, const UINT8* src, INT32 slen);
extern int wm_sha256(char* filePath, char* hashString);
extern BOOL wm_compare_sha256(char *t_file_path, char *t_given_hash);
extern void wm_dyn_rootca_read(char* t_buf);
extern void wm_dyn_rootca_write(char* t_buf);
extern void wm_dyn_ccert_read(char* t_buf);
extern void wm_dyn_ccert_write(char* t_buf);
extern void wm_dyn_ckey_read(char* t_buf);
extern void wm_dyn_ckey_write(char* t_buf);
extern void wm_static_rootca_read(char* t_buf);
extern void wm_static_rootca_write(char* t_buf);
extern void wm_static_ccert_read(char* t_buf);
extern void wm_static_ccert_write(char* t_buf);
#define wm_token_read wm_static_ckey_read
#define wm_token_write wm_static_ckey_write
extern void wm_static_ckey_read(char* t_buf);
extern void wm_static_ckey_write(char* t_buf);
extern void wm_Calculate_MD5(unsigned char* input, unsigned int length, unsigned char output[16]);
extern void wm_Calculate_MD5_str(unsigned char* input, unsigned int length, char output[33]);
extern SC_STATUS wm_Calculate_MD5_File(const char *filepath, unsigned char final_digest[16]);
extern SC_STATUS wm_Calculate_MD5_File_str(const char *filepath, unsigned char final_digest_str[33]);
extern SC_STATUS wm_base64_process(WM_BASE64_T *params);
extern SC_STATUS wm_aes_process(WM_AES_T *params);
extern void wm_nvr_init(void);

/* Queue Msg Functions */
extern void wm_msg_UIPROC(WM_TASK_SELECTION q_msg);
extern void wm_msg_URC(WM_URC_TASK_SELECTION_CASE q_msg);

#endif