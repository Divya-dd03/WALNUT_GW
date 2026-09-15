/**
 * @file log.h
 * @brief Compile-time filtered logging (global + per-module level) - walnut
 *        port of the reference firmware's module/log/log.h (verbatim API).
 *
 * Usage (per file):
 *   #define LOG_TAG "ADC"
 *   #define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
 *   #include "module/log/log.h"
 *
 * Global ceiling: pass -DLOG_GLOBAL_LEVEL=3 (see levels below).
 * Effective level = min(LOG_MODULE_LEVEL, LOG_GLOBAL_LEVEL).
 *
 * Walnut backend: log_printf() (log_manager.c) forwards to wm_sdk_debug_print
 * (USB VCOM; unmuted by wm_logger_mode(TRUE) in weware_main).
 */

#pragma once
#ifndef WEWARE_LOG_H
#define WEWARE_LOG_H

#include <stdarg.h>

/*---------------------------------------------------------------
 * Log Levels
 *---------------------------------------------------------------*/
#define LOG_LEVEL_NONE   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4
#define LOG_LEVEL_TRACE  5

#ifndef LOG_GLOBAL_LEVEL
#define LOG_GLOBAL_LEVEL LOG_LEVEL_INFO
#endif

#ifndef LOG_MODULE_LEVEL
#define LOG_MODULE_LEVEL LOG_GLOBAL_LEVEL
#endif

#define LOG_EFFECTIVE_LEVEL \
    ((LOG_MODULE_LEVEL < LOG_GLOBAL_LEVEL) ? LOG_MODULE_LEVEL : LOG_GLOBAL_LEVEL)

/*---------------------------------------------------------------
 * Tag configuration (prefer per-file override)
 *---------------------------------------------------------------*/
#ifndef LOG_TAG
#define LOG_TAG "APP"
#endif

/*---------------------------------------------------------------
 * Backend
 *---------------------------------------------------------------*/
void log_printf(const char *fmt, ...);

/*---------------------------------------------------------------
 * Logging Macros (newline automatically)
 *---------------------------------------------------------------*/
#if LOG_EFFECTIVE_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) \
    log_printf("[%s] E: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if LOG_EFFECTIVE_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) \
    log_printf("[%s] W: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if LOG_EFFECTIVE_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) \
    log_printf("[%s] I: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if LOG_EFFECTIVE_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) \
    log_printf("[%s] D: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#if LOG_EFFECTIVE_LEVEL >= LOG_LEVEL_TRACE
#define LOG_TRACE(fmt, ...) \
    log_printf("[%s] T: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
#define LOG_TRACE(...) ((void)0)
#endif

#endif /* WEWARE_LOG_H */
