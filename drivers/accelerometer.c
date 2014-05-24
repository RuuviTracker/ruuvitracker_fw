/*
 *  MMA8652FC 3-Axis accelerometer driver for Ruuvitracker.
 *
 * @author: Tuomas Kulve
 */

#include "ch.h"
#include "hal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "accelerometer.h"

/* The threshold resolution is 0.063 g/LSB */
#define ACC_MT_THS_RESOLUTION        0.063

/* Enable detection of over 1.1G values*/
#define ACC_MT_THS_G                   1.1

/* Standard slave address for MMA8652FC */
#define ACC_ADDR                      0x1D

/* Device identification ID */
#define MMA8652FC_WHO_AM_I            0x4A

/* MMA8652FC registers */
#define STATUS                        0x00
#define SYSMOD                        0x0B
#define WHO_AM_I                      0x0D
#define FF_MT_CFG                     0x15
#define FF_MT_SRC                     0x16
#define FF_MT_THS                     0x17
#define FF_MT_COUNT                   0x18
#define CTRL_REG1                     0x2A
#define CTRL_REG2                     0x2B
#define CTRL_REG3                     0x2C
#define CTRL_REG4                     0x2D
#define CTRL_REG5                     0x2E

/* MMA8652FC register bits */
#define SYSMOD_SYSMOD_STANDBY         0b00
#define SYSMOD_SYSMOD_WAKE            0b01
#define SYSMOD_SYSMOD_SLEEP           0b10
#define FF_MT_CFG_ELE             (1 << 7)
#define FF_MT_CFG_OAE             (1 << 6)
#define FF_MT_CFG_ZEFE            (1 << 5)
#define FF_MT_CFG_YEFE            (1 << 4)
#define FF_MT_CFG_XEFE            (1 << 3)
#define FF_MT_THS_DBCNTM          (1 << 7)
#define CTRL_REG1_ACTIVE          (1 << 0)
#define CTRL_REG2_RST             (1 << 6)
#define CTRL_REG4_INT_EN_FF_MT    (1 << 2)
#define CTRL_REG5_INT_CFG_FF_MT   (1 << 2)
#define CTRL_REG3_WAKE_FF_MT      (1 << 3)
#define CTRL_REG3_POL             (1 << 1)

static I2CConfig i2c_cfg = {
    .op_mode = OPMODE_I2C,
    .clock_speed = 400000,
    .duty_cycle = FAST_DUTY_CYCLE_2
};

/* Enable motion detect on specified pin */
int8_t acc_mt_enable(uint8_t pin)
{
    uint8_t txbuf[2];
    uint8_t rxbuf[1];
    msg_t ret;

    i2cStart(&I2CD1, &i2c_cfg);

    // Reset the sensor to make sure everything is at initial values
    txbuf[0] = CTRL_REG2;
    txbuf[1] = CTRL_REG2_RST;
    // Ignoring the return value as the reset seems to reset MMA8652FC's
    // i2c stack as well
    i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);

    // Just to be safe
    i2cStop(&I2CD1);
    chThdSleepMilliseconds(1);
    i2cStart(&I2CD1, &i2c_cfg);

    // Read WHO AM I
    txbuf[0] = WHO_AM_I;
    rxbuf[0] = 0;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 1, rxbuf, 1);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    chDbgAssert(rxbuf[0] == MMA8652FC_WHO_AM_I, "Invalid WHO AM I from MMA8652FC", "#1");

    // Motion detect interrupt can wake from sleep mode.
    txbuf[0] = CTRL_REG3;
    txbuf[1] = CTRL_REG3_WAKE_FF_MT;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    if (pin == 1) {
        // Use int1 for interrupts (int2 is the default)
        txbuf[0] = CTRL_REG5;
        txbuf[1] = CTRL_REG5_INT_CFG_FF_MT;
        ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
        if (ret != RDY_OK) {
            chDbgAssert(0, "i2cMasterTransmit failed", "#1");
            return -1;
        }
    }

    // Enable motion interrupts
    txbuf[0] = CTRL_REG4;
    txbuf[1] = CTRL_REG4_INT_EN_FF_MT;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    // Enable motion detection on all directions
    txbuf[0] = FF_MT_CFG;
    txbuf[1] = FF_MT_CFG_OAE | FF_MT_CFG_ELE | FF_MT_CFG_ZEFE | FF_MT_CFG_YEFE | FF_MT_CFG_XEFE;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    // Set motion detection threshold
    txbuf[0] = FF_MT_THS;
    txbuf[1] = (((uint8_t)(ACC_MT_THS_G / ACC_MT_THS_RESOLUTION)) & 0x7F);
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    // Set motion debounce counter
    txbuf[0] = FF_MT_COUNT;
    txbuf[1] = 0x10; // Guessing 10 events
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    // Enable active mode
    txbuf[0] = CTRL_REG1;
    txbuf[1] = CTRL_REG1_ACTIVE;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    return 0;
}

int8_t acc_mt_disable(void)
{
    uint8_t txbuf[2];
    msg_t ret;

    // Disable motion interrupts
    txbuf[0] = CTRL_REG4;
    txbuf[1] = 0;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    // Enter standby mode
    txbuf[0] = CTRL_REG1;
    txbuf[1] = 0;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 2, NULL, 0);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }

    i2cStop(&I2CD1);

    return 0;
}

int8_t acc_mt_restart(void)
{
    uint8_t txbuf[2];
    uint8_t rxbuf[1];
    msg_t ret;

    // Restart the motion detect interrupts by reading the SRC
    txbuf[0] = FF_MT_SRC;
    rxbuf[0] = 0;
    ret = i2cMasterTransmit(&I2CD1, ACC_ADDR, txbuf, 1, rxbuf, 1);
    if (ret != RDY_OK) {
        chDbgAssert(0, "i2cMasterTransmit failed", "#1");
        return -1;
    }
    return 0;
}

/* Emacs indentatation information
   Local Variables:
   indent-tabs-mode:nil
   tab-width:4
   c-basic-offset:4
   End:
*/
