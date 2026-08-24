/**
 * @file sdk_functionality_ota.h
 * @brief Walnut compat shim: the reference includes this for
 *        sdk_app_package_open/write/close and sdk_ota_fbf_disable. The
 *        kernel sdk_ota.h exports all of them under the CG names with the
 *        CG SdkResult contract, so this just forwards.
 */

#ifndef WEWARE_SDK_FUNCTIONALITY_OTA_H
#define WEWARE_SDK_FUNCTIONALITY_OTA_H

#include "sdk_ota.h"

#endif /* WEWARE_SDK_FUNCTIONALITY_OTA_H */
