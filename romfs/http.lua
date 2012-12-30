require('common')
require('gsm')

function http_get(url)
	local data = ""
	local user_agent = "RuuviTracker firmware/" .. firmware.version
	--Start HTTP request
	gsm_cmd([[
AT+HTTPINIT
AT+HTTPPARA="CID","1"
AT+HTTPPARA="URL","]]..url..[["
AT+HTTPPARA="UA","]]..user_agent..[["
AT+HTTPPARA="REDIR","1"]])
	gsm_send('AT+HTTPACTION=0')
	ret = gsm_wait("^%+HTTPACTION")
	local code
	local len
	_,_,code,len = string.find(ret,"%+HTTPACTION:0,(%d+),(%d+)")
	if code == "200" then
		if gsm_verbose==1 then print("Reading "..len.." bytes from "..url) end
		gsm_send('AT+HTTPREAD')
		gsm_wait("^%+HTTPREAD:"..len)
		--All bytes are data after this
		repeat
			local buf = uart.read(2,len)
			len = len-buf:len()
			data = data .. buf
		until len == 0
		--All bytes read, Wait for OK
		gsm_wait("^OK")
	else
		print("Error "..code)
	end
	--Terminate HTTP commands
	gsm_cmd("AT+HTTPTERM")
	return data
end

--URL Encode
--From http://lua-users.org/wiki/StringRecipes
function url_encode(str)
  if (str) then
    str = string.gsub (str, "\n", "\r\n")
    str = string.gsub (str, "([^%w ])",
        function (c) return string.format ("%%%02X", string.byte(c)) end)
    str = string.gsub (str, " ", "+")
  end
  return str	
end

function http_post(url, data, content)
	if content == nil then
		data = url_encode(data)
		content = 'application/x-www-form-urlencoded'
	end
	local user_agent = "RuuviTracker firmware/" .. firmware.version
	gsm_cmd([[
AT+HTTPINIT
AT+HTTPPARA="CID","1"
AT+HTTPPARA="URL","]]..url..[["
AT+HTTPPARA="UA","]]..user_agent..[["
AT+HTTPPARA="CONTENT","]]..content..[["
AT+HTTPPARA="REDIR","1"]])
	gsm_send('AT+HTTPDATA='..data:len()..',1000')
	gsm_wait("^DOWNLOAD")
	gsm_send(data)
	gsm_wait("^OK")
	gsm_send('AT+HTTPACTION=1')
	local ret = gsm_wait("^%+HTTPACTION")
	local code
	local len
	data = ""
	_,_,code,len = string.find(ret,"%+HTTPACTION:1,(%d+),(%d+)")
	if code == "200" then
		if gsm_verbose==1 then print("Reading "..len.." bytes from "..url) end
		gsm_send('AT+HTTPREAD')
		gsm_wait("^%+HTTPREAD:"..len)
		--All bytes are data after this
		repeat
			local buf = uart.read(2,len)
			len = len-buf:len()
			data = data .. buf
		until len == 0
		--All bytes read, Wait for OK
		gsm_wait("^OK")
	else
		print("Error "..code)
	end
	gsm_cmd("AT+HTTPTERM")
	return code,data
end

--url = 'http://dev-server.ruuvitracker.fi/api/v1/events'
--data = '{ "version": "1", "tracker_code": "foobar", "latitude": 65.0399, "longitude": 25.2495 }'
--url = 'http://st.dev.frontside.fi/post.php'
--data = "abba=1"
--print(http_post(url, data, "application/json" ))
