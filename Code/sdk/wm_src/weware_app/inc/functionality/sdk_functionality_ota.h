/**
 * @file sdk_functionality_ota.h
 * @brief Walnut compat shim: the reference includes this for its app-package
 *        and FBF calls. The kernel wm_sdk_ota.h exports all of them as
 *        wm_sdk_app_package_open/write/close and wm_sdk_ota_fbf_disable with
 *        the CG wm_SdkResult contract, so this just forwards.
 */

#ifndef WEWARE_SDK_FUNCTIONALITY_OTA_H
#define WEWARE_SDK_FUNCTIONALITY_OTA_H

#include "wm_sdk_ota.h"

#endif /* WEWARE_SDK_FUNCTIONALITY_OTA_H */
