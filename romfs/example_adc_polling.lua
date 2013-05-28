-- Example reading of battery voltage and high input voltage using the ADC module.
-- Use timer 1 with polling for available samples.
-- 
-- Author: Tomi Hautakoski

if pd.board() == "RUUVIB1" then
	timer = 1
	adcchannels = {8, 10}
	adcsmoothing = {1, 1}
end

-- Setup ADC and start sampling
for i, v in ipairs(adcchannels) do
	adc.setblocking(v, 0)			-- no blocking on any channels
	adc.setsmoothing(v, adcsmoothing[i])	-- set smoothing from adcsmoothing table
	adc.setclock(v, 1, timer) 		-- get 1 sample per second, per channel
end

-- Draw static text on terminal
term.clrscr()
term.print(1,1,"ADC Status:")
term.print(1,3,"CH              SLEN   VALUE")
term.print(1,#adcchannels+5,"Press ESC to exit.")

-- start sampling on all channels at the same time 
adc.sample(adcchannels, 128)

while true do
	for i, v in ipairs(adcchannels) do
		-- If samples are not being collected, start
		if adc.isdone(v) == 1 then adc.sample(v, 128) end
	
		-- Try and get a sample
		tsample = adc.getsample(v)
	    	
		-- If we have a new sample, then update display
		if not (tsample == nil) then
			if v == 8 then
			tsample = (tsample/4096)*3.3*2
	       		term.print(1,i+3,string.format("Battery voltage (%03d): %04fV\n",
	       						adcsmoothing[i], tsample))
			end
			if v == 10 then
	    		tsample = (tsample/4096)*3.3*5
	    		term.print(1,i+3,string.format("High input volt (%03d): %04fV\n",
	    						adcsmoothing[i], tsample))
			end
		end
	end

	-- Exit if user hits Escape
	key = term.getchar( term.NOWAIT )
	if key == term.KC_ESC then break end 
end

term.clrscr()
term.moveto(1, 1)

