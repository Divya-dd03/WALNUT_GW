#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include "common/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read time from RTC in requested format (LOCAL/UTC, calendar/unix). */
Result time_utils_get_time(TimeType time_type,
                           int *year, int *month, int *day,
                           int *hour, int *min, int *sec,
                           UINT32 *unix_timestamp);

/* RTC local timestamp in YYMMDDHHMMSS (for status/diagnostics). */
UINT64 time_utils_get_rtc_timestamp_yyyymmddhhmmss(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_UTILS_H */
