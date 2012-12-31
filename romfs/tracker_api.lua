require('sha1')
require('json')
-- require('http')


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
      if s == '' then
         s = key .. ':' .. value
      else 
         s = s .. '|' .. key .. ':' .. value
      end
   end
   return s
end

local function generate_mac(event, shared_secret) 
   local base = base_string(event)
   local mac = hmac_sha1(shared_secret, base)
   return mac
end

local function create_event_json(event, tracker_code, shared_secret) 
   event.version = 1
   -- TODO if possible, set event.time field here or before
   event.tracker_code = tracker_code
   if shared_secret then
      local mac = generate_mac(event, shared_secret)
      event.mac = mac
   end
   return json.encode(event)
end

--[[--
function send_event(event)
   -- TODO implement actual sending
   local message = create_event_json(event, server.tracker_code, server.shared_secret)
   local code, data = http_post(server.url .. 'events', message, 'application/json')
   -- TODO what to do in case of errors?
end
--]]

--[[--
function ping_server()
   local code, data = http_get(server.url .. 'ping')
   if code ~= "200" then
      return nil
   end
   local success, response = pcall(function() return json.decode(data) end)
   if success then
      return response
   end
   return nil
end
   --]]
--[[
-- Unit tests, uncomment to enable
local function create_test_event() 
   event = {}
   event.version = "1"
   event.tracker_code = "490154203237518"
   event.time = "2012-01-08T21:20:59.000"
   event.latitude =  "4916.46,N"
   event.longitude = "12311.12,W"
   event["X-alarm"] = ""
   event["X-foobar"] = "123"
   event.speed = nil
   return event
end


assert(base_string(create_test_event()) == 'X-alarm:|X-foobar:123|latitude:4916.46,N|longitude:12311.12,W|time:2012-01-08T21:20:59.000|tracker_code:490154203237518|version:1')

assert(generate_mac(create_test_event(), 'VerySecret1') == '070c2873b261ea2d07ec32e532d612a447537cfd')

assert(create_event_json(create_test_event(), "490154203237518", "VerySecret1") == '{"mac":"070c2873b261ea2d07ec32e532d612a447537cfd","tracker_code":"490154203237518","time":"2012-01-08T21:20:59.000","X-alarm":"","longitude":"12311.12,W","latitude":"4916.46,N","X-foobar":"123","version":1}')

assert(create_event_json(create_test_event(), "490154203237518") == '{"tracker_code":"490154203237518","time":"2012-01-08T21:20:59.000","X-alarm":"","longitude":"12311.12,W","latitude":"4916.46,N","X-foobar":"123","version":1}')
   --]]