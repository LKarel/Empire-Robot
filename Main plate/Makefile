MCU = atmega32u4

all:
	avr-gcc -Wall -mmcu=atmega32u4 -DF_CPU=16000000UL -Os -c MainPlate.c
	avr-gcc -Wall -mmcu=atmega32u4 -DF_CPU=16000000UL -Os -o MainPlate.hex MainPlate.o