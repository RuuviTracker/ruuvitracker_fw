/*
 *  Simcom 908 GSM Driver for Ruuvitracker.
 *
 * @author: Seppo Takalo
 */
#ifndef _GSM_H
#define _GSM_H

#include "lua.h"
#include "lualib.h"

enum Power_mode { POWER_OFF=0, POWER_ON };
enum Reply { AT_OK=0, AT_FAIL, AT_ERROR };

/* C-API */
int gsm_cmd(const char *cmd); 	/* Send AT command to modem. Returns AT_OK, AT_FAIL or AT_ERROR */
int gsm_wait(const char *pattern, int timeout, char *line); /* wait for pattern to appear */
void gsm_uart_write(const char *line);
void gsm_set_power_state(enum Power_mode mode);
int gsm_is_gps_ready();         /* Check if GPS flag is set in GSM */

/* LUA Application interface */
int gsm_send_cmd(lua_State *L);
int gsm_is_pin_required(lua_State *L);
int gsm_send_pin(lua_State *L);
int gsm_is_ready(lua_State *L);
int gsm_is_gprs_enabled(lua_State *L);
int gsm_gprs_enable(lua_State *L);
int gsm_send_sms(lua_State *L);
int gsm_state(lua_State *L);

/* Internals */
int luaopen_gsm( lua_State *L );
void gsm_line_received();
void gsm_setup_io();
void gsm_toggle_power_pin();
void gsm_enable_voltage();
void gsm_disable_voltage();

#endif	/* GSM_H */
