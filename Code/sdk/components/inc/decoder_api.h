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
uint32_t decoder_initial(void);

int decoder_image(uint8_t *img_buffer, int width, int height);

uint32_t decoder_getResultLen(void);

int decoder_getResult(uint8_t *result);

void decoder_setMirror(int tag);

#ifdef __cplusplus
}
#endif
#endif
