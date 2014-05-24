/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <time.h>

#include "ch.h"
#include "hal.h"
#include "test.h"

#include "shell.h"
#include "drivers/debug.h"
#include "chprintf.h"
#include "power.h"
#include "drivers/usb_serial.h"
#include "drivers/gps.h"
#include "drivers/gsm.h"
#include "drivers/http.h"
#include "drivers/reset_button.h"
#include "drivers/rtchelpers.h"
#include "drivers/accelerometer.h"

/* I2C interface #1 */
// TODO: This should probably be defined in board.h or something. It needs to be globally accessible because in case of I2C timeout we *must* reinit the whole I2C subsystem
static const I2CConfig i2cfg1 = {
    OPMODE_I2C,
    400000,
    FAST_DUTY_CYCLE_2,
};
// TODO: I guess this does not have to be global
static i2cflags_t i2c_errors = 0;


/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

#define SHELL_WA_SIZE   THD_WA_SIZE(2048)
#define TEST_WA_SIZE    THD_WA_SIZE(256)

static void cmd_mem(BaseSequentialStream *chp, int argc, char *argv[])
{
    size_t n, size;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: mem\r\n");
        return;
    }
    n = chHeapStatus(NULL, &size);
    chprintf(chp, "core free memory : %u bytes\r\n", chCoreStatus());
    chprintf(chp, "heap fragments   : %u\r\n", n);
    chprintf(chp, "heap free total  : %u bytes\r\n", size);
}

static void cmd_threads(BaseSequentialStream *chp, int argc, char *argv[])
{
    static const char *states[] = {THD_STATE_NAMES};
    Thread *tp;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: threads\r\n");
        return;
    }
    chprintf(chp, "    addr    stack prio refs     state time\r\n");
    tp = chRegFirstThread();
    do {
        chprintf(chp, "%.8lx %.8lx %4lu %4lu %9s %lu\r\n",
                 (uint32_t)tp, (uint32_t)tp->p_ctx.r13,
                 (uint32_t)tp->p_prio, (uint32_t)(tp->p_refs - 1),
                 states[tp->p_state], (uint32_t)tp->p_time);
        tp = chRegNextThread(tp);
    } while (tp != NULL);
#ifdef CORTEX_ENABLE_WFI_IDLE
    chprintf(chp, "CRTX_ENABLE_WFI_IDLE=%d\r\n", CORTEX_ENABLE_WFI_IDLE);
#endif
#ifdef ENABLE_WFI_IDLE
    chprintf(chp, "ENBL_WFI_IDLE=%d\r\n", ENABLE_WFI_IDLE);
#endif
}

static void cmd_test(BaseSequentialStream *chp, int argc, char *argv[])
{
    Thread *tp;

    (void)argv;
    if (argc > 0) {
        chprintf(chp, "Usage: test\r\n");
        return;
    }
    tp = chThdCreateFromHeap(NULL, TEST_WA_SIZE, chThdGetPriority(),
                             TestThread, chp);
    if (tp == NULL) {
        chprintf(chp, "out of memory\r\n");
        return;
    }
    chThdWait(tp);
}

/*
 * GPS Test command.
 * Enables GPS chip on RuuviTracker and pushed NMEA messages from serial port
 * to usb serial adapter
 */
static void cmd_gps(BaseSequentialStream *chp, int argc, char *argv[])
{
    struct gps_data_t gps;
    char buf[255];
    (void)argc;
    (void)argv;
    chprintf(chp, "Enabling GPS module.\r\nPress enter to stop\r\n\r\n");
    gps_start();
    while (chnGetTimeout((BaseChannel *)chp, TIME_IMMEDIATE) == Q_TIMEOUT) {
        chprintf(chp, "\x1B[2J-------[GPS Tracking,  press enter to stop]----\r\n");
        if (GPS_FIX_TYPE_NONE != gps_has_fix()) {
            gps = gps_get_data();
            snprintf(buf, sizeof(buf),
                     "lat: %f\r\n"
                     "lon: %f\r\n"
                     "satellites: %d\r\n"
                     "time: %d-%d-%d %d:%d:%d\r\n",
                     (float)gps.lat,(float)gps.lon, gps.n_satellites,
                     gps.dt.year, gps.dt.month, gps.dt.day, gps.dt.hh, gps.dt.mm, gps.dt.sec);
            chprintf(chp, buf);
        } else {
            chprintf(chp, "waiting for fix\r\n");
        }
        chThdSleepMilliseconds(1000);
    }
    gps_stop();
    chprintf(chp, "GPS stopped\r\n");
}

static void cmd_gsm(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1) {
        chprintf(chp, "Usage: gsm [apn <apn_name>] | [start] | [cmd <str>]\r\n");
        return;
    }
    if (0 == strcmp(argv[0], "apn")) {
        gsm_set_apn(argv[1]);
    } else if (0 == strcmp(argv[0], "start")) {
        gsm_start();
    } else if (0 == strcmp(argv[0], "cmd")) {
        gsm_cmd(argv[1]);
    }
}

static void cmd_http(BaseSequentialStream *chp, int argc, char *argv[])
{
    HTTP_Response *http;
    if (argc < 2) {
        chprintf(chp, "Usage: http [get <url>] | [post <url> <content> <content_type>]\r\n");
        return;
    }
    if (0 == strcmp(argv[0], "get")) {
        http = http_get(argv[1]);
        if (http) {
            chprintf(chp, "OK, Status %d\r\nContent-size: %d\r\nContent:\r\n%s\r\n", http->code, http->content_len, http->content);
            http_free(http);
        } else {
            chprintf(chp, "Failed\r\n");
        }
    } else if (0 == strcmp(argv[0], "post")) {
        chprintf(chp, "POST\r\nurl: %s\r\ntype: %s\r\ndata: %s\r\n", argv[1], argv[2], argv[3]);
        http = http_post(argv[1], argv[2], argv[3]);
        if (http) {
            chprintf(chp, "OK, Status %d\r\nContent-size: %d\r\nContent:\r\n%s\r\n", http->code, http->content_len, http->content);
            http_free(http);
        } else {
            chprintf(chp, "Failed\r\n");
        }
    }
}

/**
 * Test command to read first 6 registers of the accelerometer
 */
static void cmd_accread(BaseSequentialStream *chp, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    msg_t status = RDY_OK;
    //uint8_t txbuff[1];
    uint8_t rxbuff[6];
    uint8_t txbuff[1];
    txbuff[0] = 0x0; // register
    uint8_t addr = 0x1d; // 7-bit address
    i2cAcquireBus(&I2CD1);
    i2cStart(&I2CD1, &i2cfg1);
    status = i2cMasterTransmitTimeout(&I2CD1, addr, txbuff, 1, rxbuff, 6, MS2ST(500));
    switch(status)
    {
        case RDY_OK:
        {
            uint8_t i;
            for (i=0; i<6; i++)
            {
                chprintf(chp, "Device 0x%02X reg 0x%02X value 0x%02X\r\n", addr, txbuff[0]+i, rxbuff[i]);
            }
            break;
        }
        case RDY_RESET:
            i2c_errors = i2cGetErrors(&I2CD1);
            chprintf(chp, "error for device at 0x%02X, errors: 0x%02X I2C1->SR1=0x%04X\r\n", addr, i2c_errors, I2C1->SR1);
            // Reset copied from http://forum.chibios.org/phpbb/viewtopic.php?f=2&t=777
            i2cStop(&I2CD1);
            I2C1->CR1 |= I2C_CR1_SWRST;
            i2cStart(&I2CD1, &i2cfg1);
            break;
        case RDY_TIMEOUT:
            chprintf(chp, "TIMEOUT for device at 0x%02X\r\n", addr, i2c_errors);
            // Reset copied from http://forum.chibios.org/phpbb/viewtopic.php?f=2&t=777
            i2cStop(&I2CD1);
            I2C1->CR1 |= I2C_CR1_SWRST;
            i2cStart(&I2CD1, &i2cfg1);
            break;
    }
    i2cStop(&I2CD1);
    i2cReleaseBus(&I2CD1);
}    

/**
 * Accelerometer commands
 */
static void cmd_acc(BaseSequentialStream *chp, int argc, char *argv[])
{
    char usage[] = "Usage: acc [motion <1,2>] | stop\r\n";

    if (argc < 1) {
        chprintf(chp, usage);
        return;
    }
    if (0 == strcmp(argv[0], "motion")) {
        uint8_t pin = argv[1][0] - '0';

        if (pin != 1 && pin != 2) {
            chprintf(chp, "Invalid interrupt pin: %d\r\n", pin);
            chprintf(chp, usage);
            return;
        }

        acc_mt_enable(pin);
        chprintf(chp, "Started motion dection with interrupt pin %d\r\n", pin);

    } else if (0 == strcmp(argv[0], "stop")) {
        acc_mt_disable();
        chprintf(chp, "Stopped motion detection\r\n");
    } else {
        chprintf(chp, "Invalid acc command: %s\r\n", argv[0]);
        chprintf(chp, usage);
    }
}

static const ShellCommand commands[] = {
    {"mem", cmd_mem},
    {"threads", cmd_threads},
    {"test", cmd_test},
    {"gps_test", cmd_gps},
    {"gsm", cmd_gsm},
    {"http", cmd_http},
    {"stop", cmd_stop},
    {"standby", cmd_standby},
    {"date", cmd_date},
    {"alarm", cmd_alarm},
    {"wakeup", cmd_wakeup},
    {"accread", cmd_accread},
    {"acc", cmd_acc},
    {NULL, NULL}
};

static const ShellConfig shell_cfg1 = {
    (BaseSequentialStream *)&SDU,
    commands
};

/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

/*
 * Red LED blinker thread, times are in milliseconds.
 */
static WORKING_AREA(waThread1, 128);

__attribute__((noreturn))
static void Thread1(void *arg)
{
    uint8_t i = 0;

    (void)arg;

    chRegSetThreadName("blinker");
    while (TRUE) {

        // Check for movement every 100ms and blink slowly if no movement
        if (PAL_HIGH == palReadPad(GPIOA, GPIOA_ACC_INT2)) {
            chThdSleepMilliseconds(100);
            if (++i >= 10) {
                i = 0;
                palTogglePad(GPIOB, GPIOB_LED1);
            }
            continue;
        }

        // If movement detected, blink quickly for 1sec, then reset movement
        for (i = 0; i < 10; ++i) {
            palTogglePad(GPIOB, GPIOB_LED1);
            chThdSleepMilliseconds(100);
        }
        i = 0;
        acc_mt_restart();
    }
}

/*
 * Application entry point.
 */
static const EXTConfig extcfg; 
int main(void)
{
    Thread *shelltp = NULL;

    /*
     * System initializations.
     * - HAL initialization, this also initializes the configured device drivers
     *   and performs the board-specific initializations.
     * - Kernel initialization, the main() function becomes a thread and the
     *   RTOS is active.
     */
    halInit();
    chSysInit();

    /*
     * Initializes a serial-over-USB CDC driver.
     */
    usb_serial_init();

    /* Initializes reset button PA0 */
    /**
     * This messes my button wakeup
    button_init();
     */
    extStart(&EXTD1, &extcfg);

    /*
     * Enable motion detection using interrupt pin 2
     */
    acc_mt_enable(2);

    /*
     * Shell manager initialization.
     */
    shellInit();

    /*
     * Creates the blinker thread.
     */
    chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, (tfunc_t)Thread1, NULL);
    palClearPad(GPIOB, GPIOB_LED2);

    /*
     * Normal main() thread activity, in this demo it does nothing except
     * sleeping in a loop and check the button state.
     */
    while (TRUE) {
        if (!shelltp && (SDU.config->usbp->state == USB_ACTIVE)) {
            palSetPad(GPIOB, GPIOB_LED2);
            shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
        } else if (chThdTerminated(shelltp)) {
            chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
            shelltp = NULL;           /* Triggers spawning of a new shell.        */
            palClearPad(GPIOB, GPIOB_LED2);
        }
        chThdSleepMilliseconds(1000);
    }
}

/* Emacs indentatation information
   Local Variables:
   indent-tabs-mode:nil
   tab-width:4
   c-basic-offset:4
   End:
*/
