require('gsm')
require('logging')

-- Start package 'gps'
module(..., package.seeall)

local enabled = false
local gps_uart = 1
local logger = Logger:new('gps')

timeout = 100e3 -- Timeout 100ms for messages to arrive

is_fixed = false
local satellites = 0
event = {}

local function enable()
   logger:debug("enabling")
   uart.setup(gps_uart, 115200, 8, uart.PAR_NONE, uart.STOP_1)
   logger:info("Waiting for GSM")
   gsm.wait_ready()
   gsm.cmd('AT+CGPSPWR=1')
   gsm.cmd('AT+CGPSRST=0')
   enabled = 1
   logger:info("GPS started")
end

local function parse_rmc(line)
   local time, status, latitude, ns_indicator, longitude,
   ew_indicator, speed_knots, heading, date = line:match("^%$GPRMC,([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)")
   if status ~= "A" then return end

   -- Calculate speed
   local speed_ms = nil
   if speed_knots then
	   speed_ms = 0.51444 * speed_knots
   end

   -- Calculate time and data
   local h,m,s = time:match("(..)(..)(......)") -- GPRMC time is hhmmss.sss
   local dd,mm,yy = date:match("(..)(..)(..)")  -- GPRMC date is ddmmyy
   time = h..':'..m..':'..s
   date = '20'..yy..'-'..mm..'-'..dd
   local timestamp = date .. 'T' .. time .. 'Z'
   -- Get first timestamp as a session_code
   session_code = session_code or timestamp
   -- Fill coordinates
   latitude = latitude..','..ns_indicator
   longitude = longitude..','..ew_indicator

   logger:info("Time: " ..    time)
   logger:info("Date: " ..    date)
   logger:info("Timestamp: " .. timestamp)
   logger:info("Status: "..   status)
   logger:info("Lat: " ..     latitude)
   logger:info("Lon: " ..     longitude)
   logger:info("Speed: " ..   speed_knots .. "knots")
   logger:info("Heading: " .. heading)

   -- Fill the event object
   event.latitude     = latitude
   event.longitude    = longitude
   event.session_code = session_code
   event.speed        = speed_ms
   event.heading      = heading
   event.time         = timestamp
end

local function parse_gga(line)
   local nsatellites = line:match("^%$GPGGA,[^,]*,[^,]*,[^,]*,[^,]*,([^,]*)") -- I need only 5th field
   nsatellites = tonumber(nsatellites) or 0
   if nsatellites ~= satellites then logger:info("Tracking "..nsatellites.." satellites") end
   event.satellite_count = nsatellites
   satellites = nsatellites
end

local function parse_gsa(line)
   local fix = line:match("^%$GPGSA,.,(%d)")
   fix = tonumber(fix) or 1
   -- $GPGSA Fix status
   -- 1 = no fix
   -- 2 = 2D fix
   -- 3 = 3D fix
   if fix == 3 then
      if not is_fixed then logger:info("Found 3D fix") end
      is_fixed = true
   else
      if is_fixed then logger:info("Lost fix") end
      is_fixed = false
   end
end

function is_enabled()
   return enabled
end

local function parser()
   if enabled == false then enable() end
   
   is_fixed = false
   event = {}
   while true do
      local str = uart.read(gps_uart, '*l', timeout)
      
      if str:find("^%$GPGGA") then -- Start of message sequence
	 if event.time then event = {} end -- Forget old data
	 parse_gga(str)
      elseif str:find("^%$GPRMC") then
	 parse_rmc(str)
      elseif str:find("^%$GPGSA") then
	 parse_gsa(str)
      end

      if str ~= "" then logger:debug(str) end
      if str == "" then
	 -- End of messages; wait
	 coroutine.yield()
      end
   end
end

-- Create co-routine
handler = coroutine.create(parser)
