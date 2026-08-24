/* All rights reserved.
 *
 */

#ifndef _DRV_PWM_H_
#define _DRV_PWM_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PWM ports supported
 */
typedef enum
{
	PWM_PORT_0,		///<PWM0
	PWM_PORT_1,		///<PWM1
	PWM_PORT_2,		///<PWM2
	PWM_PORT_3,		///<PWM3
	PWM_PORT_NUM
}PwmPort_e;

/**
 * @brief initialize PWM ports and clk frequency
 *
 * @param port  		port of PWM to initialize
 * @param clk_freq  	clk frequency of PWM to initialize
 */
void pwm_init(PwmPort_e port, unsigned long clk_freq);

/**
 * @brief 	start pwm function by port
 * 
 * @note 	only support port 0, 1, 2, 3
 *
 * @param port  p		ort of PWM
 * @param period_ns  	cycle
 * @param duty_ns  		duty cycle
 * @return
 *      - (-1) 	parameter error
 *      - 0		success
 */
int pwm_work(PwmPort_e port, int period_ns, int duty_ns);

/**
 * @brief 	stop pwm by port
 * 
 * @note 	only support port 0, 1, 2, 3
 *
 * @param port  port of PWM
 * @return
 *      - (-1) 	parameter error
 *      - 0		success
 */
int pwm_stop(PwmPort_e port);

#ifdef __cplusplus
}
#endif
#endif/*_DRV_PWM_H_*/
