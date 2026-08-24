/**
  ******************************************************************************
  * @file    sc_audio.h
  * @brief   SC audio enum and defs.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __SC_AUDIO_H__
#define __SC_AUDIO_H__

#include "sc_enum.h"
#include "sc_def.h"
#include "zx_api.h"

/* Define */
#define VOL_MAX       (11)
#define VOL_MIN       (0)
#define WM_MP3_BLOCK_SIZE (1024 * 40) // 12KB
#define WM_AUD_BUFF_LIMIT (1024 * 100)
#define AUD_LOOP_LIMIT (30*200)/5 // 30s

/* Functions */
/* Audio Functions */
AUD_Volume sAPI_AudioGetVolume(void);
void sAPI_AudioSetVolume(AUD_Volume volume);
void sAPP_AudioTaskInit(void);
BOOL sAPI_AudioPlay(char* file, BOOL direct, BOOL isSingle);
BOOL sAPI_AudioStop(void);
UINT8 sAPI_AudioStatus(void);
BOOL sAPI_AudioPlay_buff(char* file);
#endif