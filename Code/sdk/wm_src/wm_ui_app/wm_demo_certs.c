/**
  ******************************************************************************
  * @file    wm_demo_certs.c
  * @author  Walnut Medical
  * @brief   TLS credentials for the Common Gateway (WEGW) reference app.
  *
  *          The mutual-TLS set the earlier demo used against AWS IoT Core,
  *          restored unchanged: Amazon Root CA 1 as the trust anchor, plus a
  *          device certificate and its private key. The MQTT demo
  *          (wm_ui_mqtt.c) passes all three to wm_sdk_mqtt_config() when
  *          WM_MQTT_USE_TLS is enabled.
  *
  * @warning These are shared DEVELOPMENT credentials, identical in every image
  *          built from this tree, and the private key below is in plain sight.
  *          That is fine for bench testing against a test AWS IoT account. It
  *          is NOT how a fleet should ship: one leaked image would expose every
  *          unit, and AWS IoT policies could not tell devices apart. For
  *          production, provision a per-device certificate into the credential
  *          store and read it back with wm_sdk_storage_cred_read()
  *          (WM_SDK_STORAGE_CRED_ROOT_CA / _CLIENT_CERT / _CLIENT_KEY) instead of
  *          compiling credentials in - wm_sdk_mqtt_config() takes either source.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "wm_demo_certs.h"

/* Identifies which credential set is compiled in, so a unit can report the
 * certificates it is carrying (see the MQTT Status menu option). */
const char wm_demo_certs_id[] = WM_DEMO_CERTS_ID;

/* Amazon Root CA 1 - the trust anchor AWS IoT Core endpoints chain to. */
const char wm_cacert[MAX_MQTT_CRT_SIZE] =
{
"-----BEGIN CERTIFICATE-----\r\n"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\r\n"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\r\n"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\r\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\r\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\r\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\r\n"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\r\n"
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\r\n"
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\r\n"
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\r\n"
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\r\n"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\r\n"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\r\n"
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\r\n"
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\r\n"
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\r\n"
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\r\n"
"rqXRfboQnoZsG4q5WTP468SQvvG5\r\n"
"-----END CERTIFICATE-----\r\n"
};

/* Device client certificate (CN=AWS IoT Certificate), issued by AWS IoT. */
const char wm_clientcert[MAX_MQTT_CRT_SIZE] =
{
"-----BEGIN CERTIFICATE-----\r\n"
"MIIDWTCCAkGgAwIBAgIUYfMLDoiARwAD/CE5o92bgQtsjtUwDQYJKoZIhvcNAQEL\r\n"
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\r\n"
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDQyNzA2NTIx\r\n"
"MFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\r\n"
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAN8ccda14GZ3mFqUc2qc\r\n"
"cwL4IpcVPtQJZNEWicMWwiuPjgxVT9FS3snLkAYLZtLc5rApS68pOazpIlc1ciZh\r\n"
"iqaj9vFhvZ7wNE6VDXAftIdcJItJUQMtzAfEzQpcSE7mZvVFo+X+IJBJ/Vi3DgXv\r\n"
"YFg8bgsv8TRYwHnIgghiVyrVrT5/iTsYmbhmYWavbsSDbvJi0VZeixoW1lql3mFF\r\n"
"kASmArtkX+WvNbXsCA38OvbcLor5irrL8IsG6SPgF7zyPO4Ft245xZeBcMeNEG57\r\n"
"cAfcPL2+JyCgwYd4iqGjuVcl+eI10GH8bxNbjgVChcNreGcmPU2qwPvb5NANijKZ\r\n"
"G7cCAwEAAaNgMF4wHwYDVR0jBBgwFoAUcBsRYUxyIir3DKdjqkuLo/+c/XMwHQYD\r\n"
"VR0OBBYEFDrgetjVXlmvmvKzNCsR4IlWW/JWMAwGA1UdEwEB/wQCMAAwDgYDVR0P\r\n"
"AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQA2OPzYnqT2U/c+RLiDHN4nU/P1\r\n"
"jySA8dVyI9/NuDcYQpW2i8zdAc/MqRdwe/9V/zERDFe+92k69XVbDPD7nW5GIESs\r\n"
"SerwXQkOkjk3sqQ/ovPwNuLRDyUYizsRv7+LFc6l89E+W43P8B4QzolMV2UEFngL\r\n"
"OI874Zp/mNKYBuNwFMllF91lxzZuzbhkrGsIwBJuktZ7Po7H3TXV67hIX834go8Q\r\n"
"es5FWEhplimBoP/gdRzLZDVEl0lS/MXl7sYP6O237A8uvG28COzoUY3gfGlXx5pL\r\n"
"VvwbLxNXcVkGYD9y2msisA544pqG4nYWUW+QY28cg3yzsYh1nEpdT+Mmqojh\r\n"
"-----END CERTIFICATE-----\r\n"
};

/* Private key for wm_clientcert. See the warning in the file header. */
const char wm_clientkey[MAX_MQTT_CRT_SIZE] =
{
"-----BEGIN RSA PRIVATE KEY-----\r\n"
"MIIEpAIBAAKCAQEA3xxx1rXgZneYWpRzapxzAvgilxU+1Alk0RaJwxbCK4+ODFVP\r\n"
"0VLeycuQBgtm0tzmsClLryk5rOkiVzVyJmGKpqP28WG9nvA0TpUNcB+0h1wki0lR\r\n"
"Ay3MB8TNClxITuZm9UWj5f4gkEn9WLcOBe9gWDxuCy/xNFjAeciCCGJXKtWtPn+J\r\n"
"OxiZuGZhZq9uxINu8mLRVl6LGhbWWqXeYUWQBKYCu2Rf5a81tewIDfw69twuivmK\r\n"
"usvwiwbpI+AXvPI87gW3bjnFl4Fwx40QbntwB9w8vb4nIKDBh3iKoaO5VyX54jXQ\r\n"
"YfxvE1uOBUKFw2t4ZyY9TarA+9vk0A2KMpkbtwIDAQABAoIBABXV5NZMqo+cuM50\r\n"
"pO8ULLVnr4r1y1ZZLQLBn+319j7pJ8/RMaSc5az+PTQ6PW/5qqInCH82G6zujCvY\r\n"
"0TksZrN0bKCnHjXF8HiYYd5NXA+7VuBC5qjW/JbuDdsruO/v4Le0fxvtSSaV9zON\r\n"
"htPR364DDAoQQR0MapoAo6lKZpmMbUAiyjyASPU1TNO3jorHg6PBzK/wDAmmUZRF\r\n"
"iawQbJbQrgO5xPEPEK6MoywOXgV5ROYyunV5bqJ7i8zlZO2/UckqobACy+STGnXO\r\n"
"Ht+G3sxBv6lF1H+Ftg091ko1JSF2y9qIAP8lcT+F/CfntsPsCHy/khexY4vufIgm\r\n"
"JMuq8lECgYEA9ub0TJTuy4NBfY6s6LatQVR70sphOSFSmzVdiMohOmcFWw9Nx802\r\n"
"5zG7Mwg7teO0k7eAF9SDeJjfHrlxnSw+iQ8ck+7kN4GesurWKdtLYpe6Cy3nd5W8\r\n"
"QVhjO2Os5UVIRkbmAbSg1OrcLqCDCI3dYFJr1cA/CsqZOsde0bRHkPMCgYEA51UR\r\n"
"VCViV+02DRKqi7VO24FtUp0K+KjEa+U30A70u4DW+6HhFaC7SmF+bvpa+pWJsf8n\r\n"
"XLMy/byV28zO/qF0lFIAkr+TBj9vWkidfTgyby3bT9rQoMz6yfv/2u9HZgvNXVeg\r\n"
"IWkCgFYHU/ufiD1YmD/U33rDJp0aaeilQ3oZGy0CgYB8GdgfihiBCKdpYcZ9bhyl\r\n"
"or5eVEUL/zjTNL6f2+FZxqCFhaq1y+tVx8RtQS0qHpsMSIbE85dljYlQmyuFMdD4\r\n"
"+TSHEuN5Ftja23jsLwK0OslThy46CHRIcBHorxAfaXSLhmVj5BOXEbm8YgeazBvC\r\n"
"p4r2NJw7Y1wjlESPqUrziwKBgQCcxbvOqN8y82FUDrhhoeflPptWB3Ot/MHQ1TCz\r\n"
"4c7dW31WXuhpfdovmE7U6vnJJJqCdIa6ff8qkimFIhGN4uFcuw4EZaw/6bGPH/ML\r\n"
"L5COZCdiwMnuj7vOMue7+bkLYSg1//JXchDt5F9m/PmqnNhzpZ6gLVQf0Qxbhfyp\r\n"
"9A8bqQKBgQDkUMH012uYm+La/eQ9FNFT59iyR6u0FdL8L45tfa04273GDbtzb8dy\r\n"
"YrQO/Az5QT9D58S7e5qr8lLjSNy/wUI3ofRCLb4k61JNUiOvAqW9mjKeVWknTC2q\r\n"
"0KiV4weHYrxc5VHXTGYs5Ky9HJGvDiQxVv+ddTUJccQwgIboHy8HZA==\r\n"
"-----END RSA PRIVATE KEY-----\r\n"
};

