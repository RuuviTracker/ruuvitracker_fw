require('common')  -- Load config
require('logging')

module('gsm', package.seeall)  -- Define module 'gsm'

power_pin = pio.PC_4

local logger = Logger:new('gsm')

local UARTID = 2
local gsm_timeout = 1e6 -- Default timeout 1s
local timer = timers.gsm_timer
local timer_hz = 2000

-- Modem Status
local Status = {
   power_down   = 0,
   start        = 1,
   ask_pin      = 2,
   find_network = 3,
   on_network   = 4,
   start_gprs   = 5,
   ready        = 6,
   error        = -1
}
local status = Status.power_down

-- GSM handler co-routine
local function state_machine()
   -- Adjust my clock
   tmr.setclock(timer, timer_hz)
   while true do
      -- State machine
      if status == Status.power_down then
	 logger:info("Powering up")
	 setup_serial_port()
	 power_toggle()
	 status = Status.start
      elseif status == Status.start then
	 -- Modem booting
	 if wait("RDY") == false then
	    switch_gsm_9600_to_115200()
	    status = Status.error -- Go through error state
	 else
	    logger:info("'RDY'")
	    -- Boot ok, next status
	    status = Status.ask_pin
	    -- Wait modem to boot
	    wait("+CFUN: 1", 3e6)
	    -- Setup HW flow, and echo-off
	    cmd("AT+IFC=2,2\nATE0")
	    logger:info("HW flow control set")
	 end
      elseif status == Status.ask_pin then
	 send("AT+CPIN?")
	 local response = wait("^+CPIN")
	 wait("^OK")
	 if response:find("SIM PIN") then -- pin required
	    cmd("AT+CPIN="..options.pin_code) -- Send pin
	    logger:info("pin ok")
	 else
	    logger:info("pin not required")
	 end
	 -- TODO: Handle Errors, PUK, etc..
	 status = Status.find_network
      elseif status == Status.find_network then
	 logger:info("Waiting for network")
	 local response = wait("^Call Ready", 30e6) -- Wait 30s for GSM network
	 if response == false then
	    status = Status.error -- Did not find network
	    logger:error("No network")
	 else
	    status = Status.on_network
	 end
      elseif status == Status.on_network then
	 send("AT+COPS?")
	 local found, _, network = string.find(wait("^+COPS"),'%+COPS: 0,0,"([^"]*)"')
	 wait("^OK")
	 if found then logger:info("Registered to network " ..network)
	 else logger:warn("Could not find network from +COPS")
	 end
	 status = Status.start_gprs
      elseif status == Status.start_gprs then
	 logger:info("Enabling GPRS")
	 start_gprs()
	 logger:info("GPRS up")
	 status = Status.ready
      elseif status == Status.error then
	 logger:warn("Error state, resetting modem")
	 power_toggle() -- Shut down modem
	 delay(5e6)     -- Wait 5s for modem to be shut down
	 status = Status.power_down
      elseif status == Status.ready then
	 -- Nothing to do
	 coroutine.yield()
      else
	 --Unknown status
	 logger:error("Unknown status")
	 status = Status.error
      end
   end
end
-- Create co-routine from state machine
handler = coroutine.create(state_machine)


-- Wait for GSM ready condition
-- Use from inside of co-routine
function wait_ready()
   while status ~= Status.ready do
      coroutine.yield()
   end
end

-- Test if GSM is ready
-- Use this outside of co-routine
function is_ready()
   if status == Status.ready then
      return true
   else
      return false
   end
end

-- Wait function
-- Use this inside of co-routine
function delay(time)
   local counter = tmr.start(timer)
   local delta = 0
   repeat
      coroutine.yield() -- Give time to other rountines
      delta = tmr.gettimediff(timer, counter, tmr.read(timer))
   until delta > time
end

-- Toggle power pin
function power_toggle()
   pio.pin.setdir(pio.OUTPUT, power_pin)
   pio.pin.setlow(power_pin)
   delay(1e6) -- Hold power low for 1s
   pio.pin.sethigh(power_pin)
end

-- Send string to GSM UART
-- append extra "\r" to serial port
function send(str)
   logger:debug("Send:"..str)
   uart.write(UARTID, str.."\r")
end

-- Read raw bytes from GSM
function read_raw(nbytes)
   local len = tonumber(nbytes)
   local data = ""
   repeat
      local buf = uart.read(UARTID,len)
      len = len - buf:len()
      data = data .. buf
   until len == 0
   return data
end


-- Wait for string pattern to appear from UART
-- Use only inside a co-routine (this yields)
function wait(pattern, timeout)
   local timeout = timeout or gsm_timeout
   local line
   local counter = tmr.start(timer)
   repeat
      line = uart.read(UARTID, '*l', 1e3) -- Wait 1ms for answer
      if line ~= "" then logger:debug(line) end
      if string.find(line, pattern) then return line end -- Found response
      if line == "" then coroutine.yield() end -- No response yet, waiting
   until tmr.gettimediff(timer, counter, tmr.read(timer)) > timeout -- Timeout
   return false
end

-- Send commands, separated by newline,
-- and wait for OK responses to each
-- Use only inside a co-routine (this yields)
function cmd(cmd_str)
   local line
   for line in cmd_str:gmatch("[^\r\n]+") do
      repeat
	 send(line)
      until wait("^OK") ~= false -- Resend CMD if OK not received
   end
end

function setup_serial_port()
   uart.setup(UARTID, 115200, 8, uart.PAR_NONE, uart.STOP_1)
   uart.set_flow_control(UARTID, uart.FLOW_RTS + uart.FLOW_CTS)
end

function switch_gsm_9600_to_115200()
   uart.setup(UARTID, 9600, 8, uart.PAR_NONE, uart.STOP_1)
   delay(50e3) -- Wait 50ms
   send('AT')  -- This is for autobauding to catch
   delay(50e3)
   -- Setup serial to 115200 HW flow control
   send('AT+IPR=115200')
   delay(500e3) -- Wait 0.5s for modem
   -- Don't wait for response, serial speed may already be 115200 and we are sending 9600
end

function start_gprs()
   -- GPRS settings
   return cmd([[
AT+SAPBR=3,1,"CONTYPE","GPRS"
AT+SAPBR=3,1,"APN","internet"
AT+SAPBR=1,1
]])
end
