/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-02-19 23:53:50
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#ifndef _PLATFORM_MUTEX_H_
#define _PLATFORM_MUTEX_H_

//#include "osa.h"
//#include "osa_old_api.h"
//#include "osa_mem.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "osi_api.h"

#define MQTT_RTOS_ARCH_TIMEOUT  (0xffffffffUL)
#define MQTT_RTOS_SEM_NULL      (NULL)
#define MQTT_RTOS_ERR_OK        (0)

typedef struct platform_mutex
{
    void * mutex;
} platform_mutex_t;

int platform_mutex_init(platform_mutex_t* m);
int platform_mutex_lock(platform_mutex_t* m);
int platform_mutex_unlock(platform_mutex_t* m);
int platform_mutex_destroy(platform_mutex_t* m);

#endif
