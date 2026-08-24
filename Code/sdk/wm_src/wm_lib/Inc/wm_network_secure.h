/**
******************************************************************************
* @file    wm_network_secure.h
* @author  Walnut Medical
* @brief   Header file of Network Functions.
******************************************************************************
* @attention
*
* Copyright (c) 2022 Walnut Medical.
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_NETWORK_SECURE_H__
#define __WM_NETWORK_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_imei_list.h"
#include "wm_global.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** External Functions
******************************************************************************/
extern int vatResName(const char* rsp, const char* name);

/*******************************************************************************
** Type Definitions
******************************************************************************/


/*******************************************************************************
** Functions
******************************************************************************/
/* Radio Functions */
SC_STATUS wm_reset_network(void);
SC_STATUS wm_radio_on(void);
SC_STATUS wm_radio_off(void);

/* RTC Functions */
void get_rtc_date_time(t_rtc *t_val);
INT16 set_rtc_date_time(t_rtc *s_val);
void auto_update_date_time_enable(void);
void auto_update_date_time_disable(void);
void get_hi_res_timezone(void);
void set_hi_res_timezone(void);
int wm_get_rtc_string(char* current_time);
int wm_update_rtc(void);

/* Network Functions */
INT16 wm_get_imei(void);
INT16 wm_get_sim_imsi(void);
INT16 wm_get_sim_iccid(void);
INT16 wm_get_net_csq(void);
INT16 wm_get_net_cpsi(void);
INT16 wm_get_net_mnum(void);
INT16 wm_get_pdp_cgpaddr(void);
INT16 wm_get_pdp_cgdcont(void);
INT16 wm_set_pdp_cgdcont(SCApnParmSet* apnSet);
BOOL wm_get_operator(void);
INT16 wm_get_serialno(void);

/* System Network Functions */
void sc_module_init(void);

#ifdef __cplusplus
}
#endif

#endif /*__WM_NETWORK_SECURE_H__*/