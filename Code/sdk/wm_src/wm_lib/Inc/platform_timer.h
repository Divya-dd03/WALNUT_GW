/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-10 22:18:32
 * @LastEditTime : 2020-01-11 01:20:44
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#ifndef _PLATFORM_TIMER_H_
#define _PLATFORM_TIMER_H_

//#include "osa.h"
//#include "osa_old_api.h"
//#include "osa_mem.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "osi_api.h"

#define MQTT_RTOS_MS_ONE_TICK              (5) /*one tick denote 5ms*/
#define MQTT_RTOS_TICK_PER_SECOND          (200) /*200 tick for 1s*/

typedef struct platform_timer {
    uint32_t tick; /*os tick*/
    uint32_t time_out; /*ms*/
} platform_timer_t;

void platform_timer_init(platform_timer_t* timer);
void platform_timer_cutdown(platform_timer_t* timer, unsigned int timeout);
char platform_timer_is_expired(platform_timer_t* timer);
int platform_timer_remain(platform_timer_t* timer);
unsigned long platform_timer_now(void);
void platform_timer_usleep(unsigned long usec);
void platform_timer_msleep(uint32_t ms);

#endif
