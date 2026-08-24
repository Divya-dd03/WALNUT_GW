#ifndef _DRV_DEF_H_
#define _DRV_DEF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
#define BIT_0 (1 << 0)

#define BIT_1 (1 << 1)

#define BIT_2 (1 << 2)

#define BIT_3 (1 << 3)

#define BIT_4 (1 << 4)

#define BIT_5 (1 << 5)

#define BIT_6 (1 << 6)

#define BIT_7 (1 << 7)

#define BIT_8 (1 << 8)

#define BIT_9 (1 << 9)

#define BIT_10 (1 << 10)

#define BIT_11 (1 << 11)

#define BIT_12 (1 << 12)

#define BIT_13 (1 << 13)

#define BIT_14 (1 << 14)

#define BIT_15 (1 << 15)

#define BIT_16 (1 << 16)

#define BIT_17 (1 << 17)

#define BIT_18 (1 << 18)

#define BIT_19 (1 << 19)

#define BIT_20 (1 << 20)

#define BIT_21 (1 << 21)

#define BIT_22 (1 << 22)

#define BIT_23 (1 << 23)

#define BIT_24 (1 << 24)

#define BIT_25 (1 << 25)

#define BIT_26 (1 << 26)

#define BIT_27 (1 << 27)

#define BIT_28 (1 << 28)

#define BIT_29 (1 << 29)

#define BIT_30 (1 << 30)

#define BIT_(__n)           (1U<<(__n))
#define BIT(__n)            (1U<<(__n))

#define SHIFT0(Val)  (Val)

#define SHIFT1(Val)  ((Val) << 1)

#define SHIFT2(Val)  ((Val) << 2)

#define SHIFT3(Val)  ((Val) << 3)

#define SHIFT4(Val)  ((Val) << 4)

#define SHIFT5(Val)  ((Val) << 5)

#define SHIFT6(Val)  ((Val) << 6)

#define SHIFT7(Val)  ((Val) << 7)

#define SHIFT8(Val)  ((Val) << 8)

#define SHIFT9(Val)  ((Val) << 9)

#define SHIFT10(Val) ((Val) << 10)

#define SHIFT11(Val) ((Val) << 11)

#define SHIFT12(Val) ((Val) << 12)

#define SHIFT13(Val) ((Val) << 13)

#define SHIFT14(Val) ((Val) << 14)

#define SHIFT15(Val) ((Val) << 15)

#define SHIFT16(Val) ((Val) << 16)

#define SHIFT17(Val) ((Val) << 17)

#define SHIFT18(Val) ((Val) << 18)

#define SHIFT19(Val) ((Val) << 19)

#define SHIFT20(Val) ((Val) << 20)

#define SHIFT21(Val) ((Val) << 21)

#define SHIFT22(Val) ((Val) << 22)

#define SHIFT23(Val) ((Val) << 23)

#define SHIFT24(Val) ((Val) << 24)

#define SHIFT25(Val) ((Val) << 25)

#define SHIFT26(Val) ((Val) << 26)

#define SHIFT27(Val) ((Val) << 27)

#define SHIFT28(Val) ((Val) << 28)

#define SHIFT29(Val) ((Val) << 29)

#define SHIFT30(Val) ((Val) << 30)

#define SHIFT31(Val) ((Val) << 31)

// #include "plat_types.h"
/*
 * MFP configuration is represented by a 32-bit unsigned integer
 */
#define MFP(_off, _pull, _drv, _slp, _edge, _sleep, _afn) ( \
	/* bits 31..16 - MFP Register Offset */	(((_off) & 0xffff) << 16) | \
	/* bits 15..13 - Run Mode Pull State */	(((_pull) & 0x7) << 13) | \
	/* bit  12..11 - Driver Strength */		(((_drv) & 0x3) << 11) | \
	/* bits 10     - pad driver */				(((_slp) & 0x1) << 10) | \
	/* bit  09..07 - sleep mode */			(((_sleep) & 0xe) << 6) | \
	/* bits 06..04 - Edge Detection */		(((_edge) & 0x7) << 4) | \
	/* bits 03     - sleep mode */			(((_sleep) & 0x1) << 3) | \
	/* bits 02..00 - Alt-fun select */			((_afn) & 0x7))

/*
 * to facilitate the definition, the following macros are provided
 *
 * 				    offset, pull,pF, drv,dF, edge,eF ,afn,aF
 */
#define MFP_OFFSET_MASK		MFP(0xffff,    0,    0,   0,   0,   0,   0)
#define MFP_REG(x)			MFP(x,         0,    0,   0,   0,   0,   0)
#define MFP_REG_GET_OFFSET(x)	((x & MFP_OFFSET_MASK) >> 16)

#define MFP_AF0				MFP(0x0000,    0,    0,   0,   0,   0,   0)
#define MFP_AF1				MFP(0x0000,    0,    0,   0,   0,   0,   1)
#define MFP_AF2				MFP(0x0000,    0,    0,   0,   0,   0,   2)
#define MFP_AF3				MFP(0x0000,    0,    0,   0,   0,   0,   3)
#define MFP_AF4				MFP(0x0000,    0,    0,   0,   0,   0,   4)
#define MFP_AF5				MFP(0x0000,    0,    0,   0,   0,   0,   5)
#define MFP_AF6				MFP(0x0000,    0,    0,   0,   0,   0,   6)
#define MFP_AF7				MFP(0x0000,    0,    0,   0,   0,   0,   7)
#define MFP_AF_MASK			MFP(0x0000,    0,    0,   0,   0,   0,   7)

#define MFP_SLEEP_CTRL2		MFP(0x0000,    0,    0,   0,   0,   1,   0)
#define MFP_SLEEP_DIR		MFP(0x0000,    0,    0,   0,   0,   2,   0)
#define MFP_SLEEP_DATA		MFP(0x0000,    0,    0,   0,   0,   4,   0)
#define MFP_SLEEP_CTRL		MFP(0x0000,    0,    0,   0,   0,   8,   0)
#define MFP_SLEEP_MASK		MFP(0x0000,    0,    0,   0,   0, 0xf,   0)

#define MFP_SLEEP_NONE		MFP(0x0000,    0,    0,   0,   0,   0,   0)
#define MFP_SLEEP_FLOAT		MFP(0x0000,    0,    0,   0,   0, 0xb,   0)
#define MFP_SLEEP_OUTPUT_HIGH	MFP(0x0000,    0,    0,   0,   0, 0xd,   0)
#define MFP_SLEEP_OUTPUT_LOW	MFP(0x0000,    0,    0,   0,   0,   9,   0)

#define MFP_LPM_EDGE_NONE	MFP(0x0000,    0,    0,   0,   4,   0,   0)
#define MFP_LPM_EDGE_RISE	MFP(0x0000,    0,    0,   0,   1,   0,   0)
#define MFP_LPM_EDGE_FALL	MFP(0x0000,    0,    0,   0,   2,   0,   0)
#define MFP_LPM_EDGE_BOTH	MFP(0x0000,    0,    0,   0,   3,   0,   0)
#define MFP_LPM_EDGE_MASK	MFP(0x0000,    0,    0,   0,   7,   0,   0)

#define MFP_SLP_DI			MFP(0x0000,    0,    0,   1,   0,   0,   0)

#define MFP_DRIVE_VERY_SLOW	MFP(0x0000,    0,    0,   0,   0,   0,   0)
#define MFP_DRIVE_SLOW		MFP(0x0000,    0,    1,   0,   0,   0,   0)
#define MFP_DRIVE_MEDIUM	MFP(0x0000,    0,    2,   0,   0,   0,   0)
#define MFP_DRIVE_FAST		MFP(0x0000,    0,    3,   0,   0,   0,   0)
#define MFP_DRIVE_MASK		MFP(0x0000,    0,    3,   0,   0,   0,   0)

#define MFP_PULL_NONE		MFP(0x0000,    0,    0,   0,   0,   0,   0)
#define MFP_PULL_LOW		MFP(0x0000,    5,    0,   0,   0,   0,   0)
#define MFP_PULL_HIGH		MFP(0x0000,    6,    0,   0,   0,   0,   0)
#define MFP_PULL_BOTH		MFP(0x0000,    7,    0,   0,   0,   0,   0)
#define MFP_PULL_FLOAT		MFP(0x0000,    4,    0,   0,   0,   0,   0)
#define MFP_PULL_MASK		MFP(0x0000,    7,    0,   0,   0,   0,   0)

#define MFP_VALUE_MASK		(MFP_PULL_MASK | MFP_DRIVE_MASK | MFP_SLP_DI \
				| MFP_LPM_EDGE_MASK | MFP_SLEEP_MASK \
				| MFP_AF_MASK)
#define MFP_EOC			0xffffffff	/* indicates end-of-conf */

/*
 * Pin names and MFPR addresses
 */
#define MFPR_OFFSET(x)					((x)*4)
#define GPIO_00							MFPR_OFFSET(55)
#define GPIO_01							MFPR_OFFSET(56)
#define GPIO_02							MFPR_OFFSET(57)
#define GPIO_03							MFPR_OFFSET(58)
#define GPIO_04							MFPR_OFFSET(59)
#define GPIO_05							MFPR_OFFSET(60)
#define GPIO_06							MFPR_OFFSET(61)
#define GPIO_07							MFPR_OFFSET(62)
#define GPIO_08							MFPR_OFFSET(63)
#define GPIO_09							MFPR_OFFSET(64)
#define GPIO_10							MFPR_OFFSET(65)
#define GPIO_11							MFPR_OFFSET(66)
#define GPIO_12							MFPR_OFFSET(67)
#define GPIO_13							MFPR_OFFSET(68)
#define GPIO_14							MFPR_OFFSET(69)
#define GPIO_15							MFPR_OFFSET(70)
#define GPIO_16							MFPR_OFFSET(71)
#define GPIO_17							MFPR_OFFSET(72)
#define GPIO_18							MFPR_OFFSET(73)
#define GPIO_19							MFPR_OFFSET(74)
#define GPIO_20							MFPR_OFFSET(75)
#define GPIO_21							MFPR_OFFSET(76)
#define GPIO_22							MFPR_OFFSET(77)
#define GPIO_23							MFPR_OFFSET(78)
#define GPIO_24							MFPR_OFFSET(79)
#define GPIO_25							MFPR_OFFSET(80)
#define GPIO_26							MFPR_OFFSET(81)
#define GPIO_27							MFPR_OFFSET(82)
#define GPIO_28							MFPR_OFFSET(83)
#define GPIO_29							MFPR_OFFSET(84)
#define GPIO_30							MFPR_OFFSET(85)
#define GPIO_31							MFPR_OFFSET(86)
#define GPIO_32							MFPR_OFFSET(87)
#define GPIO_33							MFPR_OFFSET(88)
#define GPIO_34							MFPR_OFFSET(89)
#define GPIO_35							MFPR_OFFSET(90)
#define GPIO_36							MFPR_OFFSET(91)
#define GPIO_49							MFPR_OFFSET(104)
#define GPIO_50							MFPR_OFFSET(105)
#define GPIO_51							MFPR_OFFSET(106)
#define GPIO_52							MFPR_OFFSET(107)
#define GPIO_53							MFPR_OFFSET(108)
#define GPIO_54							MFPR_OFFSET(109)
#define GPIO_55							MFPR_OFFSET(50)
#define GPIO_56							MFPR_OFFSET(161)
#define GPIO_57							MFPR_OFFSET(162)
#define GPIO_58							MFPR_OFFSET(163)
#define GPIO_59							MFPR_OFFSET(164)
#define GPIO_60							MFPR_OFFSET(193)
#define GPIO_61							MFPR_OFFSET(194)
#define GPIO_62							MFPR_OFFSET(195)
#define GPIO_63							MFPR_OFFSET(196)
#define GPIO_64							MFPR_OFFSET(197)
#define GPIO_65							MFPR_OFFSET(198)
#define GPIO_66							MFPR_OFFSET(199)

#define GPIO_71							MFPR_OFFSET(111)    //used in 3603/3601,not used in 1603
#define GPIO_72							MFPR_OFFSET(112)    //used in 3603/3601,not used in 1603
#define GPIO_73							MFPR_OFFSET(113)    //used in 3603/3601,not used in 1603
#define GPIO_74							MFPR_OFFSET(114)    //used in 3603/3601,not used in 1603

#define GPIO_37							MFPR_OFFSET(115)    //used in 3603/3601,not used in 1603
#define GPIO_75							MFPR_OFFSET(116)    //used in 3603/3601,not used in 1603
#define GPIO_76							MFPR_OFFSET(116) 

#define GPIO_77							MFPR_OFFSET(116)    //used in 3603/3601,not used in 1603
#define GPIO_78							MFPR_OFFSET(117)    //used in 3603/3601,not used in 1603
#define GPIO_81							MFPR_OFFSET(120)    //used in 3603/3601,not used in 1603
#define GPIO_82							MFPR_OFFSET(121)    //used in 3603/3601,not used in 1603
#define GPIO_83							MFPR_OFFSET(122)    //used in 3603/3601,not used in 1603
#define GPIO_84							MFPR_OFFSET(123)    //used in 3603/3601,not used in 1603
#define GPIO_85							MFPR_OFFSET(124)    //used in 3603/3601,not used in 1603
#define GPIO_86							MFPR_OFFSET(125)    //used in 3603/3601,not used in 1603
#define GPIO_121						MFPR_OFFSET(203)
#define GPIO_122						MFPR_OFFSET(50)
#define CLK_REQ							MFPR_OFFSET(51)
#define GPIO_124						MFPR_OFFSET(52)
#define GPIO_TDS_DIO0_DVC_0				MFPR_OFFSET(173)
#define GPIO_TDS_DIO1_DVC_1				MFPR_OFFSET(174)
#define GPIO_68							MFPR_OFFSET(174)
#define GPIO_69_TDS_DIO2				MFPR_OFFSET(175)
#define GPIO_70_TDS_DIO3				MFPR_OFFSET(176)
#define GPIO_TDS_DIO4_QSPI_DAT3			MFPR_OFFSET(177)
#define GPIO_TDS_DIO5_QSPI_DAT2			MFPR_OFFSET(178)
#define GPIO_TDS_DIO6_QSPI_DAT1			MFPR_OFFSET(179)
#define GPIO_TDS_DIO7_QSPI_DAT0			MFPR_OFFSET(180)
#define GPIO_TDS_CLK_QSPI_CLK			MFPR_OFFSET(181)
#define GPIO_TDS_DIO8_QSPI_CS1			MFPR_OFFSET(182)
#define GPIO_TDS_DIO9_QSPI_CS2			MFPR_OFFSET(183)
#define GPIO_TDS_DIO10_QSPI_DQM			MFPR_OFFSET(184)
#define GPIO_79_TDS_DIO11				MFPR_OFFSET(185)
#define GPIO_80_TDS_DIO12				MFPR_OFFSET(186)
#define GPIO_TDS_DIO13_SDIO_DAT3		MFPR_OFFSET(187)	//func1 can be used as GPIO48
#define GPIO_TDS_DIO14_SDIO_DAT2		MFPR_OFFSET(188)	//func1 can be used as GPIO55
#define GPIO_TDS_DIO15_SDIO_DAT1		MFPR_OFFSET(189)	//func1 can be used as GPIO56
#define GPIO_TDS_DIO16_SDIO_DAT0		MFPR_OFFSET(190)	//func1 can be used as GPIO57
#define GPIO_TDS_DIO17_SDIO_CMD			MFPR_OFFSET(191)	//func1 can be used as GPIO58
#define GPIO_TDS_DIO18_SDIO_CLK			MFPR_OFFSET(192)	//func1 can be used as GPIO59
#define GPIO_USIM2_UCLK					MFPR_OFFSET(152)
#define GPIO_USIM2_UIO					MFPR_OFFSET(153)
#define GPIO_USIM2_URSTn				MFPR_OFFSET(154)
//#define GPIO_SLAVE_RESET_OUT_N			MFPR_OFFSET(50)
#define GPIO_USIM_UCLK					MFPR_OFFSET(200)
#define GPIO_USIM_UIO					MFPR_OFFSET(201)
#define GPIO_USIM_URSTn					MFPR_OFFSET(202)
#define GPIO_VCXO_OUT					MFPR_OFFSET(54)
#define GPIO_VCXO_REQ					MFPR_OFFSET(53)
#define GPIO_PRI_TDI					MFPR_OFFSET(45)
#define GPIO_PRI_TMS					MFPR_OFFSET(46)
#define GPIO_PRI_TCK					MFPR_OFFSET(47)
#define GPIO_PRI_TDO					MFPR_OFFSET(48)
#define GPIO_MMC1_CD					MFPR_OFFSET(43)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_CLK					MFPR_OFFSET(42)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_CMD					MFPR_OFFSET(41)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_DAT0					MFPR_OFFSET(40)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_DAT1					MFPR_OFFSET(39)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_DAT2					MFPR_OFFSET(38)     //used in 3603/3601,not used in 1603
#define GPIO_MMC1_DAT3					MFPR_OFFSET(37)     //used in 3603/3601,not used in 1603
#define QSPI_VMODE                      MFPR_OFFSET(49)     //used in 1603,not used in 3603/3601


#define	REGS_MFPR_BASE					0xD401E000

#define PULL_SEL             		( BIT(15) )
#define PULLUP_EN                  	( BIT(14) )
#define PULLDN_EN                  	( BIT(13) )
#define DRIVE(_X_)                 	( (_X_) << 11 & (BIT(11)|BIT(12)))
#define SLEEP_SEL0                 	( BIT(9) )
#define SLEEP_DATA                 	( BIT(8) )
#define SLEEPDIR                   	( BIT(7) )
#define EDGE_CLEAR                 	( BIT(6) )
#define EDGE_FALL_EN               	( BIT(5) )
#define EDGE_RISE_EN               	( BIT(4) )
#define SLEEPSEL1                  	( BIT(3) )
#define AF_SEL(_X_)               	(_X_ & (BIT(0) | BIT(1) | BIT(2)))

#define RND8( _x )       ((((_x) + 7) / 8 ) * 8 ) /*rounds a number up to the nearest multiple of 8 */


#define  UPCASE( _c ) ( ((_c) >= 'a' && (_c) <= 'z') ? ((_c) - 0x20) : (_c) )

#define  DECCHK( _c ) ((_c) >= '0' && (_c) <= '9')

#define  DTMFCHK( _c ) ( ((_c) >= '0' && (_c) <= '9') ||\
                       ((_c) >= 'A' && (_c) <= 'D') ||\
                       ((_c) >= 'a' && (_c) <= 'd') ||\
					   ((_c) == '*') ||\
					   ((_c) == '#'))

#define  HEXCHK( _c ) ( ((_c) >= '0' && (_c) <= '9') ||\
                       ((_c) >= 'A' && (_c) <= 'F') ||\
                       ((_c) >= 'a' && (_c) <= 'f') )
#ifndef ARR_SIZE
#define  ARR_SIZE( _a )  ( sizeof( (_a) ) / sizeof( (_a[0]) ) )
#endif

#ifndef REG_ADDR
#define REG_ADDR(_b_, _o_) ( (uint32_t)(_b_) + (_o_) )
#endif

#ifndef REG32
#define REG32(x)                                    (*((volatile uint32_t *)(x)))
#endif // !REG32
#define REG64(x)                                    (*((volatile uint64_t *)(x)))
#define REG16(x)                                    (*((volatile uint16_t*) (x)))
#define REG8(x)                                     (*((volatile uint8_t*) (x)))

#define write64(addr, val)                          (*(volatile unsigned long long *)(addr) = (val))
#define read64(addr)                                (*(volatile unsigned long long *)(addr))

#define write32(addr, val)                          (*(volatile unsigned int *)(addr) = (val))
#define read32(addr)                                (*(volatile unsigned int *)(addr))

#define write16(addr, val)                          (*(volatile uint16 *)(addr) = (val))
#define read16(addr)                                (*(volatile uint16 *)(addr))


#define CHIP_REG_OR(reg_addr, value)    (*(volatile uint32_t *)(reg_addr) |= (uint32_t)(value))
#define CHIP_REG_AND(reg_addr, value)   (*(volatile uint32_t *)(reg_addr) &= (uint32_t)(value))
#define CHIP_REG_GET(reg_addr)          (*(volatile uint32_t *)(reg_addr))
#define CHIP_REG_SET(reg_addr, value)   (*(volatile uint32_t *)(reg_addr)  = (uint32_t)(value))

#define SET_BIT(reg,bit)    ((reg) |= (1<<(bit)))
#define CLR_BIT(reg,bit)    ((reg) &= ~(1<<(bit)))
#define TST_BIT(a_bit,shift_number)        (((a_bit)>>(shift_number))&1)
#define XOR_BIT(a_bit,shift_number)        ((a_bit) ^= (1<<(shift_number)))

#define DRV_MACRO_START do {
#define DRV_MACRO_END   } while (0)


#define MEDIA_CLK_OFFLINE        BIT(0)
#define MEDIA_CLK_ISP2X          BIT(1)
#define MEDIA_CLK_PHY            BIT(2)
#define MEDIA_CLK_INTERFACE      BIT(3)
#define MEDIA_CLK_OFFLINE_AXI    BIT(4)
#define MEDIA_CLK_LCD_CI         BIT(5)
#define MEDIA_CLK_LCD_AHB        BIT(6)
#define MEDIA_CLK_POWER          BIT(7)
#define MEDIA_CLK_LCD_SCLK		 BIT(8)
#define MEDIA_CLK_LCD_AXI		 BIT(9)
#define MEDIA_CLK_JPEG_FUNC		 BIT(10)

#ifndef NULL
#define NULL ((void*)0)
#endif

#define OS_WAIT_FORERVER (0xFFFFFFFFUL)
#define OS_WAIT_100MS (0xFFUL)

enum media_clk_module_id{
	MODULE_CAMERA = 0,
	MODULE_DISPLAY,
	MODULE_VIDEO,
	MODULE_MAX,
};

enum media_clk_dump_level{
	LEVEL_DUMP_CLOCK_ONLY = 0,
	LEVEL_DUMP_CLOCK_TIMES,
};

enum media_clk_sub_module_id{
	SUB_MODULE_OFFLINE = 0,
	SUB_MODULE_ISP2X,
	SUB_MODULE_PHY,
	SUB_MODULE_INTERFACE,
	SUB_MODULE_OFFLINE_AXI,
	SUB_MODULE_LCD_CI,
	SUB_MODULE_LCD_AHB,
	SUB_MODULE_POWER,  // multimedia power
	SUB_MODULE_LCD_SCLK,
	SUB_MODULE_LCD_AXI,
	SUB_MODULE_JPEG_FUNC,
	SUB_MODULE_MAX
};

#ifdef __cplusplus
}
#endif

#endif /*_DRV_I2C_H_*/
