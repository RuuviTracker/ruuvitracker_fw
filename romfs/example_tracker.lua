-- Example tracking script

-- load config
require('config')
-- load modules
require('led')
require('tracker')

print("Powering up GSM\n")
gsm.set_power_state(gsm.POWER_ON)
if gsm.is_pin_required() then
    gsm.send_pin( config.gsm.pin_code )
end

-- Power up GPS device
print("Powering up GPS\n")
gps.power_on()
gsm.set_apn(config.gsm.apn)

--[[
-- Debug functions
gps = {}
gps.lat = 65.05
gps.lon = 25.5
gps.fix = false
gps.has_fix = function()
   return gps.fix
end
gps.get_location = function()
   gps.lat = gps.lat+0.001
   gps.lon = gps.lon+0.001
   return gps.lat, gps.lon
end
--]]

-- Main loop
local function mainloop()
   if coroutine.status(tracker.handler) == 'suspended' then
      ok, msg = coroutine.resume(tracker.handler)
      if not ok then print("Error:", msg) end
   end

   if gps.has_fix() then  -- Show green led when GPS has fix
      green_led:on()
   else
      green_led:off()
   end

   if gsm.is_ready() then -- Show RED led on until registered to network
      red_led:off()
   else
      red_led:on()
   end
end

ruuvi.idleloop(mainloop) -- Run forewer
