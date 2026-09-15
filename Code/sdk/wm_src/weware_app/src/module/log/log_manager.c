/**
 * @file log_manager.c
 * @brief Log sink - walnut minimal backend for the reference log_manager API.
 *
 * The reference implementation buffers lines in a lock-free ring flushed by a
 * logger task (UART2 or debug sink). Walnut keeps the same API but formats and
 * forwards synchronously to wm_sdk_debug_print, matching how every other ported
 * module already logs. TODO(log): port the async ring + UART2 sink if needed.
 */

#include <stdio.h>
#include <stdarg.h>

#include "module/log/log_manager.h"
#include "module/log/log_config.h"
#include "wm_sdk_log.h"
#include "osi_api.h"

#define LOG_LINE_MAX 256

static BOOL      s_logger_ready = FALSE;
static LogOutput s_output       = LOG_OUTPUT_DEBUG;

void log_printf(const char *fmt, ...)
{
    char    buf[LOG_LINE_MAX];
    va_list args;
    int     n;

    if (!s_logger_ready || !fmt)
        return;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n <= 0)
        return;

    /* Walnut: both sinks map to the USB VCOM debug console for now. */
    (void)s_output;
    RTI_LOG("%s", buf);
}

Result logger_init(LogOutput output)
{
    if (output > LOG_OUTPUT_UART2)
        return RESULT_INVALID_PARAM;

    s_output       = output;
    s_logger_ready = TRUE;
    return RESULT_SUCCESS;
}

Result log_module_init(void)
{
    return logger_init(g_log_config.output);
}

UINT32 logger_get_dropped_log_total(void)
{
    /* Synchronous backend never queues, so nothing can be dropped. */
    return 0;
}

void logger_deinit(void)
{
    s_logger_ready = FALSE;
}
