/* All rights reserved.
 *
 */

#ifndef _AT_API_H_
#define _AT_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief send AT cmd and get response
 *
 *
 * @param inStr                  AT cmd strings
 * @param timeOut                timeout in seconds
 * @param okFmt                  format for success, not check when it comes to "NULL"
 * @param okFlag                 switch to check "OK" in returned string
 *      - 1             wait for "OK" and then return
 *      - 0             not wait for "OK", return
 * @param errFmt                 format for error, not check when it comes to "NULL"
 * @param outStr                 returned string
 * @param outLen                 length of returned string
 * 
 * @return
 *      - 0             success
 *      - OTHERS        fail
 */
int atCmdSendWaitResp(char *inStr, int timeOut, char *okFmt, int okFlag, char *errFmt, char *outStr, int outLen);

#ifdef __cplusplus
}
#endif
#endif
