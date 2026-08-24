#ifndef __HAL_LCD_DMA_H_
#define __HAL_LCD_DMA_H_

#include <stdint.h>
#include "drv_def.h"
#include "qspi_dma.h"
#include "dma.h"


// typedef struct
// {
// 	unsigned int DDADR; //descriptor address reg
// 	unsigned int DSADR; //source address reg
// 	unsigned int DTADR; //target address reg
// 	unsigned int DCMD; //command address register
// }QSPI_XLLP_DMAC_DESCRIPTOR_T, *P_QSPI_XLLP_DMAC_DESCRIPTOR_T;


// typedef struct
// {
// 	unsigned int DCSR[32];
// 	unsigned int RESERVED0[0x8];
// 	unsigned int DALGN;
// 	unsigned int DPCSR;
// 	unsigned int RESERVED1[0xE];
// 	unsigned int DRQSR0;
// 	unsigned int DRQSR1;
// 	unsigned int DRQSR2;
// 	unsigned int RESERVED2[0x1];
// 	unsigned int DINT;
// 	unsigned int RESERVED3[0x3];
// 	unsigned int DRCMR1[64];
// 	QSPI_XLLP_DMAC_DESCRIPTOR_T DMA_DESC[32];
// 	unsigned int RESERVED4[0x340];
// 	unsigned int DRCMR2[36];
// }QSPI_XLLP_DMAC_T, *P_QSPI_XLLP_DMAC_T;

// struct DMA_CMD_BITS 
// {
// 	unsigned int Length			:13; //Bits [12:0]: Length of transfer in Bytes
// 	unsigned int Reserved4		:1; //Bit 13: reserved
// 	unsigned int Width			:2; //Bits [15:14]: Width of onchip peripheral - 0=reserved, 1=1B, 2=2B, 3=4B
// 	unsigned int MaxBurstSize	:2; //Bits [17:16]: Max Burst Size - 0=reserved, 1=8B, 2=16B, 3=32B
// 	unsigned int Reserved3		:3; //Bits [20:18]: reserved
// 	unsigned int EndIRQEn		:1; //Bit 21: end interrupt enable
// 	unsigned int StartIRQEn		:1; //Bit 22: start interrupt enable
// 	unsigned int AddrMode		:1; //Bit 23: Addressing mode for descriptor compare - 0=target has addr/1=target has data
// 	unsigned int Reserved2		:1; //Bit 24: reserved
// 	unsigned int CmpEn			:1; //Bit 25: Compare Enable - descriptor branching compare enable
// 	unsigned int Reserved1		:2; //Bits [27:26]: reserved
// 	unsigned int FlowTrg		:1;	//Bit 28: Target flow control - 1=wait for request signal
// 	unsigned int FlowSrc		:1;	//Bit 29: Source flow control - 1=wait for request signal
// 	unsigned int IncTrgAddr		:1;	//Bit 30: Target increment - 0=no increment, 1=increment
// 	unsigned int IncSrcAddr		:1;	//Bit 31: Source increment - 0=no increment, 1=increment
// };

// typedef union
// {
// 	unsigned int value;
// 	struct DMA_CMD_BITS bits;	
// } DMA_CMDx_T, *P_DMA_CMDx_T;


// #define RCMR_MAPVLD BIT_7
// #define CSR_STOPINTR BIT_3
// #define CSR_RUN BIT_31
// #define CSR_STOPIRQEN BIT_29

// #define CSR_BUSERRINTR BIT_0
// #define CSR_STARTINTR BIT_1
// #define CSR_ENDINTR BIT_2
// #define CSR_EORIRQEN BIT_28

// #define CSR_NODESCFETCH BIT_30


// #define DMA_DDADR_RESERVED_MASK	    0xFFFFFFF0
// #define DMA_STATUS_INTERRUPTS_MASK  0x01000217
// #define DMA_DEVICE_PART1_NUM		64

// #define DMA_SQU_2_SQU_CHANNEL       0
// #define DMA_SQU_2_DDR_CHANNEL       1
// #define DMA_DDR_2_SQU_CHANNEL       2
// #define DMA_DDR_2_DDR_CHANNEL       3
// #define DMA_XFER_CHANNEL            4

// #define DMA_GEU_W_CHANNEL           5
// #define DMA_GEU_R_CHANNEL           6

// #define DMA_SQU_ADDR_1              0xD1000000
// #define DMA_SQU_ADDR_2              0xD1004000

// #define DMA_MDMA_XFER_SIZE	        0xF000
// #define DMA_MAX_BYTES_PER_DESC      0x1F00

// #define DMA_MDMA_NF_XFER_SIZE       0x1F00

// #define DMA_STRESS_XFER_SIZE        0x5000

// typedef struct
// {
//     uint32_t    DDADR;  // descriptor address reg
//     uint32_t    DSADR;  // source address register
//     uint32_t    DTADR;  // target address register
//     uint32_t    DCMD;   // command address register
// }DMA_DESCRIPTOR, *P_DMA_DESCRIPTOR;

// typedef enum{
//     DMA_REQ_CLK = 1,            
//     DMA_REQ_UART2_RX = 4,
//     DMA_REQ_UART2_TX = 5,
    
//     DMA_REQ_USIM1_RX = 8,
//     DMA_REQ_USIM1_TX = 9,
//     DMA_REQ_USIM2_RX = 10,
//     DMA_REQ_USIM2_TX = 11,
    
//     DMA_REQ_UART1_RX = 21,
//     DMA_REQ_UART1_TX = 22,
//     DMA_REQ_UART3_RX = 23,
//     DMA_REQ_UART3_TX = 24,
//     DMA_REQ_I2C0_TX = 25,
//     DMA_REQ_I2C0_RX = 26,
//     DMA_REQ_I2C1_TX = 27,
//     DMA_REQ_I2C1_RX = 28,
    
//     DMA_REQ_PWR_I2C_TX = 30,
//     DMA_REQ_PWR_I2C_RX = 31,
//     DMA_REQ_I2C2_TX  = 32,
//     DMA_REQ_I2C2_RX = 33,

// 	DMA_REQ_SSP0_RX = 52,
// 	DMA_REQ_SSP0_TX = 53,

// 	DMA_REQ_SSP2_RX = 60,
// 	DMA_REQ_SSP2_TX = 61,

// 	DMA_REQ_AEU_READ_CHANNEL = 68,
// 	DMA_REQ_AUE_WRITE_CHANNEL = 69,

// 	DMA_REQ_QSPI_RX = 97,
// 	DMA_REQ_QSPI_TX = 99
// }DMA_MAP_DEVICE;

// typedef struct DMA_STRESS_ARG_TAG{
//     uint32_t channel;
//     uint32_t num;
//     uint32_t size;
//     uint32_t stop_flag;
//     uint32_t mask;
//     uint32_t mask_t;
//     uint32_t src[32];
//     uint32_t dst[32];
//     uint32_t des[32];
// }DMA_STRESS_ARG, *P_DMA_STRESS_ARG;

// typedef enum{
//     DMA_BURST_SIZE_RSVD = 0,
//     DMA_BURST_SIZE_8B,
//     DMA_BURST_SIZE_16B,
//     DMA_BURST_SIZE_32B
// }DMA_BURST_SIZE;

// typedef enum{
//     DMA_MODE_FETCH = 0,
//     DMA_MODE_NONFETCH
// }DMA_MODE;

// typedef enum
// {
// 	DMA_STOP = 0,
// 	DMA_END
// }DMA_WAIT_DONE_FLAG;

enum{
	SPI_6_5M = 0,
	SPI_13M = 1,
	SPI_26M,
	SPI_52M,
	SPI_3_25M,
	SPI_1_625M,
	SPI_812_5k,
	SPI_CLK_LIMIT
};

typedef int(*dma_callback_t)(uint32_t);


typedef enum
{
	spi_rx = 0,
	spi_tx,
	max_dir,
}spidir;

int qspi_dma_init(void);
int dma_irq_callback_register(dma_callback_t func, uint32_t channel);
void dma_stop_irq_en(uint32_t channel);
void dma_map_device_to_channel(DMA_MAP_DEVICE device_id, uint32_t channel);
void set_user_aligment(uint32_t channel);
void dma_set_mode(uint32_t mode, uint32_t channel);
void dma_set_reg_nf(uint32_t src, uint32_t trg, P_DMA_CMDx_T pCMD, uint32_t channel);
void dma_xfer_start(uint32_t channel);
void dma_xfer_stop(uint32_t channel);
void ssp_write_cmd_data_to_LCD(uint32_t cmd, uint32_t data,uint32_t data_bits,uint16_t line_num);
void wait_ssp_write_data_to_LCD_complete(void);
void spi_set_clk_rate(unsigned int clk_dev);

void platLcdSpiInit(void);
void platLcdDmaInit(void);
void platLcdWaitTransferDone(void);
void platLcdDmaUninit(void);
void platLcdSpiSetClock(uint32_t clk_dev);
void platLcdWriteDataDma(uint16_t* data,uint32_t sum_pixel,uint32_t data_bits,uint16_t line_num, bool sync);
void platLcdPowerOnOff(bool on_off);
void platLcdPinmuxCfg(void);
void platLcdBacklightIOSet(bool on_off);
void platLcdCsIOSet(bool val);
void platLcdA0IOSet(bool val);
void platLcdHwReset(void);
#endif
