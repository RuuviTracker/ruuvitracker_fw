require('common')
require('led')
require('tracker')
require('gsm')
require('gps')
require('logging')

-- Start booting
red_led:on()
term.clrscr()
print("Starting RuuviTracker eLua FW", firmware.version)
print("Board: " .. pd.board())

local ok
local msg

-- List of handlers to process on main loop
local handlers = {
   gsm.handler,
   gps.handler,
   tracker.handler
}

function print_mem()
   print("Memory usage: ", collectgarbage('count'), "kB")
end

Logger.config['gsm'] = Logger.level.INFO
Logger.config['gps'] = Logger.level.INFO

local mem_timeout = 10e6 -- 10s
tmr.setclock(timers.mem_timer, 2e3 ) -- 2kHz is known to work (on ruuviA)
local counter = tmr.start(timers.mem_timer)

print("Starting main loop. Press 'q' to stop")
while uart.read(0,1,0) ~= 'q' do
   -- Run through all handler (coroutines)
   for _, handler in ipairs(handlers) do
      if coroutine.status(handler) == 'suspended' then
	 ok, msg = coroutine.resume(handler)
	 if not ok then print("Error:", msg) end
      end
   end

   -- Other functions, outside of handlers
   if gps.is_fixed then green_led:on()
   else green_led:off() end
   if gsm.is_ready() then
      red_led:off()
   else
      red_led:on()
   end
   
   local delta = tmr.gettimediff(timers.mem_timer, counter, tmr.read(timers.mem_timer))
   if delta > mem_timeout then
      print_mem()
      counter = tmr.start(timers.mem_timer) -- Reset timer
   end
end
