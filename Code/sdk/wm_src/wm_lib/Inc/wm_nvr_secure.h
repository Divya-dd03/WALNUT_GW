/**
******************************************************************************
* @file    wm_nvr_secure.h
* @author  Walnut Medical
* @brief   Source file of NVR operation.
******************************************************************************
* @attention
*
* Copyright (c) 2025 Walnut Medical
* All rights reserved.
*
******************************************************************************
*/

#ifndef __WM_NVR_SECURE_H__
#define __WM_NVR_SECURE_H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "flash_api.h"
#include "sc_os.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Defines
******************************************************************************/
#define NVR_BUFF_SIZE 2048
#define NVR_BUFF_OFFSET 2048
#define NVR_PARTITION_NAME "user_cfg"

/*******************************************************************************
** External Functions
******************************************************************************/

/*******************************************************************************
** Type Definitions
******************************************************************************/

/*******************************************************************************
** Functions
******************************************************************************/

/* Certificate Set 1 */
void wm_dyn_rootca_read(char* t_buf);
void wm_dyn_rootca_write(char* t_buf);
void wm_dyn_ccert_read(char* t_buf);
void wm_dyn_ccert_write(char* t_buf);
void wm_dyn_ckey_read(char* t_buf);
void wm_dyn_ckey_write(char* t_buf);

/* Certificate Set 2 */
void wm_static_rootca_read(char* t_buf);
void wm_static_rootca_write(char* t_buf);
void wm_static_ccert_read(char* t_buf);
void wm_static_ccert_write(char* t_buf);
void wm_static_ckey_read(char* t_buf);
void wm_static_ckey_write(char* t_buf);

/* NVR Init */
void wm_nvr_init(void);

#ifdef __cplusplus
}
#endif


#endif