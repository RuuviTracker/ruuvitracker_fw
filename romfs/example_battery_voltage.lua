-- Example battery voltage reading using the ADC module. This should be run only
-- once per second due to HW reasons.
-- 
-- Author: Tomi Hautakoski

local voltage = 0.0
local adc_id = 8

print("Reading battery cell voltage")

-- Set blocking on
adc.setblocking(adc_id, 1)

-- Start sampling for one value
adc.sample(adc_id, 1)

-- Wait for the sample to be ready and then get it
voltage = adc.getsample(adc_id)
while not voltage == nil do
	voltage = adc.getsample(adc_id)
	ruuvi.delay_ms(10)
end

-- Scale the result and print it
voltage = (voltage/4096)*3.3*2
print(string.format("Battery voltage = %04fV", voltage))

