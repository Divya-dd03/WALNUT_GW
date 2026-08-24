/* All rights reserved.
 *
 */

#ifndef _AUDIO_API_H_
#define _AUDIO_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief audio play type
 */
typedef enum
{
    AUSTREAM_FILE,          ///< audio play like file
    AUSTREAM_STREAM,        ///< audio play like stream
} auPlayeType_t;

/**
 * \brief audio play state
 */
typedef enum{
    AUDIO_PlAY_START,
    AUDIO_PlAYING,
    AUDIO_PlAY_END,
    AUDIO_PLAY_STOP,
    AUDIO_PLAY_ERROR
}drvAudioPlayState_e;
typedef void (*AudioPlayStateCallback)(drvAudioPlayState_e state);

/**
 * \brief Turn PA on or off in this callback function
 */
typedef void (*SSP_SpeakerPA_CB) (uint32_t on);

/**
 * \brief audio stream format
 */
typedef enum
{
    AUSTREAM_FORMAT_UNKNOWN, ///< placeholder for unknown format
    AUSTREAM_FORMAT_PCM,     ///< raw PCM data
    AUSTREAM_FORMAT_WAVPCM,  ///< WAV, PCM inside
    AUSTREAM_FORMAT_MP3,     ///< MP3
    AUSTREAM_FORMAT_AMRNB,   ///< AMR-NB
    AUSTREAM_FORMAT_AMRWB,   ///< AMR_WB
    AUSTREAM_FORMAT_SBC,     ///< bt SBC
} auStreamFormat_t;

/**
 * \brief audio sample format
 */
typedef enum
{
    AUSAMPLE_FORMAT_S16, ///< int16_t, native endian (little endian)
} ausampleFormat_t;

/** audio samplerate enum*/
typedef enum
{
    AUDIOHAL_FREQ_8000HZ  =  8000,///<samplerate 8000
    AUDIOHAL_FREQ_11025HZ = 11025,///<samplerate 11025
    AUDIOHAL_FREQ_12000HZ = 12000,///<samplerate 12000
    AUDIOHAL_FREQ_16000HZ = 16000,///<samplerate 16000
    AUDIOHAL_FREQ_22050HZ = 22050,///<samplerate 22050
    AUDIOHAL_FREQ_24000HZ = 24000,///<samplerate 24000
    AUDIOHAL_FREQ_32000HZ = 32000,///<samplerate 32000
    AUDIOHAL_FREQ_44100HZ = 44100,///<samplerate 44100
    AUDIOHAL_FREQ_48000HZ = 48000,///<samplerate 48000    
    AUDIOHAL_FREQ_64000HZ = 64000,///<samplerate 64000    
    AUDIOHAL_FREQ_88200HZ = 88200,///<samplerate 88200    
    AUDIOHAL_FREQ_96000HZ = 96000,///<samplerate 96000   
} AUDIOHAL_FREQ_T;

/** MP3 playback event Value structure*/
typedef enum Mp3PlayEventValue {
        /** play out all for file or stream*/
        MP3_PLAYBACK_STATUS_ENDED = 0,
        /** indicate output device is opened*/
        MP3_PLAYBACK_STATUS_STARTED,
        /** reach file end*/
        MP3_PLAYBACK_STATUS_FILE_READED,

        MP3_PLAYBACK_STATUS_STREAM = 100,
        /** indicate mp3 data producer should slow down to avoid overrun*/
        MP3_STREAM_STATUS_SLOW_DOWN,
        /** indicate mp3 data producer should fast up to avoid underrun*/
        MP3_STREAM_STATUS_FAST_UP
    }Mp3PlayEventValue;


/** MP3 playback event Type structure*/
typedef enum Mp3PlayEventType {
    /** mp3 playback status, value with type <tt>Mp3PlayEventValue</tt>*/
    MP3_PLAYBACK_EVENT_STATUS,
    /** id3 header size, value with type <tt>int<tt> */
    MP3_FILE_EVENT_ID3OFFSET,
    /** sample rate, value with type <tt>int<tt>*/
    MP3_FILE_EVENT_SAMPLERATE,
    /** channel, value with type <tt>int<tt>*/
    MP3_FILE_EVENT_CHANNEL,
    /** bit rat, value with type <tt>int<tt>*/
    MP3_FILE_EVENT_BITRATE,
}Mp3PlayEventType;

/** WAV playback event Type structure*/
typedef enum {
    WAV_PLAY_EVENT_START,
    WAV_PLAY_EVENT_STOP
}WavPlayEventType;

/** WAV playback event callback prototype*/
typedef void(*WavPlaybackEventCallback)(WavPlayEventType, int);

/** mp3 playback event callback prototype*/
typedef void(*Mp3PlaybackEventCallback)(Mp3PlayEventType, int);

/*recording callback*/
typedef void (*recordingCallback)(uint8_t *pcm_data, uint32_t pcm_data_size);

/** mp3 player config infomation prototype*/
typedef struct Mp3PlayConfigInfo {
    /** mp3 file playback option in bitmap format, refer <tt>AUDIO_PLAY_OPTION</tt> in acm_audio_def.h*/
    int option;
    /** mp3 file block size, 0 for frame mode*/
    int block_size;
    /** user registers callback to get playback event such as file end etc */
    Mp3PlaybackEventCallback trigger;
}Mp3PlayConfigInfo;

typedef enum {
    AUDIO_USE_INTERNAL_CODEC,
    AUDIO_USE_INTERNAL_PWM,
    AUDIO_USE_EXTERNAL_CODEC
}AudioCodecType_e;

typedef enum {
    AUDIO_CODEC_INPUT_FORMAT_I2S,
    AUDIO_CODEC_INPUT_FORMAT_PCM
}AudioCodeFormat_e;

typedef struct {
    char codec_name[16];
    AudioCodeFormat_e format;
}AudioCodecCfg_t;

typedef uint32_t Mp3PlaybackHandle;

typedef enum AmrPlaybackEventType {
    /** status related event type, refer @ref AmrFileStatus for event value*/
    AMR_PLAYBACK_EVENT_STATUS,
    /** amr rate event type, value ranges from 0 to 7*/
    AMR_PLAYBACK_EVENT_RATE,
    /** file name request, value with type <tt>char*<tt>*/
    AMR_PLAYBACK_EVENT_FILENAME,
}AmrPlaybackEventType;

/** amr playback handle, held and used by amr playback user*/
typedef uint32_t AmrPlaybackHandle;

typedef void(*AmrPlaybackEventCallback)(AmrPlaybackEventType, int);
typedef struct AmrPlaybackConfigInfo {
    /** playback option in bitmap format, refer @ref AUDIO_PLAY_OPTION in @ref acm_audio_def.h */
    int64_t option;
    /** block size for file only, 0 for frame mode*/
    int block_size;
    /** user registers callback to get playback event such as file end etc */
    AmrPlaybackEventCallback listener;
}AmrPlaybackConfigInfo;

typedef AmrPlaybackConfigInfo AmrFileConfigInfo;

/** Start wav file playback .
    * @param [in] file_name     wav file name
    * @param [in] cb            callback for wavplayeventtype
    * @returns                  true on success, false on failure
    */
bool auPlayWavFile(const char *file_name, 
                    WavPlaybackEventCallback cb);

/** Start PCM stream playback with dedicated option.
    * @param [in] samplerate    audio playing samplerate
    * @param [in] channels      audio playing channels
    * @param [in] format        audio playing format
    * @param [in] data          audio data
    * @param [in] size          size of audio data
    */
void auPlayPcmStream(uint32_t samplerate, 
                            uint32_t channels, 
                            ausampleFormat_t format,
                            char *data, 
                            uint32_t size);

/** Start mp3 file playback with dedicated option.
    * @param [in] samplerate    audio playing samplerate
    * @param [in] channels      audio playing channels
    * @param [in] format        audio playing format
    * @param [in] data          audio data
    * @param [in] size          size of audio data
    * @param [in] cb            callback for wavplayeventtype
    */
void auPlayPcmStreamEx(uint32_t samplerate, 
                            uint32_t channels, 
                            ausampleFormat_t format,
                            char *data, 
                            uint32_t size, WavPlaybackEventCallback cb);

/** Start mp3 file playback with dedicated option.
    * @param [in] file_name     mp3 file name
    * @param [in] configInfo    mp3 file playback option
    * @param [in,out] handle    mp3 file playback handle
    * @returns                  non-zero on failure
    */
int mp3PlayStart(const char* file_name, const Mp3PlayConfigInfo* configInfo, Mp3PlaybackHandle* handle);

/** Stop mp3 file playback with handle.
    * @param [in] handle        mp3 file playback handle
    * @param [in] drain         drain or not
    * @returns                  non-zero on failure
    */
int mp3PlayStop(Mp3PlaybackHandle handle, int drain);

/** Config audio codec.
    * @param [in] type       AUDIO_USE_INTERNAL_CODEC NOT SUPPORT IN 1602x
    * @param [in] cfg        ext codec config
    * @returns                  non-zero on failure
    */
int auCodecConfig(AudioCodecType_e type, AudioCodecCfg_t *cfg);

/** Config audio volume.
    * @param [in] volume       AUDIO_USE_INTERNAL_CODEC range[0-12]
    *                          AUDIO_USE_EXTERNAL_CODEC range[0-255]
    * @returns                  None
    */
void auPlayVolumeSet(uint32_t volume);

int amrPlayStart(const char* file_name, const AmrPlaybackConfigInfo* config, AmrPlaybackHandle* handle);

/** Write an amr data block to an amr playback handle.
 * @param [in] handle                   amr playback handle
 * @param [in] data                     pcm buffer address
 * @param [in] size                     pcm buffer size
 * @returns error code in int, non-zero on failure
 */
int amrPlayBuffer(AmrPlaybackHandle handle, const uint8_t* data, uint32_t size);

int amrPlayStop(AmrPlaybackHandle handle, int drain);

int32_t auPlaySoftGainSet(int32_t gain);

void auPlayPcmStop(uint32_t timeout);


int mp3StreamPlay(uint8_t* mp3_buffer, int size, Mp3PlaybackEventCallback cb, Mp3PlaybackHandle* handle);

void AudioPlayStateSetCallback(AudioPlayStateCallback callback);

void SSP_BindSpeakerPA_CB(SSP_SpeakerPA_CB cb);

int wavRecordingStart(const char* file_name);

int pcmRecordingStart(recordingCallback callback);

int recordingStop(void);
#ifdef __cplusplus
}
#endif
#endif
