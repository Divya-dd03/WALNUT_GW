/**
  ******************************************************************************
  * @file    utils.h
  * @author  WheelsEye
  * @brief   Common utility helpers for the weware application - walnut port
  *          of the reference firmware's common/utils.h (full API). Bodies are
  *          copied verbatim from the reference where possible so behaviour is
  *          identical.
  *
  *          Walnut adaptations:
  *          - sdk_get_ticks() already returns milliseconds, so the OS-tick
  *            conversion macros are identity/ms-based (reference: 200 ticks/s).
  *          - utils_sleep_ms() maps to sdk_task_sleep().
  *          - utils_get_uptime_seconds()/utils_time_to_unix() are walnut
  *            additions kept from the earlier port.
  ******************************************************************************
  */

#ifndef UTILS_H
#define UTILS_H

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "common/types.h"
#include "sdk_types.h"
#include "sdk_os.h"      /* sdk_get_ticks, sdk_task_sleep */
#include "module/module_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------
 * Type Definitions
 *--------------------------------------------------------------*/

/**
 * @brief Time type enumeration
 */
typedef enum
{
    TIME_TYPE_UTC = 0,          /**< GMT/UTC calendar fields */
    TIME_TYPE_LOCAL = 1,        /**< Local RTC calendar fields */
    TIME_TYPE_UTC_UNIX = 2,     /**< GMT/UTC Unix seconds */
    TIME_TYPE_LOCAL_UNIX = 3    /**< Local wall-clock seconds converted to Unix-style epoch */
} TimeType;

/**
 * @brief Enum to string mapping structure
 * @note Used for generic enum-to-string conversion
 */
typedef struct
{
    int value;              /**< Enum value */
    const char *string;     /**< String representation */
} EnumStringMap;

/** Key -> parameter-index map entry for config string parsing
 *  (reference: common/utils.h ConfigKeyMap). */
typedef struct {
    const char *key;
    int         index;
} ConfigKeyMap;

/*---------------------------------------------------------------
 * Configuration Macros
 *--------------------------------------------------------------*/

/**
 * @brief Systick conversion macros
 * @note  Walnut: sdk_get_ticks() returns MILLISECONDS since boot (wraps with
 *        UINT32), so the reference's 200-ticks/s conversions collapse to
 *        ms-based identities. Keep using these macros so reference code
 *        ports verbatim.
 */
#define OS_TICKS_PER_SECOND 1000u                                   /**< Walnut tick = 1 ms */
#define OS_TICKS_TO_MS(ticks) (ticks)                               /**< Convert OS ticks to milliseconds */
#define OS_TICKS_TO_SEC(ticks) ((ticks) / OS_TICKS_PER_SECOND)      /**< Convert OS ticks to seconds */
#define MS_TO_OS_TICKS(ms) (ms)                                     /**< Convert milliseconds to OS ticks */
#define SEC_TO_OS_TICKS(sec) ((sec) * OS_TICKS_PER_SECOND)          /**< Convert seconds to OS ticks */

/** Reference code reads the tick counter through SDK_GET_TICKS(). */
#ifndef SDK_GET_TICKS
#define SDK_GET_TICKS() sdk_get_ticks()
#endif

/**
 * @brief Sleep for specified milliseconds (reference: sAPI_TaskSleep(ticks))
 */
#define utils_sleep_ms(ms) \
    do { \
        sdk_task_sleep(ms); \
    } while (0)

/**
 * @brief Set state variable to a new state value
 * @note Uses do-while(0) pattern to ensure macro behaves like a single statement
 */
#define SET_STATE(state_var, new_state) do { (state_var) = (new_state); } while (0)

/**
 * @brief Handle state machine transition based on Result
 * @param state_var State variable to modify (lvalue)
 * @param result Result from state handler (RESULT_SUCCESS = transition, RESULT_BUSY = stay, RESULT_ERROR = error)
 * @param on_success State to set if result is RESULT_SUCCESS
 * @param on_error State to set if result is RESULT_ERROR
 * @param on_busy State to set if result is RESULT_BUSY (stay in current state)
 */
#define HANDLE_STATE(state_var, result, on_success, on_error, on_busy) \
    do { \
        Result _r = (result); \
        if (_r == RESULT_SUCCESS) \
            SET_STATE(state_var, on_success); \
        else if (_r == RESULT_ERROR) \
            SET_STATE(state_var, on_error); \
        else \
            SET_STATE(state_var, on_busy); \
    } while (0)

/*---------------------------------------------------------------
 * Enum/string X-macro helpers (reference: common/utils.h)
 *--------------------------------------------------------------*/

/* Internal macros for X-macro expansion - do not use directly */
#define ENUM_GEN_ENTRY(name, str) name,
#define ENUM_MAP_ENTRY(name, str) {name, str},

/** Generate enum type from X-macro list */
#define DEFINE_ENUM(enum_name, enum_list) \
    typedef enum { \
        enum_list(ENUM_GEN_ENTRY) \
    } enum_name;

/** Generate string map array from X-macro list (for header files) */
#define DEFINE_ENUM_STRING_MAP_INLINE(map_name, enum_list) \
    static const EnumStringMap map_name[] = { \
        enum_list(ENUM_MAP_ENTRY) \
    };

/** Convert enum value to string via a map generated above */
#define ENUM_TO_STRING(enum_value, map_array, default_str) \
    utils_enum_to_string((int)(enum_value), (map_array), sizeof(map_array)/sizeof((map_array)[0]), (default_str))

/*---------------------------------------------------------------
 * Time
 *--------------------------------------------------------------*/

/** Monotonic seconds since boot (RTOS tick derived; wraps with UINT32 ms). */
UINT32 utils_get_uptime_seconds(void);

/** Civil date/time -> Unix seconds (days-from-civil; valid for year >= 2000).
 *  Returns 0 when @p t is NULL or the date is out of range. */
UINT32 utils_time_to_unix(const SdkNetworkTime *t);

/** Milliseconds elapsed since @p start_ticks (from SDK_GET_TICKS()); wrap-safe. */
UINT32 utils_elapsed_ms_since(UINT32 start_ticks);

/** Current monotonic time in milliseconds (wraps with UINT32). */
UINT32 utils_monotonic_ms_now(void);

/** Milliseconds elapsed since @p start_ms (from utils_monotonic_ms_now()); wrap-safe. */
UINT32 utils_monotonic_ms_elapsed(UINT32 start_ms);

/*---------------------------------------------------------------
 * Memory Utility Functions
 *--------------------------------------------------------------*/

/** Safe memory copy with bounds checking; bytes copied or -1 on error. */
int utils_memcpy_safe(void *dest, size_t dest_size, const void *src, size_t src_size);

/** Safe memory set with bounds checking; bytes set or -1 on error. */
int utils_memset_safe(void *dest, size_t dest_size, int value, size_t count);

/*---------------------------------------------------------------
 * String Utility Functions
 *--------------------------------------------------------------*/

/** Safe strcpy (src must fit incl. terminator); length copied or -1. */
int utils_strcpy_safe(char *dest, size_t dest_size, const char *src);

/** Safe strcat (result must fit incl. terminator); new length or -1. */
int utils_strcat_safe(char *dest, size_t dest_size, const char *src);

/** Safe strncpy that always null-terminates; chars copied or -1. */
int utils_strncpy_safe(char *dest, const char *src, size_t dest_size);

/** Bounded strlen; length or -1 when unterminated within @p max_len. */
int utils_strlen_safe(const char *str, size_t max_len);

/** Trim leading/trailing spaces and tabs in place; returns the new start. */
char *utils_trim_whitespace(char *str);

/** Reentrant strtok (reference implementation). */
char *utils_strtok_r(char *str, const char *delim, char **saveptr);

/** Bounded strdup for tokenization; free with utils_free_tokenization().
 *  Returns NULL on NULL input, overlong input or allocation failure. */
char *utils_strdup_for_tokenization(const char *src);

/** Free a buffer from utils_strdup_for_tokenization(). */
void utils_free_tokenization(char *ptr);

/*---------------------------------------------------------------
 * Mathematical Utility Functions
 *--------------------------------------------------------------*/

/** NMEA ddmm.mmmm -> decimal degrees; 0.0 on bad input. */
double utils_ddmm_to_degrees(const char *ddmm);

/** Scale @p value and serialize big-endian into @p output (@p bytes wide). */
void utils_float_to_fixed_point(float value, int scale, char *output, int bytes);

/** Scale @p value and serialize big-endian into @p output (@p bytes wide). */
void utils_double_to_fixed_point(double value, int scale, char *output, int bytes);

/** XOR checksum over @p length bytes; 0 on bad input. */
uint8_t utils_calculate_checksum(const char *data, int length);

/** Haversine distance between two coordinates in meters; 0 on bad input. */
float utils_calculate_gps_distance(double lat1, double lon1, double lat2, double lon2);

/** Absolute course change in degrees, normalized to 0-180. */
float utils_calculate_angle_change(float course1, float course2);

/** CRC-16 (MODBUS style: poly 0xA001, init 0xFFFF). */
UINT16 utils_crc16_modbus(const void *data, size_t len);

/*---------------------------------------------------------------
 * Hex / binary conversion
 *--------------------------------------------------------------*/

/** Binary -> uppercase hex string (2 chars/byte + NUL); 1 ok, 0 fail. */
int utils_bytes_to_hex_str(const void *data, size_t data_len, char *hex_out, size_t hex_out_size);

/** Hex string -> binary; 1 ok (out_len set), 0 fail (bad char/odd len/overflow). */
int utils_hex_str_to_bytes(const char *hex_str, size_t hex_len, void *out, size_t out_cap, size_t *out_len);

/** Convert 15/16-digit IMEI string to 8 packed BCD-style bytes; 1 ok, 0 fail. */
int utils_imei_to_8bytes(const char *imei, char *output);

/*---------------------------------------------------------------
 * JSON helpers (minimal string extraction)
 *--------------------------------------------------------------*/

/** Extract string/number value for @p key; 0 ok, -1 not found, -2 too long. */
int utils_extract_json_string(const char *json, const char *key, char *out, size_t out_len);

/** Extract item @p index of array @p array_key; 0 ok, -1 not found, -2 too long. */
int utils_extract_json_array_item(const char *json, const char *array_key, int index, char *out, size_t out_len);

/*---------------------------------------------------------------
 * Validation Utility Functions
 *--------------------------------------------------------------*/

/** 1 when @p ip is a syntactically valid dotted-quad IPv4 string. */
int utils_validate_ip_address(const char *ip);

/** 1 when @p port is a valid TCP/UDP port (non-zero). */
int utils_validate_port(uint16_t port);

/** 1 when @p imei is exactly 15 digits. */
int utils_validate_imei(const char *imei);

/*---------------------------------------------------------------
 * Config parsing
 *--------------------------------------------------------------*/

/** Value part of a "key:value" token (after the colon), or the token itself. */
const char *utils_extract_config_value(const char *token);

/** Extract the key part of a "key:value" token into @p key_buf.
 *  @return TRUE when a non-empty key fitting the buffer was found. */
BOOL utils_config_parse_key(const char *token, char *key_buf, size_t key_buf_size);

/** Look @p key up in @p table (case-insensitive); -1 when not found. */
int utils_config_key_to_index(const char *key, const ConfigKeyMap *table, size_t count);

/*---------------------------------------------------------------
 * Module Communication
 *--------------------------------------------------------------*/

/**
 * @brief Route a command/response string to a module's message queue.
 * @param source_module Destination module (whose msg_q receives the message)
 * @param address Optional address/context copied into the message (may be NULL)
 * @param response Response text; for MODULE_ID_TCP it is framed as a WE packet
 *                 (type 38 string, or type 39 for "cmd-rsp,<hex>" BLE replies)
 * @param sender_module Module sending the response (0 defaults to MODULE_ID_CMD)
 */
void utils_route_response_to_module(ModuleId source_module, const char* address, const char* response, ModuleId sender_module);

/*---------------------------------------------------------------
 * Result helpers
 *--------------------------------------------------------------*/

/** TRUE when @p result is RESULT_SUCCESS. */
BOOL result_is_success(Result result);

/** TRUE when @p result is not RESULT_SUCCESS. */
BOOL result_is_error(Result result);

/*---------------------------------------------------------------
 * Enum to String
 *--------------------------------------------------------------*/

/** Look @p enum_value up in @p map; @p default_string when missing. */
const char *utils_enum_to_string(int enum_value, const EnumStringMap *map, size_t map_size, const char *default_string);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
