/**
  ******************************************************************************
  * @file    utils.c
  * @author  WheelsEye
  * @brief   Common utility helpers - walnut ports of the reference firmware's
  *          common/utils.c (full API). Function bodies are copied verbatim
  *          from the reference except: tokenization memory comes from
  *          wm_sdk_memory_alloc/free instead of malloc/free, tick reads use
  *          wm_sdk_get_ticks() (already milliseconds), and TCP response packets
  *          are built via module/tcp/tcp_ops.h.
  ******************************************************************************
  */
/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>    /* strcasecmp */

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "common/utils.h"
#include "module/module_manager.h"
#include "module/tcp/tcp_ops.h"
#include "common/queue_manager.h"

/*---------------------------------------------------------------
 * SDK Platform Abstraction Layer
 *--------------------------------------------------------------*/
#include "wm_global.h"
#include "wm_sdk_os.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "UTILS"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Time Utility Functions
 *--------------------------------------------------------------*/

UINT32 utils_get_uptime_seconds(void)
{
    return wm_sdk_get_ticks() / 1000u;
}

UINT32 utils_time_to_unix(const wm_SdkNetworkTime *t)
{
    int      y;
    int      era;
    unsigned yoe, doy, doe;
    long     days;

    if (!t || t->year < 2000 || t->month < 1 || t->month > 12 ||
        t->day < 1 || t->day > 31)
        return 0;

    y   = (int)t->year - (t->month <= 2 ? 1 : 0);
    era = y / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153u * (unsigned)(t->month + (t->month > 2 ? -3 : 9)) + 2u) / 5u
          + (unsigned)t->day - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    days = (long)era * 146097L + (long)doe - 719468L;

    return (UINT32)((long long)days * 86400LL +
                    (long long)t->hour * 3600 +
                    (long long)t->minute * 60 +
                    (long long)t->second);
}

UINT32 utils_elapsed_ms_since(UINT32 start_ticks)
{
    UINT32 now = SDK_GET_TICKS();

    // Handle tick wraparound (32-bit counter)
    UINT32 ticks = (now >= start_ticks)
                   ? (now - start_ticks)
                   : (UINT32_MAX - start_ticks + now + 1);

    return OS_TICKS_TO_MS(ticks);
}

UINT32 utils_monotonic_ms_now(void)
{
    return OS_TICKS_TO_MS(SDK_GET_TICKS());
}

UINT32 utils_monotonic_ms_elapsed(UINT32 start_ms)
{
    UINT32 now = utils_monotonic_ms_now();
    return (now >= start_ms) ? (now - start_ms) : (UINT32_MAX - start_ms + now + 1u);
}

/*---------------------------------------------------------------
 * Memory Utility Functions
 *--------------------------------------------------------------*/

int utils_memcpy_safe(void *dest, size_t dest_size, const void *src, size_t src_size)
{
    if (!dest || !src || dest_size == 0 || src_size == 0)
    {
        LOG_ERROR("memcpy_safe failed: invalid parameters");
        return -1;
    }

    size_t copy_size = (src_size < dest_size) ? src_size : dest_size;
    memcpy(dest, src, copy_size);

    return (int)copy_size;
}

int utils_memset_safe(void *dest, size_t dest_size, int value, size_t count)
{
    if (!dest || dest_size == 0 || count == 0)
    {
        LOG_ERROR("memset_safe failed: invalid parameters");
        return -1;
    }

    size_t set_size = (count < dest_size) ? count : dest_size;
    memset(dest, value, set_size);

    return (int)set_size;
}

/*---------------------------------------------------------------
 * String Utility Functions
 *--------------------------------------------------------------*/

/* src must be null-terminated; strlen is used and strcpy is used. */
int utils_strcpy_safe(char *dest, size_t dest_size, const char *src)
{
    if (!dest || !src || dest_size == 0)
    {
        LOG_ERROR("strcpy_safe failed: invalid parameters");
        return -1;
    }
    size_t src_len = strlen(src);
    if (src_len >= dest_size)
    {
        LOG_ERROR("strcpy_safe failed: source string too long (%zu >= %zu)", src_len, dest_size);
        return -1;
    }

    strcpy(dest, src);
    return (int)src_len;
}

int utils_strcat_safe(char *dest, size_t dest_size, const char *src)
{
    if (!dest || !src || dest_size == 0)
    {
        LOG_ERROR("strcat_safe failed: invalid parameters");
        return -1;
    }

    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);

    /* Check for integer overflow in length calculation */
    if (dest_len > SIZE_MAX - src_len || dest_len + src_len >= dest_size)
    {
        LOG_ERROR("strcat_safe failed: concatenated string would exceed buffer (%zu + %zu >= %zu)",
                 dest_len, src_len, dest_size);
        return -1;
    }

    strcat(dest, src);
    return (int)(dest_len + src_len);
}

/**
 * @brief Safe strncpy wrapper that always null-terminates destination
 * @note Replaces the common pattern: strncpy(dest, src, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0';
 */
int utils_strncpy_safe(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0)
    {
        LOG_ERROR("strncpy_safe failed: invalid parameters");
        return -1;
    }

    /* Safe copy: copy at most dest_size-1 bytes, then null-terminate */
    /* Don't use strlen() as src might not be null-terminated (e.g., network data) */
    size_t copy_len = dest_size - 1;

    /* Copy up to dest_size-1 bytes */
    strncpy(dest, src, copy_len);
    dest[copy_len] = '\0';  /* Always null-terminate */

    /* Return actual copied length (excluding null terminator) */
    /* Find actual length by checking for null terminator in source or copied data */
    size_t actual_len = 0;
    while (actual_len < copy_len && src[actual_len] != '\0')
    {
        actual_len++;
    }

    return (int)actual_len;
}

/** Max length for config/tokenization strings; avoids unbounded strlen on non-null-terminated input. */
#define UTILS_STRDUP_TOKENIZATION_MAX_LEN 2048

/**
 * @brief Allocate and copy a string for tokenization (bounded length, then memcpy).
 * @note Caller must free the returned pointer using utils_free_tokenization()
 */
char *utils_strdup_for_tokenization(const char *src)
{
    if (!src)
        return NULL;

    /* Bounded length: stop at max or first null to avoid overflow from non-null-terminated input */
    size_t len = 0;
    while (len < UTILS_STRDUP_TOKENIZATION_MAX_LEN && src[len] != '\0')
        len++;
    if (len == UTILS_STRDUP_TOKENIZATION_MAX_LEN && src[len] != '\0')
    {
        LOG_ERROR("strdup_for_tokenization: string too long or not null-terminated (max=%u)",
                  (unsigned)UTILS_STRDUP_TOKENIZATION_MAX_LEN);
        return NULL;
    }

    char *copy = (char *)wm_sdk_memory_alloc((UINT32)(len + 1));
    if (!copy)
    {
        LOG_ERROR("strdup_for_tokenization failed: allocation failed (len=%zu)", len);
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

void utils_free_tokenization(char *ptr)
{
    if (ptr)
        wm_sdk_memory_free(ptr);
}

int utils_strlen_safe(const char *str, size_t max_len)
{
    if (!str)
    {
        LOG_ERROR("strlen_safe failed: invalid string parameter");
        return -1;
    }

    size_t len = 0;
    while (len < max_len && str[len] != '\0')
    {
        len++;
    }

    if (len == max_len && str[len] != '\0')
    {
        LOG_ERROR("strlen_safe failed: string too long (exceeds max_len=%zu)", max_len);
        return -1;
    }

    return (int)len;
}

/*---------------------------------------------------------------
 * Mathematical Utility Functions
 *--------------------------------------------------------------*/

double utils_ddmm_to_degrees(const char *ddmm)
{
    if (ddmm == NULL || *ddmm == '\0')
    {
        LOG_DEBUG("ddmm_to_degrees: invalid input (NULL or empty)");
        return 0.0;
    }

    const char *p = ddmm;
    unsigned long accum = 0;
    int digits = 0;

    while (*p && *p != '.' && *p >= '0' && *p <= '9')
    {
        accum = accum * 10 + (unsigned long)(*p - '0');
        p++;
        digits++;
    }

    int deg = 0;
    unsigned long min_scaled = 0;

    if (digits >= 3)
    {
        /* Format: DDDMM.mmmm or DDDMM */
        deg = (int)(accum / 100);
        min_scaled = (accum - (unsigned long)deg * 100) * 10000UL;
    }
    else if (digits >= 2)
    {
        /* Format: DDMM.mmmm or DDMM */
        deg = (int)(accum / 100);
        min_scaled = (accum - (unsigned long)deg * 100) * 10000UL;
    }
    else if (digits > 0)
    {
        /* Format: DMM.mmmm or DMM - treat as minutes only */
        deg = 0;
        min_scaled = accum * 10000UL;
    }
    else
    {
        /* No digits before decimal point */
        LOG_DEBUG("ddmm_to_degrees: invalid format (no digits before decimal)");
        return 0.0;
    }

    if (*p == '.')
    {
        p++;
        unsigned long frac = 0;
        int i = 0;

        while (*p && i < 4 && *p >= '0' && *p <= '9')
        {
            frac = frac * 10 + (unsigned long)(*p - '0');
            p++;
            i++;
        }

        /* Pad fractional part to 4 digits */
        while (i < 4)
        {
            frac *= 10;
            i++;
        }
        min_scaled += frac;
    }

    double minutes = (double)min_scaled / 10000.0;
    return (double)deg + minutes / 60.0;
}

void utils_float_to_fixed_point(float value, int scale, char *output, int bytes)
{
    if (!output || bytes <= 0)
    {
        LOG_ERROR("float_to_fixed_point failed: invalid parameters");
        return;
    }

    int fixed_value = (int)(value * scale);
    int i;

    for (i = 0; i < bytes; i++) {
        output[i] = (char)((fixed_value >> ((bytes - i - 1) * 8)) & 0xFF);
    }
}

void utils_double_to_fixed_point(double value, int scale, char *output, int bytes)
{
    if (!output || bytes <= 0)
    {
        LOG_ERROR("double_to_fixed_point failed: invalid parameters");
        return;
    }

    int fixed_value = (int)(value * scale);
    int i;

    for (i = 0; i < bytes; i++) {
        output[i] = (char)((fixed_value >> ((bytes - i - 1) * 8)) & 0xFF);
    }
}

uint8_t utils_calculate_checksum(const char *data, int length)
{
    if (!data || length <= 0)
    {
        LOG_ERROR("calculate_checksum failed: invalid parameters");
        return 0;
    }

    uint8_t checksum = 0;
    int i;

    for (i = 0; i < length; i++) {
        checksum ^= (uint8_t)data[i];
    }

    return checksum;
}

/*---------------------------------------------------------------
 * String Utility Functions (continued)
 *--------------------------------------------------------------*/

char *utils_strtok_r(char *str, const char *delim, char **saveptr)
{
    char *token_start;
    char *token_end;

    if (!delim || !saveptr) {
        LOG_ERROR("strtok_r failed: invalid parameters");
        return NULL;
    }

    /* If str is NULL, continue from saved position */
    if (str == NULL) {
        str = *saveptr;
        if (str == NULL) {
            return NULL;
        }
    }

    /* Skip leading delimiters */
    while (*str != '\0' && strchr(delim, *str) != NULL) {
        str++;
    }

    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    token_start = str;

    /* Find end of token */
    token_end = token_start;
    while (*token_end != '\0' && strchr(delim, *token_end) == NULL) {
        token_end++;
    }

    if (*token_end != '\0') {
        /* Replace delimiter with null terminator */
        *token_end = '\0';
        *saveptr = token_end + 1;
    } else {
        /* End of string */
        *saveptr = NULL;
    }

    return token_start;
}

/*---------------------------------------------------------------
 * Mathematical Utility Functions (continued)
 *--------------------------------------------------------------*/

float utils_calculate_gps_distance(double lat1, double lon1, double lat2, double lon2)
{
    /* Validate input coordinates */
    if (lat1 < -90.0 || lat1 > 90.0 || lat2 < -90.0 || lat2 > 90.0 ||
        lon1 < -180.0 || lon1 > 180.0 || lon2 < -180.0 || lon2 > 180.0)
    {
        LOG_ERROR("calculate_gps_distance failed: invalid coordinates (lat1=%.6f, lon1=%.6f, lat2=%.6f, lon2=%.6f)",
                 lat1, lon1, lat2, lon2);
        return 0.0f;
    }

    const double R = 6371000.0; /* Earth's radius in meters */
    const double PI = 3.14159265359;

    double dlat = (lat2 - lat1) * PI / 180.0;
    double dlon = (lon2 - lon1) * PI / 180.0;

    double a = sin(dlat/2) * sin(dlat/2) +
               cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
               sin(dlon/2) * sin(dlon/2);

    double c = 2 * atan2(sqrt(a), sqrt(1-a));

    return (float)(R * c);
}

/**
 * @brief Calculate angle change between two course values
 * @return Angle change in degrees (0-180)
 */
float utils_calculate_angle_change(float course1, float course2)
{
    /* Normalize courses to 0-360 range */
    while (course1 < 0.0f) course1 += 360.0f;
    while (course1 >= 360.0f) course1 -= 360.0f;
    while (course2 < 0.0f) course2 += 360.0f;
    while (course2 >= 360.0f) course2 -= 360.0f;

    float diff = fabsf(course2 - course1);
    if (diff > 180.0f) {
        diff = 360.0f - diff;
    }
    return diff;
}

/*---------------------------------------------------------------
 * Validation Utility Functions
 *--------------------------------------------------------------*/

int utils_validate_ip_address(const char *ip)
{
    if (!ip)
    {
        LOG_DEBUG("validate_ip_address: NULL input");
        return 0;
    }

    // Simple IPv4 validation
    int dots = 0;
    int digits = 0;
    const char *p = ip;

    while (*p)
    {
        if (*p == '.')
        {
            if (digits == 0 || digits > 3)
            {
                LOG_DEBUG("validate_ip_address: invalid octet length (%d) at position %d", digits, (int)(p - ip));
                return 0;
            }
            dots++;
            digits = 0;
        }
        else if (*p >= '0' && *p <= '9')
        {
            digits++;
            if (digits > 3)
            {
                LOG_DEBUG("validate_ip_address: octet too long at position %d", (int)(p - ip));
                return 0;
            }
        }
        else
        {
            LOG_DEBUG("validate_ip_address: invalid character '%c' at position %d", *p, (int)(p - ip));
            return 0;
        }
        p++;
    }

    int valid = (dots == 3 && digits > 0 && digits <= 3);
    if (!valid)
    {
        LOG_DEBUG("validate_ip_address: invalid format (dots=%d, final_digits=%d)", dots, digits);
    }
    return valid;
}

int utils_validate_port(uint16_t port)
{
    int valid = (port > 0 && port <= 65535);
    if (!valid)
    {
        LOG_DEBUG("validate_port: invalid port %u", port);
    }
    return valid;
}

int utils_validate_imei(const char *imei)
{
    if (!imei)
    {
        LOG_DEBUG("validate_imei: NULL input");
        return 0;
    }

    // IMEI should be 15 digits
    int len = 0;
    const char *p = imei;

    while (*p && *p >= '0' && *p <= '9')
    {
        len++;
        p++;
    }

    int valid = (len == 15 && *p == '\0');
    if (!valid)
    {
        LOG_DEBUG("validate_imei: invalid length (%d) or non-digit character", len);
    }
    return valid;
}

/**
 * @brief Convert IMEI string to 8-byte format (hex digit pairs to bytes)
 * @param imei IMEI string (15 or 16 hex digits; 15 is padded with leading 0)
 * @param output Output buffer for 8 bytes (must have at least 8 bytes)
 * @return 1 on success, 0 on failure
 */
int utils_imei_to_8bytes(const char *imei, char *output)
{
    if (!imei || !output)
        return 0;

    size_t len = strlen(imei);
    if (len != 15 && len != 16)
        return 0;

#define UTILS_HEX_VAL(c) \
    ((c) >= '0' && (c) <= '9' ? (c) - '0' : (c) >= 'A' && (c) <= 'F' ? (c) - 'A' + 10 : (c) >= 'a' && (c) <= 'f' ? (c) - 'a' + 10 : 0xFF)

    int out_idx = 0;
    if (len == 15)
    {
        unsigned char v2 = (unsigned char)UTILS_HEX_VAL(imei[0]);
        if (v2 == 0xFF)
            return 0;
        output[out_idx++] = (char)((0 << 4) | v2);
    }

    for (; out_idx < 8; out_idx++)
    {
        size_t str_idx = (len == 15) ? (size_t)(out_idx * 2 - 1) : (size_t)(out_idx * 2);
        if (str_idx + 1 >= len)
            return 0;
        unsigned char v1 = (unsigned char)UTILS_HEX_VAL(imei[str_idx]);
        unsigned char v2 = (unsigned char)UTILS_HEX_VAL(imei[str_idx + 1]);
        if (v1 == 0xFF || v2 == 0xFF)
            return 0;
        output[out_idx] = (char)((v1 << 4) | v2);
    }

#undef UTILS_HEX_VAL
    return 1;
}

/**
 * @brief CRC-16 (MODBUS style: poly 0xA001, init 0xFFFF)
 */
UINT16 utils_crc16_modbus(const void *data, size_t len)
{
    UINT16 crc = 0xFFFF;
    const unsigned char *p = (const unsigned char *)data;
    while (len--)
    {
        crc ^= (UINT16)*p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (UINT16)((crc >> 1) ^ 0xA001) : (UINT16)(crc >> 1);
    }
    return crc;
}

/**
 * @brief Convert binary bytes to uppercase hex string (2 hex chars per byte)
 */
int utils_bytes_to_hex_str(const void *data, size_t data_len, char *hex_out, size_t hex_out_size)
{
    if (!hex_out || hex_out_size < data_len * 2 + 1)
        return 0;
    if (data_len > 0 && !data)
        return 0;

    static const char hex_digit[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < data_len; i++)
    {
        hex_out[i * 2]     = hex_digit[(p[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_digit[p[i] & 0x0F];
    }
    hex_out[data_len * 2] = '\0';
    return 1;
}

int utils_hex_str_to_bytes(const char *hex_str, size_t hex_len, void *out, size_t out_cap, size_t *out_len)
{
    if (!out_len)
        return 0;
    *out_len = 0;
    if (hex_len == 0)
        return 1;
    if (!hex_str || !out)
        return 0;
    if ((hex_len % 2u) != 0u)
        return 0;
    size_t nbyte = hex_len / 2u;
    if (nbyte > out_cap)
        return 0;

#define UTILS_HEX_NIBBLE(c) \
    ((c) >= '0' && (c) <= '9' ? (unsigned)((c) - '0') : \
     (c) >= 'A' && (c) <= 'F' ? (unsigned)((c) - 'A' + 10) : \
     (c) >= 'a' && (c) <= 'f' ? (unsigned)((c) - 'a' + 10) : 16u)

    UINT8 *dst = (UINT8 *)out;
    for (size_t i = 0; i < nbyte; i++) {
        unsigned v1 = UTILS_HEX_NIBBLE((unsigned char)hex_str[i * 2]);
        unsigned v2 = UTILS_HEX_NIBBLE((unsigned char)hex_str[i * 2 + 1]);
        if (v1 >= 16u || v2 >= 16u) {
#undef UTILS_HEX_NIBBLE
            return 0;
        }
        dst[i] = (UINT8)((v1 << 4) | v2);
    }
#undef UTILS_HEX_NIBBLE
    *out_len = nbyte;
    return 1;
}

/**
 * @brief Extract string value from JSON by key
 */
int utils_extract_json_string(const char *json, const char *key, char *out, size_t out_len)
{
    if (!json || !key || !out || out_len == 0)
    {
        LOG_ERROR("extract_json_string failed: invalid parameters");
        return -1;
    }

    char pattern[64];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
    {
        LOG_ERROR("extract_json_string failed: key too long");
        return -1;
    }

    const char *pos = strstr(json, pattern);
    if (!pos)
    {
        /* Try without quotes in case key is not quoted */
        if (snprintf(pattern, sizeof(pattern), "%s", key) >= (int)sizeof(pattern))
        {
            LOG_ERROR("extract_json_string failed: key too long");
            return -1;
        }
        pos = strstr(json, pattern);
        if (!pos)
        {
            LOG_DEBUG("extract_json_string: key '%s' not found", key);
            return -1;
        }
    }

    /* Find the colon after the key */
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos)
    {
        LOG_DEBUG("extract_json_string: colon not found after key '%s'", key);
        return -1;
    }

    pos++;
    /* Skip whitespace */
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n')
        pos++;

    /* Check if value is a quoted string */
    if (*pos == '"')
    {
        pos++;
        const char *end = strchr(pos, '"');
        if (!end)
        {
            LOG_DEBUG("extract_json_string: unterminated quoted string for key '%s'", key);
            return -1;
        }

        size_t len = end - pos;
        if (len >= out_len)
        {
            LOG_WARN("extract_json_string: value too long for key '%s' (%zu >= %zu)", key, len, out_len);
            return -2;
        }

        memcpy(out, pos, len);
        out[len] = '\0';
        return 0;
    }
    else
    {
        /* Try to extract unquoted value (number or other) */
        const char *start = pos;
        while (*pos && *pos != ',' && *pos != '}' && *pos != ']' && *pos != ' ' && *pos != '\t' && *pos != '\r' && *pos != '\n')
            pos++;

        size_t len = pos - start;
        if (len == 0 || len >= out_len)
        {
            LOG_DEBUG("extract_json_string: invalid unquoted value length for key '%s'", key);
            return -1;
        }

        memcpy(out, start, len);
        out[len] = '\0';
        return 0;
    }
}

int utils_extract_json_array_item(const char *json, const char *array_key, int index, char *out, size_t out_len)
{
    if (!json || !array_key || !out || out_len == 0)
    {
        LOG_ERROR("extract_json_array_item failed: invalid parameters");
        return -1;
    }

    if (index < 0)
    {
        LOG_ERROR("extract_json_array_item failed: negative index (%d)", index);
        return -1;
    }

    /* If array_key is empty, assume json is the array itself */
    const char *array_start = json;

    if (strlen(array_key) > 0)
    {
        /* Find the array */
        char pattern[64];
        if (snprintf(pattern, sizeof(pattern), "\"%s\"", array_key) >= (int)sizeof(pattern))
        {
            LOG_ERROR("extract_json_array_item failed: array_key too long");
            return -1;
        }
        array_start = strstr(json, pattern);
        if (!array_start)
        {
            LOG_DEBUG("extract_json_array_item: array_key '%s' not found", array_key);
            return -1;
        }

        /* Find the opening bracket */
        array_start = strchr(array_start, '[');
        if (!array_start)
        {
            LOG_DEBUG("extract_json_array_item: opening bracket not found for key '%s'", array_key);
            return -1;
        }
    }
    else
    {
        /* If array_key is empty, find first '[' */
        array_start = strchr(json, '[');
        if (!array_start)
        {
            LOG_DEBUG("extract_json_array_item: opening bracket not found");
            return -1;
        }
    }

    array_start++; /* Skip '[' */

    /* Skip to the requested index */
    int current_index = 0;
    const char *item_start = array_start;

    while (current_index < index)
    {
        /* Find next comma or closing bracket */
        const char *comma = strchr(item_start, ',');
        const char *bracket = strchr(item_start, ']');

        if (!bracket)
        {
            LOG_DEBUG("extract_json_array_item: closing bracket not found at index %d", current_index);
            return -1;
        }

        if (comma && comma < bracket)
        {
            item_start = comma + 1;
            current_index++;
        }
        else
        {
            LOG_DEBUG("extract_json_array_item: not enough items (requested index %d, found %d)", index, current_index);
            return -1; /* Not enough items */
        }
    }

    /* Find the end of this item */
    const char *item_end = strchr(item_start, ',');
    const char *bracket_end = strchr(item_start, ']');

    if (!bracket_end)
    {
        LOG_DEBUG("extract_json_array_item: closing bracket not found for item at index %d", index);
        return -1;
    }

    size_t len;
    if (item_end && item_end < bracket_end)
    {
        len = item_end - item_start;
    }
    else
    {
        len = bracket_end - item_start;
    }

    if (len >= out_len)
    {
        LOG_WARN("extract_json_array_item: item at index %d too long (%zu >= %zu)", index, len, out_len);
        return -2;
    }

    memcpy(out, item_start, len);
    out[len] = '\0';
    return 0;
}

/*---------------------------------------------------------------
 * Result Utility Functions
 *--------------------------------------------------------------*/

BOOL result_is_success(Result result)
{
    return (result == RESULT_SUCCESS);
}

BOOL result_is_error(Result result)
{
    return (result != RESULT_SUCCESS);
}

/*===============================================================
 * Enum to String Utility Functions
 *==============================================================*/

const char *utils_enum_to_string(int enum_value, const EnumStringMap *map, size_t map_size, const char *default_string)
{
    if (!map || map_size == 0)
    {
        LOG_DEBUG("enum_to_string: invalid map (enum_value=%d)", enum_value);
        return (default_string ? default_string : "NULL");
    }

    for (size_t i = 0; i < map_size; i++)
    {
        if (map[i].value == enum_value)
        {
            return (map[i].string ? map[i].string : "NULL");
        }
    }

    LOG_DEBUG("enum_to_string: enum_value %d not found in map", enum_value);
    return (default_string ? default_string : "UNKNOWN");
}

/*---------------------------------------------------------------
 * Configuration Parsing Utility Functions
 *--------------------------------------------------------------*/

const char *utils_extract_config_value(const char *token)
{
    if (!token)
    {
        return NULL;
    }

    const char *colon = strchr(token, ':');
    if (colon != NULL)
    {
        return colon + 1;  /* Return part after colon */
    }
    return token;  /* No prefix, return as-is */
}

BOOL utils_config_parse_key(const char *token, char *key_buf, size_t key_buf_size)
{
    const char *colon;
    size_t key_len;

    if (!token || !key_buf || key_buf_size == 0)
        return FALSE;

    colon = strchr(token, ':');
    if (colon == NULL)
        return FALSE;

    key_len = (size_t)(colon - token);
    if (key_len == 0 || key_len >= key_buf_size)
        return FALSE;

    memcpy(key_buf, token, key_len);
    key_buf[key_len] = '\0';
    return TRUE;
}

int utils_config_key_to_index(const char *key, const ConfigKeyMap *table, size_t count)
{
    size_t i;

    if (!key || !table)
        return -1;

    for (i = 0; i < count; i++) {
        if (table[i].key && strcasecmp(key, table[i].key) == 0)
            return table[i].index;
    }
    return -1;
}

char *utils_trim_whitespace(char *str)
{
    if (!str)
    {
        LOG_DEBUG("trim_whitespace: NULL input");
        return NULL;
    }

    /* Trim leading whitespace */
    while (*str == ' ' || *str == '\t')
    {
        str++;
    }

    /* Trim trailing whitespace */
    size_t len = strlen(str);
    if (len > 0)
    {
        char *end = str + len - 1;
        while (end > str && (*end == ' ' || *end == '\t'))
        {
            *end-- = '\0';
        }
    }

    return str;
}

/*---------------------------------------------------------------
 * Module Communication Utility Functions
 *--------------------------------------------------------------*/

void utils_route_response_to_module(ModuleId source_module, const char* address, const char* response, ModuleId sender_module)
{
    if (!response || strlen(response) == 0) {
        LOG_DEBUG("route_response_to_module: empty response, skipping");
        return;
    }

    const ModuleConfig *source_config = module_manager_get_config(source_module);
    if (!source_config || !source_config->enabled || !source_config->msg_q) {
        LOG_DEBUG("route_response_to_module: source module %d not available or disabled",
                 source_module);
        return;
    }

    /* Use sender_module if provided, otherwise default to MODULE_ID_CMD for backward compatibility */
    ModuleId actual_sender = (sender_module != 0) ? sender_module : MODULE_ID_CMD;

    ModuleMessage msg = {0};
    msg.source_module = actual_sender;
    msg.destination_module = source_module;

    if (address) {
        if (utils_strncpy_safe(msg.address, address, sizeof(msg.address)) < 0) {
            LOG_WARN("route_response_to_module: address too long, truncated");
        }
    }

    if (source_module == MODULE_ID_TCP) {
        /* TCP queue expects a WE packet. BLE command replies arrive as
         * "OK,cmd-rsp,<hex>" - for those, send only the decoded binary as a
         * type-39 packet. Everything else stays a type-38 string packet. */
        const char *marker = strstr(response, "cmd-rsp,");
        int pkt_len = 0;
        if (marker) {
            const char *hex = marker + 8;  /* past "cmd-rsp," */
            int hex_len = 0;
            while (hex[hex_len] &&
                   ((hex[hex_len] >= '0' && hex[hex_len] <= '9') ||
                    (hex[hex_len] >= 'a' && hex[hex_len] <= 'f') ||
                    (hex[hex_len] >= 'A' && hex[hex_len] <= 'F'))) {
                hex_len++;
            }
            pkt_len = tcp_ble_cmd_response_packet_create(msg.message, (int)sizeof(msg.message), hex, hex_len);
            if (pkt_len == 0) {
                LOG_WARN("route_response_to_module: cmd-rsp hex invalid/empty, dropping");
                return;
            }
        } else {
            int plen = (int)strlen(response);
            pkt_len = tcp_command_response_packet_create(msg.message, (int)sizeof(msg.message), response, plen);
            if (pkt_len == 0) {
                LOG_WARN("route_response_to_module: failed to create TCP command response packet");
                return;
            }
        }
        msg.data_len = (UINT32)pkt_len;
    } else {
        int n = utils_strncpy_safe(msg.message, response, sizeof(msg.message));
        if (n < 0) {
            LOG_WARN("route_response_to_module: response copy failed");
            return;
        }
        msg.data_len = (UINT32)n;
    }

    Result push_result = queue_push(source_config->msg_q, &source_config->msg_q_config, &msg);
    if (push_result != RESULT_SUCCESS) {
        LOG_WARN("route_response_to_module: queue_push failed (result=%d)", push_result);
    }
}
