#ifndef	_MFPR_H_
#define	_MFPR_H_

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

//#define mfpr_debug
#endif	/* _MFPR_H_ */

