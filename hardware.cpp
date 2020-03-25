// gotta go fast!
#pragma GCC optimize ("-O3")


#include <Arduino.h>
#include "hardware.h"


void glitcherInit() {
	// initialise port pins
	digitalWrite(CARD_NVCCEN_PIN, HIGH);
	digitalWrite(CARD_VCCGLITCH_PIN, LOW);
	pinMode(CARD_NVCCEN_PIN, OUTPUT);
	pinMode(CARD_VCCGLITCH_PIN, OUTPUT);

	digitalWrite(CARD_DATA_TX_PIN, HIGH);
	pinMode(CARD_DATA_RX_PIN, INPUT_PULLUP);	// Input with pull-up, because there isn't an external PUR (eww)
	pinMode(CARD_DATA_TX_PIN, OUTPUT);

	digitalWrite(CARD_RESET_PIN, HIGH);
	pinMode(CARD_RESET_PIN, OUTPUT);

	digitalWrite(CARD_CLKOUT_PIN, LOW);
	pinMode(CARD_CLKIN_PIN, INPUT);
	pinMode(CARD_CLKOUT_PIN, OUTPUT);

	digitalWrite(SCOPE_TRIGGER_PIN, LOW);
	pinMode(SCOPE_TRIGGER_PIN, OUTPUT);

	digitalWrite(LED_PIN, LOW);
	pinMode(LED_PIN, OUTPUT);
}


void scReset(bool val)
{
	if (val) {
		// Put card in reset
		digitalWrite(CARD_RESET_PIN, LOW);
	} else {
		// Release card from reset
		digitalWrite(CARD_RESET_PIN, HIGH);
	}
}


void scPower(bool val)
{
	if (val) {
		// vcc on
		digitalWrite(CARD_NVCCEN_PIN, LOW);
	} else {
		// vcc off
		digitalWrite(CARD_NVCCEN_PIN, HIGH);
	}
}


void scClockFreerun(bool on)
{
	// Card clock is on OC1A, master clock is 14.31818MHz.
	// That means we can use PWM to generate a 3.5MHz card clock.
	
	if (on) {	
		ICR1  = 3;		// PWM Max
		OCR1A = 1;		// Duty cycle (50:50)
			// 0: 25%
			// 1: 50%
			// 2: 75%

		// Timer off
		TCCR1B = 0;

		// COM1A[1..0]	=   10 = Non-inverted PWM
		// WGM1[3..0]	= 1110 = Mode 14: Fast PWM. ICR1 sets period, OCR1A sets duty.
		// CS1[2..0]	=  001 = Timer enabled, no prescale
		TCCR1A = _BV(COM1A1) | _BV(WGM11);
		TCCR1B = _BV(CS10)   | _BV(WGM13) | _BV(WGM12);
	} else {
		// Wait for the card clock to go low
		while (digitalRead(CARD_CLKOUT_PIN) == HIGH) {} ;

		// Timer off
		TCCR1A = 0;
		TCCR1B = 0;
	}
}
