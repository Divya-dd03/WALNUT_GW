/**
 * @file time_utils.c
 * @brief RTC time utilities (UTC authoritative).
 */

#include "system/time_utils.h"
#include "sdk_platform.h"
#include "functionality/sdk_functionality_network.h"
#include <stdint.h>

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG          "TIME"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/
#define TIME_UTILS_MIN_VALID_UTC_UNIX  (1577836800u)  /* 2020-01-01 */
#define TIME_UTILS_MAX_YEAR            (2099u)
#define TIME_UTILS_SEC_PER_DAY         (86400u)

/*---------------------------------------------------------------
 * Internal Helpers
 *--------------------------------------------------------------*/

static inline BOOL is_leap_year(int y)
{
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

static inline int days_in_month(int y, int m)
{
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2)
        return mdays[m - 1] + is_leap_year(y);
    return mdays[m - 1];
}

static BOOL calendar_to_unix(int y, int mo, int d,
                              int h, int mi, int s,
                              UINT32 *out)
{
    if (!out)
        return FALSE;

    if (y < 1970 || y > (int)TIME_UTILS_MAX_YEAR ||
        mo < 1 || mo > 12 ||
        d  < 1 || d  > 31 ||
        h  < 0 || h  > 23 ||
        mi < 0 || mi > 59 ||
        s  < 0 || s  > 59)
        return FALSE;

    if (d > days_in_month(y, mo))
        return FALSE;

    UINT64 days = 0;
    for (int i = 1970; i < y; i++)
        days += is_leap_year(i) ? 366 : 365;
    for (int m = 1; m < mo; m++)
        days += days_in_month(y, m);
    days += (d - 1);

    UINT64 ts = days * TIME_UTILS_SEC_PER_DAY +
                (UINT64)h  * 3600ULL +
                (UINT64)mi * 60ULL +
                (UINT64)s;

    if (ts > UINT32_MAX)
        return FALSE;

    *out = (UINT32)ts;
    return TRUE;
}

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/* Do not add LOG statements here — causes deadlock. */
Result time_utils_get_time(TimeType type,
                           int *y, int *mo, int *d,
                           int *h, int *mi, int *s,
                           UINT32 *unix_out)
{
    wm_SdkNetworkTime t = {0};
    UINT32         unix_time = 0U;
    wm_SdkResult      sdk_result;

    if (type != TIME_TYPE_UTC && type != TIME_TYPE_LOCAL &&
        type != TIME_TYPE_UTC_UNIX && type != TIME_TYPE_LOCAL_UNIX)
        return RESULT_INVALID_PARAM;

    if (type == TIME_TYPE_UTC || type == TIME_TYPE_UTC_UNIX)
        sdk_result = wm_sdk_network_rtc_get_utc_time(&t);
    else
        sdk_result = wm_sdk_network_rtc_get_local_time(&t);

    if (sdk_result != WM_SDK_RESULT_SUCCESS)
        return RESULT_ERROR;

    if (t.year < 1970 || t.year > (int)TIME_UTILS_MAX_YEAR)
        t.year = 2000;

    if (!calendar_to_unix(t.year, t.month, t.day, t.hour, t.minute, t.second, &unix_time))
        return RESULT_ERROR;

    if (unix_out) *unix_out = unix_time;
    if (y)        *y  = t.year;
    if (mo)       *mo = t.month;
    if (d)        *d  = t.day;
    if (h)        *h  = t.hour;
    if (mi)       *mi = t.minute;
    if (s)        *s  = t.second;

    return RESULT_SUCCESS;
}

UINT64 time_utils_get_rtc_timestamp_yyyymmddhhmmss(void)
{
    int y, m, d, h, mi, s;

    if (time_utils_get_time(TIME_TYPE_LOCAL, &y, &m, &d, &h, &mi, &s, NULL) != RESULT_SUCCESS)
        return 0;

    return (UINT64)(y % 100) * 10000000000ULL +
           (UINT64)m         * 100000000ULL   +
           (UINT64)d         * 1000000ULL     +
           (UINT64)h         * 10000ULL       +
           (UINT64)mi        * 100ULL         +
           (UINT64)s;
}
