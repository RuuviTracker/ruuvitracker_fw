-- Common functionality and constants

firmware = {}
firmware.version = "0.1"

options = {
   pin_code = '0000',
   tracking_intervall = 5  -- Interval in secods, 0=real time
}

server = {
   url = "http://dev-server.ruuvitracker.fi/api/v1-dev/",
   tracker_code = "sepeto",
   shared_secret = "sepeto"
}

timers = {
   mem_timer = 9,
   gsm_timer = 10,
   gps_timer = 11,
   tracker_timer = 8
}
