/*
 *  MMA8652FC 3-Axis accelerometer driver for RuuviTracker.
 *
 * @author: Tuomas Kulve
 */

#ifndef _ACCELEROMETER_H_
#define _ACCELEROMETER_H_

/******** API ***************/

/**
 * Start accelerometer sensor in motion detection mode using
 * interrupts in pin 1 or 2
 */
int8_t acc_mt_enable(uint8_t pin);

/**
 * Stop accelerometer
 */
int8_t acc_mt_disable(void);

/**
 * Restart motion detection after previous detection
 */
int8_t acc_mt_restart(void);

#endif

/* Emacs indentatation information
   Local Variables:
   indent-tabs-mode:nil
   tab-width:4
   c-basic-offset:4
   End:
*/
