Led = {}
function Led:on()
   pio.pin.setdir(pio.OUTPUT, self.pin)
   pio.pin.sethigh(self.pin)
end
function Led:off()
   pio.pin.setdir(pio.OUTPUT, self.pin)
   pio.pin.setlow(self.pin)
end
function Led:new(pin)
   o = {}
   o.pin = pin
   setmetatable(o, self)
   self.__index = self
   return o
end

if pd.board() == "STM32F4DSCY" then
   -- TODO: add leds for this board
elseif pd.board() == "RUUVIA" then
   green_led = Led:new(pio.PC_14)
   red_led   = Led:new(pio.PC_15)
end

