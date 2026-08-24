/**
  ******************************************************************************
  * @file    weware_version.h
  * @author  WheelsEye
  * @brief   Version information for the weware application. FW/HW versions
  *          are 4 hex digits and go into the login packet as two bytes each.
  ******************************************************************************
  */

#ifndef WEWARE_VERSION_H
#define WEWARE_VERSION_H

/* Device Version Information */
#define FIRMWARE_VERSION "0100"
#define HARDWARE_VERSION "0106"

/* Application Version Information */
#define APP_VERSION "1.0.0"

/* Config storage blob version (config/config.c; bump on any stored-config
 * struct layout change - mismatched files are deleted and recreated).
 * v5: NetworkConfig added as a stored module (2026-08-21). */
#define CONFIG_FILE_VERSION 5


#endif /* WEWARE_VERSION_H */
