/*
 *  Simcom 908 GPS Driver for Ruuvitracker.
 *
 * @author: Tomi Hautakoski
*/

#include <string.h>
#include <stdlib.h>
#include <delay.h>
#include <stdio.h>
#include <math.h>
#include "platform.h"
#include "platform_conf.h"
#include "common.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "gsm.h"
#include "gps.h"
#include "slre.h"

#ifdef BUILD_GPS

/* Lua API 2 */
#define MIN_OPT_LEVEL 2
#include "lrodefs.h"




int gps_has_fix(lua_State *L)
{
  lua_pushboolean(L, (gps.state == STATE_HAS_3D_FIX) || 
                      (gps.state == STATE_HAS_2D_FIX));
  
	return 1;
}

int gps_set_power_state(lua_State *L)
{
  // TODO: handle error states
  enum GPS_power_mode next = luaL_checkinteger(L, -1);
  
  switch(next) {
    case GPS_POWER_ON:
      if(gps.state == STATE_OFF) {
        while(gsm_is_gps_ready() != TRUE) {
          delay_ms(100);
        }
        gsm_cmd("AT+CGPSPWR=1");    /* Power-on */
        gsm_cmd("AT+CGPSOUT=255");  /* Select which GP sentences GPS should print */
        gsm_cmd("AT+CGPSRST=1");    /* Do a GPS warm reset */
      }
      break;
    case GPS_POWER_OFF:
      if(gps.state == STATE_ON) {
        // TODO: handle other states
        gsm_cmd("AT+CGPSPWR=0");    /* Power-off */
      }
      break;
    default:
      printf("GPS: Error, unknown power state!\n");
      break;
    }
  return 0;
}

int gps_get_location(lua_State *L)
{
 	lua_pushnumber(L, gps_data.lat);
 	lua_pushnumber(L, gps_data.lon);
  return 2;
}



/* ===================Internal functions===================== */

/* Setup IO ports. Called in platform_setup() from platform.c */
void gps_setup_io()
{
  // Setup serial port, GPS_UART_ID == 2 @ RUUVIB1
  // TODO: setup other boards
  platform_uart_setup(GPS_UART_ID, 115200, 8, PLATFORM_UART_PARITY_NONE, PLATFORM_UART_STOPBITS_1);
}

/**
 * Receives lines from Simcom serial interfaces and parses them.
 */
void gps_line_received()
{
  char buf[GPS_BUFF_SIZE];
  int c, i = 0;

  while('\n' != (c=platform_s_uart_recv(GPS_UART_ID, 0))) { /* 0 = return immediately */
    if(-1 == c)
      break;
    if('\r' == c)
      continue;
    buf[i++] = (char)c;
    if(i == GPS_BUFF_SIZE)
      break;
  }
  buf[i] = 0;
  if(i > 0) {
    if(strstr(buf, "$GPRMC")) {
      parse_gprmc(buf);
    } else if(strstr(buf, "$GPGGA")) {  // Global Positioning System Fix Data
      parse_gpgga(buf);
    } else if(strstr(buf, "$GPGSA")) {  // GPS DOP and active satellites 
      parse_gpgsa(buf);
    } else if(strstr(buf, "$GPGSV")) {  // Satellites in view
      //parse_gpgsv(buf); // TODO
    } else if(strstr(buf, "$GPGLL")) {  // Geographic Position, Latitude / Longitude and time.
      //parse_gpgll(buf); // TODO
    } else if(strstr(buf, "$GPVTG")) {  // Track Made Good and Ground Speed
      //parse_gpvtg(buf); // TODO
    } else if(strstr(buf, "$GPZDA")) {  // Date & Time
      //parse_gpzda(buf); // Not needed ATM, GPRMC has the same data&time data
    } else {
      printf("GPS: input line doesn't match any supported GP sentences!\n");
    }
  }
}

int parse_gpgga(const char *line) {
    int n_sat = 0;
    const char *error;
    
    error = slre_match(0, "^\\$GPGGA,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,([^,]*),[^,]*,[^,]*,[^,]*",
            line, strlen(line),
            SLRE_INT, sizeof(n_sat), &n_sat);
    if(error != NULL) {
        printf("GPS: Error parsing string: %s\n", error);
        return -1;
    } else {
        gps_data.n_satellites = n_sat;
        printf("GPS: Number of satellites in view: %d\n", gps_data.n_satellites);
        return 0;
    }
}

int parse_gpzda(const char *line) {
    char time[15];
    int day, month, year;
    const char *error;

    memset(time, 0, sizeof(time));
    error = slre_match(0, "^\\$GPZDA,([^,]*),([^,]*),([^,]*),([^,]*),[^,]*,[^,]*",
                line, strlen(line),
                SLRE_STRING, sizeof(time), time,
                SLRE_INT, sizeof(day), &day,
                SLRE_INT, sizeof(month), &month,
                SLRE_INT, sizeof(year), &year);
    if(error != NULL) {
        printf("GPS: Error parsing string: %s\n", error);
        return -1;
    } else {
        parse_nmea_time_str(time, &gps_data.dt);
        gps_data.dt.day = day;
        gps_data.dt.month = month;
        gps_data.dt.year = year;
        printf("GPS: hh: %d\n", gps_data.dt.hh);
        printf("GPS: mm: %d\n", gps_data.dt.mm);
        printf("GPS: sec: %d\n", gps_data.dt.sec);
        printf("GPS: msec: %d\n", gps_data.dt.msec);
        printf("GPS: day: %d\n", gps_data.dt.day);
        printf("GPS: month: %d\n", gps_data.dt.month);
        printf("GPS: year: %d\n", gps_data.dt.year);
        return 0;
    }
}

int parse_gpgsa(const char *line) {
    int gps_fix_type;
    double pdop, hdop, vdop;
    const char *error;
    
    error = slre_match(0, "^\\$GPGSA,[^,]*,([^,]*),[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,([^,]*),([^,]*),([^,]*)",
                line, strlen(line),
                SLRE_INT, sizeof(gps_fix_type), &gps_fix_type,
                SLRE_FLOAT, sizeof(pdop), &pdop,
                SLRE_FLOAT, sizeof(hdop), &hdop,
                SLRE_FLOAT, sizeof(vdop), &vdop);
    
    if(error != NULL) {
        //printf("GPS: Error parsing string: %s\n", error);
        return -1;
    } else {
        switch(gps_fix_type) {
        case GPS_FIX_TYPE_NONE:
            gps_data.fix_type = GPS_FIX_TYPE_NONE;
            gps.state = STATE_ON;
            printf("GPS: No GPS fix\n");
            gps_data.lat = 0.0;
            gps_data.lon = 0.0;
            return -1;
            break;
        case GPS_FIX_TYPE_2D:
            gps_data.fix_type = GPS_FIX_TYPE_2D;
            printf("GPS: fix type: 2D\n");
            gps.state = STATE_HAS_2D_FIX;
            break;
        case GPS_FIX_TYPE_3D:
            gps_data.fix_type = GPS_FIX_TYPE_3D;
            printf("GPS: fix type: 3D\n");
            gps.state = STATE_HAS_3D_FIX;
            break;
        default:
            printf("GPS: Error, unknown GPS fix type!");
            return -1;
            break;
        }
        gps_data.fix_type = gps_fix_type;
        gps_data.pdop = pdop;
        gps_data.hdop = hdop;
        gps_data.vdop = vdop;
        /*
        printf("GPS: pdop: %f\n", gps_data.pdop);
        printf("GPS: hdop: %f\n", gps_data.hdop);
        printf("GPS: vdop: %f\n", gps_data.vdop);
        */
        return 0;
    }
}

int parse_gprmc(const char *line) {
    char time[15], status[2], ns[2], ew[2], date[7];
    double lat, lon, speed_ms, heading;
    
    const char *error;
    error = slre_match(0, "^\\$GPRMC,([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),[^,]*,[^,]*,[^,]*",
                line, strlen(line),
                SLRE_STRING, sizeof(time), time,
                SLRE_STRING, sizeof(status), status,
                SLRE_FLOAT, sizeof(lat), &lat,
                SLRE_STRING, sizeof(ns), ns,
                SLRE_FLOAT, sizeof(lon), &lon,
                SLRE_STRING, sizeof(ew), ew,
                SLRE_FLOAT, sizeof(speed_ms), &speed_ms,
                SLRE_FLOAT, sizeof(heading), &heading,
                SLRE_STRING, sizeof(date), date);

    if(error != NULL) {
        printf("GPS: Error parsing string: %s\n", error);
        return -1;
    } else {
        if(strcmp(status, "A") != 0) {
            printf("GPS: No GPS fix\n");
            return -1;
        }
        parse_nmea_time_str(time, &gps_data.dt);
        parse_nmea_date_str(date, &gps_data.dt);
        gps_data.lat = nmeadeg2degree(lat);
        gps_data.lon = nmeadeg2degree(lon);
        /*
        printf("GPS: hh: %d\n", gps_data.dt.hh);
        printf("GPS: mm: %d\n", gps_data.dt.mm);
        printf("GPS: sec: %d\n", gps_data.dt.sec);
        printf("GPS: msec: %d\n", gps_data.dt.msec);
        printf("GPS: day: %d\n", gps_data.dt.day);
        printf("GPS: month: %d\n", gps_data.dt.month);
        printf("GPS: year: %d\n", gps_data.dt.year);
        printf("GPS: status: %s\n", status);
        printf("GPS: latitude: %f\n", lat);
        printf("GPS: ns_indicator: %s\n", ns);
        printf("GPS: longitude: %f\n", lon);
        printf("GPS: ew_indicator: %s\n", ew);
        printf("GPS: speed_ms: %f\n", speed_ms);
        printf("GPS: heading: %f\n", heading);
        */
        return 0;
    }
}

// Time string should be the following format: '093222.000'
void parse_nmea_time_str(char *str, gps_datetime *dt) {
    char tmp[4];

    memcpy(tmp, str, 2);
    tmp[2] = 0;
    dt->hh = strtol(tmp, NULL, 10);
    memcpy(tmp, str+2, 2);
    dt->mm = strtol(tmp, NULL, 10);
    memcpy(tmp, str+4, 2);
    dt->sec = strtol(tmp, NULL, 10);
    memcpy(tmp, str+7, 3);
    dt->msec = strtol(tmp, NULL, 10);
    tmp[3] = 0;
}

// Date string should be the following format: '140413'
void parse_nmea_date_str(char *str, gps_datetime *dt) {
    char tmp[6];

    memcpy(tmp, str, 2);
    tmp[2] = 0;
    dt->day = strtol(tmp, NULL, 10);
    memcpy(tmp, str+2, 2);
    dt->month = strtol(tmp, NULL, 10);
    memcpy(tmp, str+4, 2);
    dt->year = 2000 + strtol(tmp, NULL, 10); // Its past Y2k now
}

double nmeadeg2degree(double val) {
    double deg = ((int)(val / 100));
    val = deg + (val - deg * 100) / 60;
    return val;
}

double degree2nmeadeg(double val) {
    double int_part;
    double fra_part;
    fra_part = modf(val, &int_part);
    val = int_part * 100 + fra_part * 60;
    return val;
}


const LUA_REG_TYPE gps_map[] =
{
#if LUA_OPTIMIZE_MEMORY > 0
  /* FUNCTIONS */
  { LSTRKEY("set_power_state") , LFUNCVAL(gps_set_power_state) },
  { LSTRKEY("has_fix") , LFUNCVAL(gps_has_fix) },
  { LSTRKEY("get_location") , LFUNCVAL(gps_get_location) },

  /* CONSTANTS */
#define MAP(a) { LSTRKEY(#a), LNUMVAL(a) }
  MAP( GPS_POWER_OFF),
  MAP( GPS_POWER_ON),
  MAP( STATE_UNKNOWN),
  MAP( STATE_OFF),
  MAP( STATE_ON ),
  MAP( STATE_HAS_2D_FIX ),
  MAP( STATE_HAS_3D_FIX ),
  MAP( STATE_ERROR ),
#endif
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_gps( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else
#error "Optimize memory=0 is not supported"
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}

#endif	/* BUILD_GPS */

