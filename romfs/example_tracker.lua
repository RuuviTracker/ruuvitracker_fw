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
gps.set_power_state(gps.GPS_POWER_ON)

-- Main loop
local function mainloop()
   if coroutine.status(tracker.handler) == 'suspended' then
      ok, msg = coroutine.resume(tracker.handler)
      if not ok then print("Error:", msg) end
   end

   if gsm.is_ready() and not gsm.flag_is_set(gsm.GPRS_READY) then
      gsm.gprs_enable(config.gsm.apn)
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
