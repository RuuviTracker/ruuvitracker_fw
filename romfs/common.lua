-- Common functionality and constants 

firmware = {}
firmware.version = "0.1"

-- Read values from EEPROM?
-- TODO this assumes that only one server
server = {url = "http://dev-server.ruuvitracker.fi/api/v1-dev/",
          tracker_code = "sepeto",
          shared_secret = "sepeto"
}


