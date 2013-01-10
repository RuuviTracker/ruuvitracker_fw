
function i2c_test(id, addr)
   print("Setup I2C id="..id)
   speed = i2c.setup(id, i2c.FAST)
   print("Speed="..speed)
   
   print("Send start")
   i2c.start(id)
   print("Select I2C device "..addr)
   i2c.address(id, addr, i2c.TRANSMITTER)
   print("Send subaddress 0x20")
   i2c.write(id, 0x20)
   i2c.stop(id)
   print("Read byte")
   i2c.start(id)
   i2c.address(id, addr, i2c.RECEIVER)
   byte = i2c.read(id, 1)
   i2c.stop(id)
   print(string.format("%02X", string.byte(byte)))
end

--Test LSM303DLHC
--Linear acceleration address is 0011001b = 0x19
--Magnetic interface address is 11110b (0x1E)
i2c_test(0, 0x19)
