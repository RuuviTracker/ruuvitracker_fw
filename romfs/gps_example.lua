-- Example script for getting a GPS location from
-- Simcom 908 with RuuviTracker GPS api.
--
-- Author: Tomi Hautakoski


-- First example. Power up GSM device
print("Powering up GSM\n")
gsm.set_power_state(gsm.POWER_ON)

-- Power up GPS device
gps.power_on()

-- Wait for a GPS fix
print("Setup done, waiting for GPS fix")
repeat ruuvi.delay_ms(1000) until gps.has_fix()

print("Got a GPS fix! Getting current location...")

-- While we have a fix, get the current location
while gps.has_fix() do
    print("\nLUA: ")
    print(gps.get_location())
    ruuvi.delay_ms(1000)
end

