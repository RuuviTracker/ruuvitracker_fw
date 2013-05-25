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

struct gps_device {
	enum GPS_state state;
	int serial_port_validated;
} static gps = {					/* Initial status */
	.state = STATE_OFF,
	.serial_port_validated = FALSE,	/* For production tests, MCU<->GPS serial line */
};

/* Location data */
struct _gps_data {
    int		fix_type;
    int		n_satellites;

    double	lat;
    double	lon;
    double	speed;
    double	heading;
    double	altitude;

    double	pdop;
    double	hdop;
    double	vdop;

    gps_datetime dt;
} static gps_data = {
  .fix_type     = GPS_FIX_TYPE_NONE,  // Set initial values
  .n_satellites = 0,
  .lat          = 0.0,
  .lon          = 0.0,
  .speed        = 0.0,
  .heading      = 0.0,
  .altitude     = 0.0,
  .pdop         = 0.0,
  .hdop         = 0.0,
  .vdop         = 0.0,
};


/* ===================Lua interface begins=================================== */

/* For production tests, check that MCU<->GPS serial line works */
int gps_validate_serial_port(lua_State *L)
{
  lua_pushboolean(L, gps.serial_port_validated);
  return 1;
}

/* Check if GPS has a fix */
int gps_has_fix(lua_State *L)
{
  lua_pushboolean(L, (gps.state == STATE_HAS_3D_FIX) || 
                      (gps.state == STATE_HAS_2D_FIX));
  
	return 1;
}

int gps_power_on(lua_State *L)
{
	if(gps.state == STATE_OFF) {
		while(gsm_is_gps_ready() != TRUE) {
			delay_ms(100);
		}
		gsm_cmd("AT+CGPSPWR=1");	/* Power-on */
		// TODO: select only a subset of sentences to print
		gsm_cmd("AT+CGPSOUT=255");	/* Select which GP sentences GPS should print */
		gsm_cmd("AT+CGPSRST=1");	/* Do a GPS warm reset */
		gps.state = STATE_ON;
	}
	return 0;
}

int gps_power_off(lua_State *L)
{
	// TODO: handle error states
	if((gps.state == STATE_ON) || (gps.state == STATE_HAS_2D_FIX)
				|| (gps.state == STATE_HAS_3D_FIX)) {
		// TODO: handle other states
		gsm_cmd("AT+CGPSPWR=0");	/* Power-off */
		gps.state = STATE_OFF;
	}
	return 0;
}

int gps_get_location(lua_State *L)
{
 	lua_pushnumber(L, gps_data.lat);
 	lua_pushnumber(L, gps_data.lon);
  return 2;
}

int gps_get_data(lua_State *L)
{
	char timestr[25];
	
	lua_newtable(L);
	lua_pushstring(L, "fix_type");
	lua_pushinteger(L, gps_data.fix_type);
	lua_settable(L, -3);
	
	lua_pushstring(L, "satellite_count");
	lua_pushinteger(L, gps_data.n_satellites);
	lua_settable(L, -3);
	
	lua_pushstring(L, "latitude");
	lua_pushnumber(L, gps_data.lat);
	lua_settable(L, -3);
	
	lua_pushstring(L, "longitude");
	lua_pushnumber(L, gps_data.lon);
	lua_settable(L, -3);
	
	lua_pushstring(L, "speed");
	lua_pushnumber(L, gps_data.speed);
	lua_settable(L, -3);
	
	lua_pushstring(L, "heading");
	lua_pushnumber(L, gps_data.heading);
	lua_settable(L, -3);
	
	// TODO: convert to meters
	lua_pushstring(L, "altitude");
	lua_pushnumber(L, gps_data.altitude);
	lua_settable(L, -3);
	
	lua_pushstring(L, "positional_accuracy");
	lua_pushnumber(L, gps_data.pdop);
	lua_settable(L, -3);
	
	// TODO: convert to meters
	lua_pushstring(L, "vertical_accuracy");
	lua_pushnumber(L, gps_data.vdop);
	lua_settable(L, -3);
	
	lua_pushstring(L, "accuracy");
	lua_pushnumber(L, gps_data.hdop);
	lua_settable(L, -3);
	
	// Construct ISO compatible time string, 2012-01-08T20:57:30.123Z
	sprintf(timestr, "%d-%02d-%02dT%02d:%02d:%02d.%03dZ",
		gps_data.dt.year,
		gps_data.dt.month,
		gps_data.dt.day,
		gps_data.dt.hh,
		gps_data.dt.mm,
		gps_data.dt.sec,
		gps_data.dt.msec);
	lua_pushstring(L, "time");
	lua_pushstring(L, timestr);
	lua_settable(L, -3);
	return 1;
}

/* ===================Internal functions begin=============================== */

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
  	if(strstr(buf, "IIII")) {
  		return;				// GPS module outputs a 'IIII' as the first string, skip it
  	}
    if(calculate_gps_checksum(buf)) {
      gps.serial_port_validated = TRUE;
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
        printf("GPS: input line '%s' doesn't match any supported GP sentences!\n", buf);
      }
    } else {
      printf("GPS: error, calculated checksum does not match received!\n");
    }
  }
}

int calculate_gps_checksum(const char *data) {
  char checksum = 0;
  char received_checksum = 0;
  char *checksum_index;

  if((checksum_index = strstr(data, "*")) == NULL) { // Find the beginning of checksum
    printf("GPS: error, cannot find the beginning of checksum from input line '%s'\n", data);
    return FALSE;
  }
  sscanf(checksum_index + 1, "%02hhx", &received_checksum);
  
  /* Loop through data, XORing each character to the next */
  data = strstr(data, "$");
  data++;
  while(*data != '*') {
    checksum ^= *data;
    data++;
  }
  
  if(checksum == received_checksum) {
    return TRUE;
  } else {
    return FALSE;
  }
}

int parse_gpgga(const char *line) {
    int n_sat = 0;
	double altitude = 0.0;
    const char *error;

    error = slre_match(0, "^\\$GPGGA,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,([^,]*),[^,]*,([^,]*),[^,]*",
            line, strlen(line),
            SLRE_INT, sizeof(n_sat), &n_sat,
            SLRE_FLOAT, sizeof(altitude), &altitude);
    if(error != NULL) {
        //printf("GPS: Error parsing GPGGA string '%s': %s\n", line, error);
        return -1;
    } else {
	if(gps_data.n_satellites != n_sat) {
	        printf("GPS: Number of satellites in view: %d\n", n_sat);
	}
        gps_data.n_satellites = n_sat;
	gps_data.altitude = altitude;
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
        printf("GPS: Error parsing GPZDA string '%s': %s\n", line, error);
        return -1;
    } else {
        parse_nmea_time_str(time, &gps_data.dt);
        gps_data.dt.day = day;
        gps_data.dt.month = month;
        gps_data.dt.year = year;
        /*
        printf("GPS: hh: %d\n", gps_data.dt.hh);
        printf("GPS: mm: %d\n", gps_data.dt.mm);
        printf("GPS: sec: %d\n", gps_data.dt.sec);
        printf("GPS: msec: %d\n", gps_data.dt.msec);
        printf("GPS: day: %d\n", gps_data.dt.day);
        printf("GPS: month: %d\n", gps_data.dt.month);
        printf("GPS: year: %d\n", gps_data.dt.year);
		*/
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
		// TODO: add a check for incomplete sentence
        //printf("GPS: Error parsing GPGSA string '%s': %s\n", line error);
        return -1;
    } else {
        switch(gps_fix_type) {
        case GPS_FIX_TYPE_NONE:
            if(gps_data.fix_type != GPS_FIX_TYPE_NONE)
 	           printf("GPS: No GPS fix\n");
            gps_data.fix_type = GPS_FIX_TYPE_NONE;
            gps.state = STATE_ON;
            gps_data.lat = 0.0;
            gps_data.lon = 0.0;
            return -1;
            break;
        case GPS_FIX_TYPE_2D:
            if(gps_data.fix_type != GPS_FIX_TYPE_2D)
	            printf("GPS: fix type 2D\n");
            gps_data.fix_type = GPS_FIX_TYPE_2D;
            gps.state = STATE_HAS_2D_FIX;
            break;
        case GPS_FIX_TYPE_3D:
            if(gps_data.fix_type != GPS_FIX_TYPE_3D)
	            printf("GPS: fix type 3D\n");
            gps_data.fix_type = GPS_FIX_TYPE_3D;
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
        printf("GPS: Error parsing GPRMC string '%s': %s\n", line, error);
        return -1;
    } else {
        if(strcmp(status, "A") != 0) {
            return -1;
        }
        parse_nmea_time_str(time, &gps_data.dt);
        parse_nmea_date_str(date, &gps_data.dt);
        gps_data.lat = nmeadeg2degree(lat);
        if(strstr(ns, "S")) {
        	gps_data.lat = -1 * gps_data.lat;
        }
        gps_data.lon = nmeadeg2degree(lon);
        if(strstr(ew, "W")) {
        	gps_data.lon = -1 * gps_data.lon;
        }
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
  { LSTRKEY("power_on") , LFUNCVAL(gps_power_on) },
  { LSTRKEY("power_off") , LFUNCVAL(gps_power_off) },
  { LSTRKEY("has_fix") , LFUNCVAL(gps_has_fix) },
  { LSTRKEY("get_location") , LFUNCVAL(gps_get_location) },
  { LSTRKEY("get_data") , LFUNCVAL(gps_get_data) },
  { LSTRKEY("is_receiving"), LFUNCVAL(gps_validate_serial_port) },

  /* CONSTANTS */
#define MAP(a) { LSTRKEY(#a), LNUMVAL(a) }
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

