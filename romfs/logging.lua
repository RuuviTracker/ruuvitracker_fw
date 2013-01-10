-- logging
-- Usage:
-- local logger = Logger:new('gsm')
-- logger:debug("message ...")
-- logger:info("Message sent")
-- logger:warn("Battery 5%")
-- logger:error("Failed to obtain GPS fix")
--
-- Configuring logging levels:
-- Logger.config.ROOT = Logger.level.INFO  -- default level
-- Logger.config.gsm = Logger.level.WARN -- minumum level for gsm category

Logger = {}
Logger.__index = Logger

-- Logger.config contains logging level for categories
-- e.g. Logger.config['gsm'] = Logger.level.INFO
-- Logs only INFO level and higher for gsm category
Logger.config = {}
Logger.level = {DEBUG = 10, INFO = 20, WARN = 30, ERROR = 50, NEVER = 100}
-- Category ROOT is default.
Logger.config = {ROOT = Logger.level.DEBUG}
-- Logger.appender points to a function that stores
-- messages somewhere (memory card, serial connection etc)
Logger.appender = print

function Logger:new(c)
   return setmetatable({category = c or ''}, Logger)
end

function Logger:log(level, message)
   local category_level = self.config[self.category] or self.config.ROOT or 0
   local message_level = self.level[level]
   -- TODO: get timestamp and add it to the message
   if category_level <= message_level then
      self.appender(level .. ' ' .. self.category .. ': ' .. message)
   end
end

-- Log debug and trace information
-- Not used in normal operation
function Logger:debug(message)
   self:log('DEBUG', message)
end

-- Log normal operation events
-- e.g. message sent
function Logger:info(message)
   self:log('INFO', message)
end

-- Log unusual and noteworthy conditions
-- e.g. battery charge is low
function Logger:warn(message)
   self:log('WARN', message)
end

-- Log errors
-- e.g. can't connect network
function Logger:error(message)
   self:log('ERROR', message)
end

--[[--
-- testing:
Logger.config['gsm'] = Logger.level.ERROR
logger = Logger:new('gsm')
logger:debug("jeed")
logger:info("jeei")
logger:error("jeee")

logger2 = Logger:new('gps')
logger2:debug("jeed")
logger2:info("jeei")
logger2:error("jeee")

logger:error("jeee2")
   --]]--
