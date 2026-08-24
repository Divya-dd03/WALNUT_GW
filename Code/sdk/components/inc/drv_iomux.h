#ifndef _DRV_IOMUX_H_
#define _DRV_IOMUX_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void drvGpioPinMuxConfig(const unsigned int * mfp_cfgs);

void mfp_config(const unsigned int  *mfp_cfgs);

#ifdef __cplusplus
}
#endif

#endif /*_DRV_IOMUX_H_*/
