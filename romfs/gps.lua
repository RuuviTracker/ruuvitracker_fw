require('gsm')
require('tracker_api')

gps_enabled = 0
gps_uart = 1

function gps_enable()
	uart.setup( gps_uart, 115200, 8, uart.PAR_NONE, uart.STOP_1 )
	gsm_cmd('AT+CGPSPWR=1')
	gsm_cmd('AT+CGPSRST=0')
	gps_enabled = 1
end

function gps_parse_rmc(line)
			local time, status, latitude, ns_indicator, longitude,
			ew_indicator, speed_knots, heading, date = line:match("^%$GPRMC,([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)")
			if status == "A" then print("Status: Valid location")
			else print("Not valid location") return end

                        local speed_ms = nil
                        if speed_knots then
                           speed_ms = 0.51444 * speed_knots
                        end
			latitude = latitude..','..ns_indicator
			longitude = longitude..','..ew_indicator
			print("Time: " ..time)
			print("Date: " ..date)
			print("Status: "..status)
			print("Lat:" ..latitude)
			print("Lon:" ..longitude)
                        print("Speed:" .. speed_knots .. "knots")
                        print("Heading:" .. heading)
			print("Sending event")
			local event={}
			--Get first timestamp as a session_code
			session_code = session_code or (date .. '-' .. time)
			event.latitude = latitude
			event.longitude = longitude
			event.session_code = session_code
                        event.speed = speed_ms
                        event.heading = heading

			send_event(event)
end

function gps_looper()
	if gps_enabled==0 then gps_enable() end

	print("Starting GPS loop. Press 'q' to stop")	
	while 1 == 1 do
		local str = uart.read(gps_uart,'*l')
		if str:find("^%$GPRMC") then gps_parse_rmc(str)	end
		if str~="" then print(str) end
		
		q = uart.read(0,'*l',uart.NO_TIMEOUT)
		if q == "q" then break end
	end
end
