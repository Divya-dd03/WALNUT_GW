/**
  ******************************************************************************
  * @file    TTS_player_api.h
  * @author  Walnut Medical
  * @brief   Source file of key events.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

#ifndef _TTS_PLAYER_H_
#define _TTS_PLAYER_H_

#include <stdbool.h>
#include <stdint.h>

// extern "C" {

/**
 * \brief enums of tts player status
 */
typedef enum{
    PCM_PLAY_START,                 ///<status of start playing
    PCM_PLAY_DATA,                  ///<status of data transporting
    PCM_PLAY_END,                   ///<status of tts playing over
    PCM_PLAY_EXIT                   ///<status of tts task exit
}TTSPlayState_e;

/**
 * \brief enums for encoding formats TTS supported
 */
typedef enum{
    TTS_ENCODING_UTF8 = 0,          ///< encoding in UTF8 format
    TTS_ENCODING_GBK                ///< encoding in GBK format
}TTSTextEncoding_e;

/**
 * \brief set tts log print on CP Uart port
 */
#define tts_log(fmt, args...) do { CPUartLogPrintf("[tts]"fmt, ##args); } while(0)

/**
 * \brief redefine tts player callback fuction
 */
typedef void *ttsPlayerStateCB(TTSPlayState_e state, void *data, int size);

/**
 * \brief initialize tts player
 *
 * TTS player is designed as singleton.
 */
void ttsPlayerInit(void);

/**
 * \brief TTS play text
 *
 * This is asynchronized, that is it will return immediately after the play
 * event is sent.
 *
 * When \p len is -1, the actual text size will be determined by terminating
 * NUL character.
 *
 * \p text is the string to be played, encoded with \p encoding. Refer to
 * ml.h for the value of \p encoding. Not all encodings are supported.
 *
 * \param text      text to be played
 * \param len       text len, -1 for terminated with NUL
 * \param encoding  text encoding
 *      - 1         for GBK format
 *      - 0         for UTF8 format
 * \return
 *      - true on success
 *      - false on failed
 */
bool ttsPlayText(const void *text, int len, unsigned encoding);
bool ttsPlayTextEx(const void *text, int size, unsigned encoding, ttsPlayerStateCB cb);
/**
 * \brief stop tts play
 */
void ttsStop(void);

/**
 * \brief whether tts is playing
 *
 * \return
 *      - true if there are text in playing
 *      - false if no text in playing
 */
bool ttsIsPlaying(void);

/**
 * \brief output tts pcm data to output
 *
 * It is designed to be called by TTS engine, don't call it in application.
 * When parameter \p size is 0, it indicates the end of pcm data.
 *
 * \param text          pcm data, can't be NULL when \p size is not 0
 * \param size          pcm data byte count
 * \param encoding      encoding format
 * \param out           out data
 * \param out_size      out data size
 * \return
 *      - true if all pcm data are output
 *      - false on error, such as tts player is about to be stopped
 */
int ttsOutputPcmData(const void *text, int size, unsigned encoding, char *out, int out_size);

/**
 * \brief wait for tts player stop playing
 *
 * \param timeout      timeout set to seconds
 * \return
 *      - true if tts player stops successfully
 *      - false on error or timeout
 */
bool ttsPlayWaitFinish(uint32_t timeout);

/**
 * \brief change tts player task poriority
 *
 * \param newPriority      new poriority set to task
 * \return
 *      - true if tts player stops successfully
 *      - false on error or timeout
 */
bool ttsPlayTaskChangePriority(uint8_t newPriority);

/**
 * \brief destory the tts player task
 */
void ttsPlayerDestory(void);

// }

#endif
