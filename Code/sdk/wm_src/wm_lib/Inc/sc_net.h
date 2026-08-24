/**
  ******************************************************************************
  * @file    sc_os.h
  * @brief   SC os enum and defs.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __SC_NET_H__
#define __SC_NET_H__

#include "sc_enum.h"
#include "sc_def.h"
#include "zx_api.h"

/* Define */

/* Enum */

/* Structure */

/* Functions */
int sAPI_NetworkGetCpsi(SCcpsiParm* wm_cpsi);
int sAPI_NetworkSetCfun(int CfunValue);
SC_STATUS sAPI_SysGetImei(char* ImeiValue);
SC_STATUS sAPI_SysGetImsi(char* ImsiValue);
SC_simcard_err_e wm_Simcardstatus(void);

/* NTP Functions */
int wm_ntp_update(char* ntp_server_addr, char* back_server, int timeout);

/* Read SDk Version */
int wm_sdk_ver_read(void);

/* UE Config */
int sAPI_SysConfigUE(void);

#endif