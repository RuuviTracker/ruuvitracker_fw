require('config')
require('logging')

local JSON = require('json')

module('tracker', package.seeall)

local logger = Logger:new('tracker')
local session_code = nil
--[[ event data structure
event = {}
event.version = nil
event.tracker_code = nil
event.time = nil
event.session_code = nil
event.nonce = nil
event.latitude = nil
event.longitude = nil
event.accuracy = nil
event.vertical_accuracy = nil
event.positional_accuracy = nil
event.heading = nil
event.satellite_count = nil
event.battery = nil
event.speed = nil
event.altitude = nil
event.temperature = nil
event.annotation = nil
event.mac = nil
event.password = nil
   --]]

local function base_string(event)
   local sorted_keys = {}
   for key in pairs(event) do
      table.insert(sorted_keys, key)
   end
   table.sort(sorted_keys)
   local s = ""
   for _, key in ipairs(sorted_keys) do
      local value = event[key]
      s = s .. key .. ':' .. value .. '|'
   end
   return s
end

local function generate_mac(event, shared_secret)
   local base = base_string(event)
   local mac = ruuvi.sha1.hmac(shared_secret, base)
   return mac
end


local function create_event_json(event, tracker_code, shared_secret)
   local event = event  -- Do not modify original
   event.version = "1"
   -- TODO if possible, set event.time field here or before
   event.tracker_code = tracker_code
   if shared_secret then
      local mac = generate_mac(event, shared_secret)
      event.mac = mac
   end
   return JSON:encode(event)
end


function send_event(event)
   logger:info("Sending event")
   local message = create_event_json(event, config.tracker.tracker_code, config.tracker.shared_secret)
   logger:debug(message)
   http.post(config.tracker.url .. 'events', message, 'application/json')
end


--[[
function ping_server()
   local response = http.get(server.url .. 'ping')
   if not response.is_success then
      return nil
   end
   local success, response = pcall(function() return json.decode(response.content) end)
   if success then
      return response
   end
   return nil
end
--]]

--[[
-- Unit tests, uncomment to enable
function create_test_event()
   local event = {
      heading      = "90",
      time         = "2013-01-05T15:45:02.000Z",
      speed        = 7.2227376,
      latitude     = "6504.019739,N",
      session_code = "test",
      longitude    = "02525.097090,E"
   }
   return event
end
--]]

function tracker_handler()
   local timer = firmware.timers.tracker_timer
   local intervall = config.tracker.tracking_intervall * 1e6 -- to microseconds
   tmr.setclock(timer, 2e3) -- 2kHz is known to work (on ruuviA), allow intervalls from 0s to 32s
   local counter = tmr.start(timer)
   
   -- Wait for GSM to start
   while not gsm.is_ready() do
       coroutine.yield()
   end

   -- Notify server that we are UP and Running.
   send_event({annotation = "Boot OK"})

   -- Loop
   while true do
      local delta = tmr.gettimediff(timer, counter, tmr.read(timer))
      -- TODO: get timestamp from GPS and check interval against that. Get rid of HW timer
      if delta > intervall then -- Time to send
	 logger:debug("Time to send")
	 if gps.has_fix() then
	    local event={}
	    gps_data = gps.get_data()
	    if session_code == nil then
		session_code = gps_data.time
	    end
	    event.session_code = session_code
	    event.latitude = gps_data.latitude
	    event.longitude = gps_data.longitude
	    event.time = gps_data.time
	    event.speed = gps_data.speed
	    event.accuracy = gps_data.accuracy
	    event.vertical_accuracy = gps_data.vertical_accuracy
	    event.positional_accuracy = gps_data.positional_accuracy
	    event.altitude = gps_data.altitude
	    event.heading = gps_data.heading
	    event.satellite_count = gps_data.satellite_count
	    send_event(event)
	 else
	    logger:debug("no fix")
	 end
	 counter = tmr.start(timer) -- Clear; even when there is no fix, wait another intervall
      end
      coroutine.yield()
   end
end
--create co-routine
handler = coroutine.create(tracker_handler)
