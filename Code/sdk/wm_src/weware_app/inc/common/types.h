/**
 * @file types.h
 * @brief Common types for the weware application - walnut port of the
 *        reference firmware's common/types.h.
 *
 * Walnut adaptations:
 * - BOOL / UINT8..UINT64 / INT64 come from the walnut SDK (sdk_types.h ->
 *   typedef.h) instead of the SIMCOM/Quectel compat headers.
 * - TRUE/FALSE mirror sc_def.h (identical token sequence, so a later
 *   include of wm_global.h is a benign redefinition).
 * - sdk_task_ref_t (reference: sdk_platform.h) is the walnut opaque task
 *   handle (sdk_task_create returns void *).
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "sdk_types.h"

#ifndef TRUE
#define TRUE true
#endif
#ifndef FALSE
#define FALSE false
#endif

/** Opaque task handle (reference: sdk_platform.h sdk_task_ref_t). */
typedef void *sdk_task_ref_t;

/**
 * @brief Result type for functions that can fail (reference common/types.h).
 * @note Values shared with SdkResult are numerically identical
 *       (SUCCESS 0, ERROR -1, TIMEOUT -2, INVALID_PARAM -3, ...), so an
 *       SdkResult-returning module init can be used through a Result-typed
 *       function pointer (same convention the reference uses for casts).
 */
typedef enum
{
    RESULT_SUCCESS = 0,
    RESULT_ERROR = -1,
    RESULT_TIMEOUT = -2,
    RESULT_INVALID_PARAM = -3,
    RESULT_NOT_INITIALIZED = -4,
    RESULT_ALREADY_INITIALIZED = -5,
    RESULT_OUT_OF_MEMORY = -6,
    RESULT_NOT_FOUND = -7,
    RESULT_BUSY = -8,
    RESULT_NOT_SUPPORTED = -9
} Result;

/**
 * @brief Version information structure
 */
typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t build;
} Version;

#endif /* TYPES_H */
