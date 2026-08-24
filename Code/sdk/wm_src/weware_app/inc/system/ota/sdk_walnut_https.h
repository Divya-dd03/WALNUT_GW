/**
 * @file sdk_walnut_https.h
 * @brief WALNUT HTTPS backend - ops table getter (sdk_walnut_https.c).
 */

#ifndef SDK_WALNUT_HTTPS_H
#define SDK_WALNUT_HTTPS_H

#include "ota/sdk_functionality_https.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Walnut backend ops table - pass to sdk_platform_register_https_ops(). */
const SdkHttpsFunctionalityOps *sdk_walnut_get_https_functionality_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_WALNUT_HTTPS_H */
