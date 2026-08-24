/* All rights reserved.
 *
 */

#ifndef _FOTA_API_H_
#define _FOTA_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUPDATE_APP_FILE_NAME       "D:/customer_app.bin"///<fote file name of app
#define FUPDATE_KERNEL_FILE_NAME    "D:/system_patch.bin"///<fote file name of kernel

/**
 * @brief update app from fota file in path
 *
 *
 * @param path                  fota file path
 * @return
 *      - true      success
 *      - false     fail
 */
bool fupdateAppSetReady(const char *path);

/**
 * @brief update kernel from fota file in path
 *
 *
 * @param path                  fota file path
 * @param kernel_version        kernel version
 * @return
 *      - true      success
 *      - false     fail
 */
bool fupdateKernelSetReady(const char *path, char *kernel_version);

/**
 * @brief check app bin file valid
 *
 *
 * @param fw_path     FW file path
 * @return
 *      - true      success
 *      - false     fail
 */
bool fupdateAppFileIsValid(const char *fw_path);

#ifdef __cplusplus
}
#endif
#endif
