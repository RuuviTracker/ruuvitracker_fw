require('common')
require('gsm')
require('logging')

module('http', package.seeall)

local logger = Logger:new('http')
local timeout = 10e6 -- 10s

function get(url)
	local data = ""
	local user_agent = "RuuviTracker firmware/" .. firmware.version
	logger:debug("Get request to "..url)
	-- Start HTTP request
	gsm.cmd([[
AT+HTTPINIT
AT+HTTPPARA="CID","1"
AT+HTTPPARA="URL","]]..url..[["
AT+HTTPPARA="UA","]]..user_agent..[["
AT+HTTPPARA="REDIR","1"]])
	gsm.send('AT+HTTPACTION=0')
	logger:debug("connecting...")
	local ret = gsm.wait("^%+HTTPACTION", timeout)
	local code
	local len
	if ret then
	   logger:debug("connected")
	   _,_,code,len = string.find(ret, "%+HTTPACTION:0,(%d+),(%d+)")
	   if code == "200" then
	      logger:debug("200 OK, Reading "..len.." bytes")
	      gsm.send('AT+HTTPREAD')
	      gsm.wait("^%+HTTPREAD:"..len)
	      -- All bytes are data after this
	      data = gsm.read_raw(len)
	      -- All bytes read, wait for OK
	      gsm.wait("^OK")
	   else
	      logger:warn("Error "..code)
	   end
	else
	   logger:warn("Timeout!")
	end
	-- Terminate HTTP commands
	gsm.cmd("AT+HTTPTERM")
	return code, data
end

-- URL Encode
-- From http://lua-users.org/wiki/StringRecipes
function url_encode(str)
  if str then
    str = string.gsub(str, "\n", "\r\n")
    str = string.gsub(str, "([^%w ])",
        function(c) return string.format("%%%02X", string.byte(c)) end)
    str = string.gsub(str, " ", "+")
  end
  return str
end

-- TODO: Condense get() and post(); quite some code is duplicated
function post(url, data, content)
   if content == nil then
      data = url_encode(data)
      content = 'application/x-www-form-urlencoded'
   end
   local user_agent = "RuuviTracker firmware/" .. firmware.version
   logger:debug("POST request to "..url)
   gsm.cmd([[
AT+HTTPINIT
AT+HTTPPARA="CID","1"
AT+HTTPPARA="URL","]]..url..[["
AT+HTTPPARA="UA","]]..user_agent..[["
AT+HTTPPARA="CONTENT","]]..content..[["
AT+HTTPPARA="REDIR","1"]])
   gsm.send('AT+HTTPDATA='..data:len()..',1000')
   gsm.wait("^DOWNLOAD")
   gsm.send(data)
   gsm.wait("^OK")
   gsm.send('AT+HTTPACTION=1')
   logger:debug("connecting")
   local ret = gsm.wait("^%+HTTPACTION", timeout)
   local code
   local len
   local data = ""
   if ret then
      _,_,code,len = string.find(ret,"%+HTTPACTION:1,(%d+),(%d+)")
      if code == "200" then
	 logger:debug("200 OK, Reading "..len.." bytes")
	 gsm.send('AT+HTTPREAD')
	 gsm.wait("^%+HTTPREAD:"..len)
	 -- All bytes are data after this
	 data = gsm.read_raw(len)
	 -- All bytes read, Wait for OK
	 gsm.wait("^OK")
      else
	 logger:warn("Error "..code)
      end
   else
      logger:warn("Timeout")
   end
   gsm.cmd("AT+HTTPTERM")
   return code, data
end

