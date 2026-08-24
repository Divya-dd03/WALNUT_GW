/* All rights reserved.
 *
 */
#ifndef _DRV_CCID_H_
#define _DRV_CCID_H_

#include "osi_compiler.h"
#include "osi_api.h"

OSI_EXTERN_C_BEGIN

#include <stddef.h>
#include <stdint.h>

/**
 * \brief ccid driver
 */
typedef struct drv_ccid drvCCID_t;


#define CCID_RDRTOPC_NOTIFY_SLOT_CHANGE 0x50
#define CCID_RDRTOPC_HARDWARE_ERROR 0x51

#define ABDATA_SIZE 0x04F6

// refer to ccid spec 6.3
/**
 * \brief ccid notification structure
 */
struct ccid_notification {
	uint8_t bMessageType;
	union
	{
		uint8_t buf[3];
		struct {
			uint8_t bSlot;
			uint8_t bSeq;
			uint8_t bHardwareErrorCode;
		};
	};
} __attribute__((packed));

/**
 * \brief ccid bulk in header structure
 */
struct ccid_bulk_in_header {
	uint8_t bMessageType;
	uint32_t dwLength;
	uint8_t bSlot;
	uint8_t bSeq;
	uint8_t bStatus;
	uint8_t bError;
	uint8_t bSpecific;
	uint8_t abData[ABDATA_SIZE];
	uint32_t bSizeToSend;
} __attribute__((packed));

/**
 * \brief ccid bulk out header structure
 */
struct ccid_bulk_out_header {
	uint8_t bMessageType;
	uint32_t dwLength;
	uint8_t bSlot;
	uint8_t bSeq;
	uint8_t bSpecific_0;
	uint8_t bSpecific_1;
	uint8_t bSpecific_2;
	uint8_t APDU[ABDATA_SIZE];
} __attribute__((packed));

/**
 * \brief ccid Event type structure
 */
typedef enum
{
    DRV_CCID_EVENT_BULKOUT_ARRIVED = (1 << 0),  ///< Received new data
    DRV_CCID_EVENT_OPEN = (1 << 5),        ///< open by host
    DRV_CCID_EVENT_CLOSE = (1 << 6),       ///< close by host
}drvCCIDEvent_e;

/**
 * @brief function type to notify data event
 */
typedef void (*drvCCIDEventCB_t)(drvCCIDEvent_e event, void *data, void *ctx);


/**
 * @brief CCID config
 */
typedef struct
{
    uint32_t event_mask;
    drvCCIDEventCB_t event_cb;
    void *event_cb_ctx;
	uint8_t *CCIDDevDesc;
	uint8_t *CCIDManufacturerDesc;
} drvCCIDCfg_t;

/**
 * \brief open the ccid device
 *
 * \param cfg  the CCID device conifg
 * \return
 *      - NULL  fail
 *      - other point of the instance
 */
drvCCID_t *drvCCIDOpen(drvCCIDCfg_t *cfg);

/**
 * \brief read CCID device
 * \note this api is synchronous, if no data coming, the call will block
 *
 * \param ccid  the CCID device
 * \param buf   buf to store read data
 * \param size  buffer size, must greater than struct ccid_bulk_out_header
 * \return
 *      - (negative integer)    negative error number
 *      - other                 actual read size
 */
int drvCCIDRead(drvCCID_t *ccid, struct ccid_bulk_out_header *buf, unsigned size);

/**
 * \brief write CCID device
 *
 * \param ccid  the CCID device
 * \param data  data buffer
 * \return
 *      - (negative integer)    negative error number
 *      - other                 actual write size
 */
int drvCCIDWrite(drvCCID_t *ccid, const struct ccid_bulk_in_header *data);

/**
 * \brief close CCID device
 *
 * \param ccid  the CCID device
 */
void drvCCIDClose(drvCCID_t *ccid);

OSI_EXTERN_C_END

#endif
