/*
 *  Simcom 908 GPS driver for Ruuvitracker.
 *
 * @author: Tomi Hautakoski
 */

#ifndef _GPS_H_
#define _GPS_H_

#define GPS_FIX_TYPE_NONE 1
#define GPS_FIX_TYPE_2D   2
#define GPS_FIX_TYPE_3D   3

#define GPS_BUFF_SIZE	128

enum GPS_state {
  STATE_UNKNOWN = 0,
  STATE_OFF = 1,
  STATE_ON = 2,
  STATE_HAS_2D_FIX = 3,
  STATE_HAS_3D_FIX = 4,
  STATE_ERROR = 5
};

/* Date and time data */
typedef struct _gps_datetime {
    int hh;
    int mm;
    int sec;
    int msec;
    int day;
    int month;
    int year;
} gps_datetime;

/* C-API */
int gps_validate_serial_port();

/* LUA Application interface */
int gps_power_on(lua_State *L);
int gps_power_off(lua_State *L);
int gps_has_fix(lua_State *L);
int gps_get_location(lua_State *L);
int gps_get_data(lua_State *L);

/* Internals */
int luaopen_gps( lua_State *L );
void gps_setup_io();
void gps_line_received();
int calculate_gps_checksum(const char *data);
int parse_gpzda(const char *line);
int parse_gpgga(const char *line);
int parse_gpgsa(const char *line);
int parse_gprmc(const char *line);
void parse_nmea_time_str(char *str, gps_datetime *dt);
void parse_nmea_date_str(char *str, gps_datetime *dt);
double degree2nmeadeg(double val);
double nmeadeg2degree(double val);

#endif

