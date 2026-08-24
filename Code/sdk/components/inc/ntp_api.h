/* All rights reserved.
 *
 */

#ifndef _NTP_API_H_
#define _NTP_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief set NTP server
 *
 *
 * @param server  NTP server URL
 * 
 * @param back_server  backup NTP server URL
 * 
 * @return
 *      - true      success
 *      - false     fail
 */
bool ntpSetServer(const char *server, const char *back_server);

/**
 * @brief Get config NTP server URL
 *
 * @return
 *      - NTP server  URL
 */
char *ntpGetServer(void);

/**
 * @brief sync current time by NTP server
 *
 * @param timeout  Sync NTP server timeout unit ms
 * 
 * @return
 *      - true      success
 *      - false     fail
 */
bool ntpSyncServer(int timeout);

#ifdef __cplusplus
}
#endif
#endif // _NTP_API_H_ endif
