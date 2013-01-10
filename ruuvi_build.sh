#/bin/sh
# A simple script used for building eLua for the RuuviTracker project

#BOARD=STM32F4DSCY
BOARD=RUUVIA

if [[ "$1" == "clean" ]]; then
	scons board=$BOARD -c
else
scons board=$BOARD romfs=compile prog
#scons board=$BOARD prog

fi
