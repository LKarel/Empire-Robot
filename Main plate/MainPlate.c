#define __AVR_ATmega32U4__
#define F_CPU 16000000UL
#include <avr/io.h>
#include <stdbool.h>

int main() {
	MCUCR = 1 << JTD;
	MCUCR = 1 << JTD;

	DDRF |= 0b11110011;
	PORTF |= 0b11110011;

	PORTF ^= 0b11110011; //turn on all the leds

	while (1) {
		
	}
}

