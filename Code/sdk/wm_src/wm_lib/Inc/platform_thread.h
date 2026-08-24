/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-02-19 23:53:50
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#ifndef _PLATFORM_THREAD_H_
#define _PLATFORM_THREAD_H_

//#include "osa.h"
//#include "osa_old_api.h"
//#include "osa_mem.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "osi_api.h"

typedef void* OSTaskRef;

typedef struct platform_thread
{
    OSTaskRef thread;
} platform_thread_t;

platform_thread_t *platform_thread_init(  char * name,
                                        void (*entry)(void *),
                                        void * param,
                                        unsigned int stack_size,
                                        unsigned int priority,
                                        unsigned int tick);
void platform_thread_startup(platform_thread_t* thread);
void platform_thread_stop(platform_thread_t* thread);
void platform_thread_start(platform_thread_t* thread);
void platform_thread_destroy(platform_thread_t* thread);

#endif
