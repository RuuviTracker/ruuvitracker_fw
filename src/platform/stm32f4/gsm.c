/*
 *  Simcom 908 GSM Driver for Ruuvitracker.
 *
 * @author: Seppo Takalo
 */


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
#include <delay.h>
#include <string.h>
#include <stdlib.h>

#ifdef BUILD_GSM

// For plaform_pio_op() portnums
#define PORT_A 0
#define PORT_B 1
#define PORT_C 2
#define PORT_D 3
#define PORT_E 4

#define STATUS_PIN GPIO_Pin_12
#define STATUS_PORT PORT_E
#define DTR_PIN    GPIO_Pin_14
#define DTR_PORT   PORT_C
#define POWER_PIN  GPIO_Pin_2
#define POWER_PORT PORT_E
#define ENABLE_PIN  GPIO_Pin_15
#define ENABLE_PORT PORT_C

#define BUFF_SIZE	64

#define GSM_CMD_LINE_END "\r\n"

#define TIMEOUT  1000           /* Default timeout 1000ms */

enum State {
  STATE_UNKNOWN = 0,
  STATE_OFF = 1,
  STATE_BOOTING,
  STATE_ASK_PIN,
  STATE_WAIT_NETWORK,
  STATE_READY,
  STATE_ERROR,
};
enum CFUN {
  CFUN_0 = 0,
  CFUN_1 = 1,
};
enum GSM_FLAGS {
  HW_FLOW_ENABLED = 0x01,
  SIM_INSERTED    = 0x02,
  GPS_READY       = 0x04,
  GPRS_READY      = 0x08,
  CALL            = 0x10,
  INCOMING_CALL   = 0x20,
};

/* Modem Status */
struct gsm_modem {
  enum Power_mode power_mode;
  enum State state;
  volatile int waiting_reply;
  volatile int waiting_pattern;
  enum Reply reply;
  int flags;
  enum CFUN cfun;
} static gsm = {		/* Initial status */
  .power_mode=POWER_OFF,
  .state = STATE_OFF,
};


/* Handler functions for AT replies*/
static void handle_ok(char *line);
static void handle_fail(char *line);
static void handle_error(char *line);
static void set_hw_flow();
static void parse_cfun(char *line);
static void gpsready(char *line);
static void sim_inserted(char *line);
static void no_sim(char *line);
static void incoming_call(char *line);
static void call_ended(char *line);
static void parse_caller(char *line);
static void parse_network(char *line);
static void parse_sapbr(char *line);

typedef struct Message Message;
struct Message {
  char *msg;			/* Message string */
  enum State next_state;	/* Atomatic state transition */
  void (*func)();		/* Function to call on message */
};

/* Messages from modem */
static Message urc_messages[] = {
  /* Unsolicited Result Codes (URC messages) */
  { "RDY",                  .next_state=STATE_BOOTING },
  { "+CPIN: NOT INSERTED",  .next_state=STATE_ERROR, .func = no_sim },
  { "+CPIN: READY",         .next_state=STATE_WAIT_NETWORK, .func = sim_inserted },
  { "+CPIN: SIM PIN",       .next_state=STATE_ASK_PIN, .func = sim_inserted },
  { "+CFUN:",               .func = parse_cfun },
  { "Call Ready",           .next_state=STATE_READY },
  { "GPS Ready",            .func = gpsready },
  { "NORMAL POWER DOWN",    .next_state=STATE_OFF },
  { "+COPS:",               .func = parse_network },
  { "+CLCC:",               .func = parse_caller },
  { "+SAPBR:",              .func = parse_sapbr },
  /* Return codes */
  { "OK",   .func = handle_ok },
  { "FAIL", .func = handle_fail },
  { "ERROR",.func = handle_error },
  { "+CME ERROR", .func = handle_error },
  { "+CMS ERROR" },                       /* TODO: handle */
  /* During Call */
  { "NO CARRIER",   .func = call_ended }, /* This is general end-of-call */
  { "NO DIALTONE",  .func = call_ended },
  { "BUSY",         .func = handle_fail },
  { "NO ANSWER",    .func = handle_fail },
  { "RING",         .func = incoming_call },
  { NULL } /* Table must end with NULL */
};

static void incoming_call(char *line)
{
  gsm.flags |= INCOMING_CALL;
  gsm_uart_write("AT+CLCC" GSM_CMD_LINE_END); /* XXX: This is not working! Why? */
}

static void call_ended(char *line)
{
  gsm.flags &= ~CALL;
  gsm.flags &= ~INCOMING_CALL;
}

static void parse_caller(char *line)
{
  char number[64];

  /* Example reply: +CLCC: 1,1,4,0,0,"+35850381xxxx",145,"" */
  if (1 == sscanf(line, "+CLCC: %*d,%*d,4,0,%*d,\"%s", number)) {
    *strchr(number,'"') = 0; //Set number to end to  " mark
    printf("GSM: Incoming call from %s\n", number);
  }
}

static void parse_network(char *line)
{
  char network[64];
  /* Example: +COPS: 0,0,"Saunalahti" */
  if (1 == sscanf(line, "+COPS: 0,0,\"%s", network)) {
    *strchr(network,'"') = 0;
    printf("GSM: Registered to network %s\n", network);
    gsm.state = STATE_READY;
  }
}

#define STATUS_CONNECTING 0
#define STATUS_CONNECTED 1
static const char sapbr_deact[] = "+SAPBR 1: DEACT";

static void parse_sapbr(char *line)
{
  int status;

  /* Example: +SAPBR: 1,1,"10.172.79.111"
   * 1=Profile number
   * 1=connected, 0=connecting, 3,4 =closing/closed
   * ".." = ip addr
   */
  if (1 == sscanf(line, "+SAPBR: %*d,%d", &status)) {
    switch(status) {
    case STATUS_CONNECTING:
    case STATUS_CONNECTED:
      gsm.flags |= GPRS_READY;
      break;
    default:
      gsm.flags &= ~GPRS_READY;
    }
  } else if (0 == strncmp(line, sapbr_deact, strlen(sapbr_deact))) {
    gsm.flags &= ~GPRS_READY;
  }
}

static void sim_inserted(char *line)
{
  gsm.flags |= SIM_INSERTED;
}

static void no_sim(char *line)
{
  gsm.flags &= ~SIM_INSERTED;
}

static void gpsready(char *line)
{
  gsm.flags |= GPS_READY;
}

static void parse_cfun(char *line)
{
  if (strchr(line,'0')) {
    gsm.cfun = CFUN_0;
  } else if (strchr(line,'1')) {
    gsm.cfun = CFUN_1;
  } else {
    printf("GSM: Unknown CFUN state\n");
  }
}
static void handle_ok(char *line)
{
  gsm.reply = AT_OK;
  gsm.waiting_reply = 0;
}

static void handle_fail(char *line)
{
  gsm.reply = AT_FAIL;
  gsm.waiting_reply = 0;
}

static void handle_error(char *line)
{
  gsm.reply = AT_ERROR;
  gsm.waiting_reply = 0;
}


/**
 * Send AT command to modem.
 * Waits for modem to reply with 'OK', 'FAIL' or 'ERROR'
 * return AT_OK, AT_FAIL or AT_ERROR
 */
int gsm_cmd(const char *cmd)
{
  gsm.waiting_reply = 1;

  gsm_uart_write(cmd);
  gsm_uart_write(GSM_CMD_LINE_END);

  while(gsm.waiting_reply)
    delay_ms(1);

  return gsm.reply;
}

/* Wait for specific pattern */
int gsm_wait(const char *pattern, int timeout, char *line)
{
  const char *p = pattern;
  int c;
  int i;

  gsm.waiting_pattern = 1;

  while(*p) {
    c = platform_s_uart_recv(GSM_UART_ID, timeout);
    if (-1 == c)
      return AT_FAIL;
    if (c == *p) { //Match
      p++;
    } else { //No match, go back to start
      p = pattern;
    }
  }

  if (line) {
       strcpy(line, pattern);
       i = strlen(line);
       while ('\n' != (c = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT))) {
	    line[i++] = c;
       }
  }

  gsm.waiting_pattern = 0;

  return AT_OK;
}

/* Setup IO ports. Called in platform_setup() from platform.c */
void gsm_setup_io()
{
  /* Power pin (PE.2) */
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_SET);
  /* DTR pin (PC14) */
  platform_pio_op(DTR_PORT, DTR_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(DTR_PORT, DTR_PIN, PLATFORM_IO_PIN_CLEAR);
  /* Status pin (PE12) */
  platform_pio_op(STATUS_PORT, STATUS_PIN, PLATFORM_IO_PIN_DIR_INPUT);
  /* Enable_voltage (PC15) */
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);

  // Serial port
  platform_uart_setup( GSM_UART_ID, 115200, 8, PLATFORM_UART_PARITY_NONE, PLATFORM_UART_STOPBITS_1);
  platform_s_uart_set_flow_control( GSM_UART_ID, PLATFORM_UART_FLOW_CTS | PLATFORM_UART_FLOW_RTS);
}

static void set_hw_flow()
{
  while(gsm.state < STATE_BOOTING)
    delay_ms(1);

  if (AT_OK == gsm_cmd("AT+IFC=2,2")) {
    gsm.flags |= HW_FLOW_ENABLED;
    gsm_cmd("ATE0"); 		/* Disable ECHO */
  }
}

void gsm_enable_voltage()
{
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);
  delay_ms(100);		/* Give 100ms for voltage to settle */
}

void gsm_disable_voltage()
{
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);
}

void gsm_toggle_power_pin()
{
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_CLEAR);
  delay_ms(2000);
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_SET);
}

/* Send *really* slow. Add inter character delays */
static void slow_send(const char *line) {
  while(*line) {
      platform_s_uart_send(GSM_UART_ID, *line);
    delay_ms(10);		/* inter-char delay */
     if ('\r' == *line)
       delay_ms(100);		/* Inter line delay */
     line++;
  }
}

void gsm_uart_write(const char *line)
{
  if (!gsm.flags&HW_FLOW_ENABLED)
    return slow_send(line);

  while(*line) {
    platform_s_uart_send(GSM_UART_ID, *line);
    line++;
  }
}

/* Find message matching current line */
static Message *lookup_urc_message(const char *line)
{
  int n;
  for(n=0; urc_messages[n].msg; n++) {
    if (0 == strncmp(line, urc_messages[n].msg, strlen(urc_messages[n].msg))) {
      return &urc_messages[n];
    }
  }
  return NULL;
}

/**
 * GSM command and state machine handler.
 * Receives lines from Simcom serial interfaces and parses them.
 */
void gsm_line_received()
{
  Message *m;
  char buf[BUFF_SIZE];
  int c,i=0;

  /* Dont mess with patterns */
  if (gsm.waiting_pattern)
    return;

  while('\n' != (c=platform_s_uart_recv(GSM_UART_ID,0))) {
    if (-1 == c)
      break;
    if ('\r' == c)
      continue;
    buf[i++] = (char)c;
    if(i==BUFF_SIZE)
      break;
  }
  buf[i] = 0;
  if (0 == i)
    return;                     /* Skip empty lines */

  printf("GSM: %s\n", buf);

  m = lookup_urc_message(buf);
  if (m) {
    if (m->next_state) {
      gsm.state = m->next_state;
    }
    if (m->func) {
      m->func(buf);
    }
  }
}

/* Enable GSM module */
void gsm_set_power_state(enum Power_mode mode)
{
  int status_pin = platform_pio_op(STATUS_PORT, STATUS_PIN, PLATFORM_IO_PIN_GET);

  switch(mode) {
  case POWER_ON:
    if (0 == status_pin) {
      gsm_enable_voltage();
      gsm_toggle_power_pin();
      set_hw_flow();
    } else {                    /* Modem already on. Possibly warm reset */
      if (gsm.state == STATE_OFF) {
        gsm_cmd("AT+CPIN?");    /* Check PIN, Functionality and Network status */
        gsm_cmd("AT+CFUN?");    /* Responses of these will feed the state machine */
        gsm_cmd("AT+COPS?");
        gsm_cmd("AT+SAPBR=2,1"); /* Query GPRS status */
        set_hw_flow();
        gsm_cmd("ATE0");
        /* We should now know modem's real status */
        /* Assume that gps is ready, there is no good way to check it */
        gsm.flags |= GPS_READY;
      }
    }
    break;
  case POWER_OFF:
    if (1 == status_pin) {
      gsm_toggle_power_pin();
    }
    break;
  }
}

/* Check if GPS flag is set */
int gsm_is_gps_ready()
{
  if((gsm.flags&GPS_READY) != 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/* -------------------- L U A   I N T E R F A C E --------------------*/

int gsm_lua_set_power_state(lua_State *L)
{
  enum Power_mode next = luaL_checkinteger(L, -1);
  gsm_set_power_state(next);
  return 0;
}

int gsm_power_on(lua_State *L)
{
  gsm_set_power_state(POWER_ON);
  return 0;
}

int gsm_power_off(lua_State *L)
{
  gsm_set_power_state(POWER_OFF);
  return 0;
}

int gsm_send_cmd(lua_State *L)
{
  const char *cmd;
  int ret;

  cmd = luaL_checklstring(L, -1, NULL);
  if (!cmd)
    return 0;

  ret = gsm_cmd(cmd);
  lua_pushinteger(L, ret);

  if (ret != AT_OK) {
    printf("GSM: Cmd failed (%s) returned %d\n", cmd, ret);
  }
  return 1;
}

int gsm_is_pin_required(lua_State *L)
{
  /* Wait for modem to boot */
  while( gsm.state < STATE_ASK_PIN )
    delay_ms(1);		/* Allow uC to sleep */

  lua_pushboolean(L, STATE_ASK_PIN == gsm.state);
  return 1;
}

int gsm_send_pin(lua_State *L)
{
  int ret;
  char cmd[20];
  const char *pin = luaL_checklstring(L, -1, NULL);
  if (!pin)
    return 0;
  snprintf(cmd,20,"AT+CPIN=%s", pin);

  /* Wait for modem to boot */
  while( gsm.state < STATE_ASK_PIN )
    delay_ms(1);		/* Allow uC to sleep */

  if (STATE_ASK_PIN == gsm.state) {
    ret = gsm_cmd(cmd);
    lua_pushinteger(L, ret);
  } else {
    lua_pushinteger(L, AT_OK);
  }
  return 1;
}

int gsm_gprs_enable(lua_State *L)
{
  const char *apn = luaL_checkstring(L, 1);
  char ap_cmd[64];

  /* Check if already enabled */
  gsm_cmd("AT+SAPBR=2,1");

  if (gsm.flags&GPRS_READY)
    return 0;

  snprintf(ap_cmd, 64, "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);

  while (gsm.state < STATE_READY)
    delay_ms(1000);

  gsm_cmd("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
  gsm_cmd(ap_cmd);
  gsm_cmd("AT+SAPBR=1,1");

  if (AT_OK == gsm.reply)
    gsm.flags |= GPRS_READY;
  return gsm.reply;
}

static const char ctrlZ[] = {26, 0};
int gsm_send_sms(lua_State *L)
{
  const char *number;
  const char *text;
  int ret;
  
  number = luaL_checklstring(L, -2, NULL);
  text = luaL_checklstring(L, -1, NULL);
  if (!number || !text)
    return 0;

  if (AT_OK != gsm_cmd("AT+CMGF=1")) {		/* Set to text mode */
    lua_pushinteger(L, AT_ERROR);
    return 1;
  }

  gsm_uart_write("AT+CMGS=\"");
  gsm_uart_write(number);

  gsm_uart_write("\"\r");
  gsm_wait(">", TIMEOUT, NULL);	 /* Send SMS cmd and wait for '>' to appear */
  gsm_uart_write(text);
  ret = gsm_cmd(ctrlZ);		/* CTRL-Z ends message */
  lua_pushinteger(L, ret);
  return 1;
}

typedef enum method_t { GET, POST } method_t; /* HTTP methods */

static void gsm_http_init(const char *url)
{
  char url_cmd[256];
  snprintf(url_cmd, 256, "AT+HTTPPARA=\"URL\",\"%s\"", url);
  gsm_cmd("AT+HTTPINIT");
  gsm_cmd("AT+HTTPPARA=\"CID\",\"1\"");
  gsm_cmd(url_cmd);
  gsm_cmd("AT+HTTPPARA=\"UA\",\"RuuviTracker/SIM908\"");
  gsm_cmd("AT+HTTPPARA=\"REDIR\",\"1\"");
}

static void gsm_http_send_content_type(const char *content_type)
{
  char cmd[256];
  snprintf(cmd,256,"AT+HTTPPARA=\"CONTENT\",\"%s\"", content_type);
  if (AT_OK != gsm_cmd(cmd)) {
    printf("GSM: Failed to set content type\n");
  }
}

static void gsm_http_send_data(const char *data)
{
  char cmd[256];
  snprintf(cmd, 256, "AT+HTTPDATA=%d,1000" GSM_CMD_LINE_END, strlen(data));
  gsm_uart_write(cmd);
  gsm_wait("DOWNLOAD", TIMEOUT, NULL);
  gsm_uart_write(data);
  gsm_cmd(GSM_CMD_LINE_END);  /* Send empty command to wait for OK */
  if (AT_OK != gsm.reply)
    printf("GSM: Failed to load query data\n");
}

int gsm_http_handle(lua_State *L, method_t method,
                    const char *data, const char *content_type)
{
  int i,status,len;
  char ret[64];
  const char *url = luaL_checkstring(L, 1);
  const char *p,*httpread_pattern = "+HTTPREAD";
  char *buf=0;
  char c;

  gsm_http_init(url);
  if (AT_OK != gsm.reply) {
    printf("GSM: Failed to initialize HTTP\n");
    goto HTTP_END;
  }

  if (content_type) {
    gsm_http_send_content_type(content_type);
    if (AT_OK != gsm.reply)
      goto HTTP_END;
  }

  if (data) {
    gsm_http_send_data(data);
    if (AT_OK != gsm.reply)
      goto HTTP_END;
  }

  if (GET == method)
    gsm_cmd("AT+HTTPACTION=0");
  else
    gsm_cmd("AT+HTTPACTION=1");

  gsm_wait("+HTTPACTION", TIMEOUT, ret); //TODO: timeouts

  if (2 != sscanf(ret, "+HTTPACTION:%*d,%d,%d", &status, &len)) { /* +HTTPACTION:<method>,<result>,<lenght of data> */
    printf("Failed to parse response\n");
    goto HTTP_END;
  }

  /* Read response, if there is any */
  if (len < 0)
    goto HTTP_END;

  if (NULL == (buf = malloc(len))) {
    printf("Out of memory\n");
    goto HTTP_END;
  }
  
  //Now read all bytes. block others from reading.
  gsm.waiting_pattern = 1;
  gsm_uart_write("AT+HTTPREAD" GSM_CMD_LINE_END); //Wait for header
  p = httpread_pattern;
  while(*p) {           /* Wait for "+HTTPREAD:<data_len>" */
    c = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
    if (c == *p) { //Match
      p++;
    } else { //No match, go back to start
      p = httpread_pattern;
    }
  }
  while ('\n' != platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT))
    ;;                          /* Wait end of line */
  /* Rest of bytes are data */
  for(i=0;i<len;i++) {
    buf[i] = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
  }
  buf[i]=0;
  gsm.waiting_pattern = 0;      /* Release block */

HTTP_END:
  gsm_cmd("AT+HTTPTERM");

  /* Create response structure */
  lua_newtable(L);
  lua_pushstring(L, "status");
  lua_pushinteger(L, status);
  lua_settable(L, -3);
  if (buf) {
    lua_pushstring(L, "content");
    lua_pushstring(L, buf);
    lua_settable(L, -3);
  }
  lua_pushstring(L, "is_success" );
  lua_pushboolean(L, ((200 <= status) && (status < 300))); /* If HTTP response was 2xx */
  lua_settable(L, -3);
  return 1;
}

int gsm_http_get(lua_State *L)
{
  return gsm_http_handle(L, GET, NULL, NULL);
}

int gsm_http_post(lua_State *L)
{
  const char *data = luaL_checkstring(L, 2); /* 1 is url, it is handled in http_handle function */
  const char *content_type = luaL_checkstring(L, 3);
  return gsm_http_handle(L, POST, data, content_type);
}

int gsm_lua_wait(lua_State *L)
{
  int ret,timeout=TIMEOUT;
  char line[256];
  const char *text;
  text = luaL_checklstring(L, 1, NULL);
  if (2 <= lua_gettop(L)) {
       timeout = luaL_checkinteger(L, 2);
  }
  ret = gsm_wait(text, timeout, line);
  lua_pushinteger(L, ret);
  lua_pushstring(L, line);
  return 2;
}

int gsm_state(lua_State *L)
{
  lua_pushinteger(L, gsm.state);
  return 1;
}


int gsm_is_ready(lua_State *L)
{
  lua_pushboolean(L, gsm.state == STATE_READY);
  return 1;
}

int gsm_flag_is_set(lua_State *L)
{
  int flag = luaL_checkinteger(L, -1);
  lua_pushboolean(L, (gsm.flags&flag) != 0);
  return 1;
}

/* Export Lua GSM library */

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"

const LUA_REG_TYPE gsm_map[] =
{
#if LUA_OPTIMIZE_MEMORY > 0
  /* Functions, Map name to func (I was lazy)*/
#define F(name,func) { LSTRKEY(#name), LFUNCVAL(func) }
  F(set_power_state, gsm_lua_set_power_state) ,
  F(is_ready, gsm_is_ready),
  F(flag_is_set, gsm_flag_is_set),
  F(state, gsm_state),
  F(is_pin_required, gsm_is_pin_required),
  F(send_pin, gsm_send_pin),
  F(send_sms, gsm_send_sms),
  F(cmd, gsm_send_cmd),
  F(wait, gsm_lua_wait),
  F(gprs_enable, gsm_gprs_enable),
  F(power_on, gsm_power_on),
  F(power_off, gsm_power_off),

  /* CONSTANTS */
#define MAP(a) { LSTRKEY(#a), LNUMVAL(a) }
  MAP( POWER_ON ),
  MAP( POWER_OFF ),
  MAP( STATE_OFF),
  MAP( STATE_BOOTING ),
  MAP( STATE_ASK_PIN ),
  MAP( STATE_WAIT_NETWORK ),
  MAP( STATE_READY ),
  MAP( STATE_ERROR ),
  MAP( GPS_READY ),
  MAP( SIM_INSERTED ),
  MAP( GPRS_READY ),
  MAP( INCOMING_CALL ),
  { LSTRKEY("OK"), LNUMVAL(AT_OK) },
  { LSTRKEY("FAIL"), LNUMVAL(AT_FAIL) },
  { LSTRKEY("ERROR"), LNUMVAL(AT_ERROR) },
#endif
  { LNILKEY, LNILVAL }
};

/* Add HTTP related functions to another table */
const LUA_REG_TYPE http_map[] =
{
  F(get, gsm_http_get),
  F(post, gsm_http_post),
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_gsm( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else
#error "Optimize memory=0 is not supported"
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}

#endif	/* BUILD_GSM */
