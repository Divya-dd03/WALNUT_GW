/**
  ******************************************************************************
  * @file    wm_demo_certs.h
  * @author  Walnut Medical
  * @brief   TLS credentials for the Common Gateway (WEGW) reference app.
  *
  *          Declares the AWS IoT Core mutual-TLS set defined in
  *          wm_demo_certs.c. Read the @warning in that file before using these
  *          for anything but bench testing: they are shared development
  *          credentials with the private key compiled in.
  *
  *          Only declarations live here. The definitions are in the .c so the
  *          credentials exist once in the image - the earlier CERT_AND_KEY.h
  *          defined its certificate in the header, which duplicates the symbol
  *          in every translation unit that includes it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __WM_DEMO_CERTS__H__
#define __WM_DEMO_CERTS__H__

/*******************************************************************************
** Header Files
******************************************************************************/
#include "wm_global.h"   /* MAX_MQTT_CRT_SIZE */

#ifdef __cplusplus
extern "C"
{
#endif

/* Tag for the compiled-in credential set: region + owner of the AWS IoT thing
 * these certificates belong to. Bump it whenever the certificates are
 * replaced, so a running unit can say which set it is carrying. */
#define WM_DEMO_CERTS_ID    "CERTS_MUMB_JD"

extern const char wm_demo_certs_id[];

/* PEM, NUL-terminated, zero-padded to MAX_MQTT_CRT_SIZE. The fixed bound makes
 * an oversized replacement a compile error rather than a runtime surprise. */
extern const char wm_cacert[MAX_MQTT_CRT_SIZE];      /* Amazon Root CA 1      */
extern const char wm_clientcert[MAX_MQTT_CRT_SIZE];  /* device certificate    */
extern const char wm_clientkey[MAX_MQTT_CRT_SIZE];   /* device private key    */

#ifdef __cplusplus
}
#endif

#endif /* __WM_DEMO_CERTS__H__ */
