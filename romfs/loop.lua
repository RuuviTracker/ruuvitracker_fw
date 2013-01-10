
function uart_loop(id)
	print("Now in loop, send commands\n-----------------")
	while true do
	l = uart.read(id, '*l', uart.NO_TIMEOUT)
	if l ~= "" then uart.write(0, l) end
	l = uart.read(0, '*l', uart.NO_TIMEOUT)
	if l ~= "" then uart.write(id, l) end
	end
end
