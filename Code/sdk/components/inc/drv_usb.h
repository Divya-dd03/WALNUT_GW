/* All rights reserved.
 *
 */

#ifndef _DRV_USB_H_
#define _DRV_USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <osi_api.h>

/**
 * @brief enums for usb interface types
 */
typedef enum
{
    DRV_USB_INTERFACE_SERIAL = (0x01 << 0),
    DRV_USB_INTERFACE_SERIAL_EXT = (0x01 << 1),  // CCID INTERFACE
    DRV_USB_INTERFACE_RNDIS = (0x01 << 2),
    DRV_USB_INTERFACE_DIAG = (0x01 << 3),
    DRV_USB_INTERFACE_SULOG = (0x01 << 4),
    DRV_USB_INTERFACE_ALL = 0xFFFF,
}drvUsbInterface_e;

/**
 * @brief enums for usb status types
 */
typedef enum
{
    DRV_USB_STATUS_CABLE_OUT = 0,
    DRV_USB_STATUS_CABLE_IN,
    DRV_USB_STATUS_RESET,
    DRV_USB_STATUS_GET_DEVICE_DESC,
    DRV_USB_STATUS_SET_CONFIGURATION,
    DRV_USB_STATUS_SUSPEND,
    DRV_USB_STATUS_RESUME,
    DRV_USB_STATUS_ENUM_DONE
}drvUsbStatus_e;

typedef void (*statusChangeNotify_f)(drvUsbStatus_e status, void *param);

/**
 * @brief Enable usb feature
 */
void drvUsbEnable(void);

/**
 * @brief Disable usb feature
 */
void drvUsbDisable(void);

/**
* @brief destory usb feature
*/
void drvUsbDestory(void);

/**
* @brief change usb callback notify
* 
* @param cb             callback set for usb
* @param param          callback parameters set to usb
*/
void drvUsbSetStatusChangeNotify(statusChangeNotify_f cb, void *param);

/**
* @brief get usb status
* 
* @return USB current status
*/
drvUsbStatus_e drvUsbGetStatus(void);

/**
* @brief re-enumerate the usb device
* 
* @param interfaces          new interfaces set to usb
*/
bool drvUsbInterfaceConfig(uint32_t interfaces);

#ifdef __cplusplus
}
#endif

#endif // _DRV_USB_H_
