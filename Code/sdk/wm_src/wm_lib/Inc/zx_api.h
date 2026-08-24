/**
  ******************************************************************************
  * @file    zx_api.h
  * @brief   ZX API Set
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __ZX_API_H__
#define __ZX_API_H__
  /* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "typedef.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "osi_api.h"
#include "drv_serial.h"
#include "drv_keypad.h"
#include "drv_pwm.h"
#include "audio_api.h"
#include "fs_api.h"
#include "at_api.h"
#include "gpio_api.h"
#include "drv_iomux.h"
#include "drv_def.h"
#include "httpclient.h"
#include "mqttclient.h"
#include "base64.h"
#include "kernel_version.h"
#include "dirent-cis.h"
#include "zlib.h"
#include "contrib/minizip/zip.h"
#include "contrib/minizip/unzip.h"
#include "flash_api.h"
#include "cJSON.h"
#endif