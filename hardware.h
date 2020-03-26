#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// Card data receive
#define CARD_DATA_RX_PIN		2
#define CARD_DATA_RX_RPORT		PIND
#define CARD_DATA_RX_BIT		2

// Card data transmit
#define CARD_DATA_TX_PIN		3
#define CARD_DATA_TX_WPORT		PORTD
#define CARD_DATA_TX_BIT		3

// Card reset, 0=reset, 1=run
#define CARD_RESET_PIN			4

// Card clock is assigned to OC1A (timer 1 PWM) so we have full control over card clocking
// including switching this pin to I/O mode (PB1) and clocking manually
#define CARD_CLKOUT_PIN			9
#define CARD_CLKOUT_PORT		PORTB
#define CARD_CLKOUT_TPORT		PINB
#define CARD_CLKOUT_BIT			1


// As we're using the PWM to generate the card clock, we loop it back into
// ICP1 on Timer 1 so we can count clock pulses
#define CARD_CLKIN_PIN			8

// Amber status LED on PD6, 1=on
#define LED_PIN					6

// Test point on PD7, used for triggering an oscilloscope
#define SCOPE_TRIGGER_PIN		7

// -- analog pins = 14 + A-number

// CVCCEN (Card VCC Enable) is PC2, 0=on
#define CARD_NVCCEN_PIN			A2

// CVCCGLITCH (Card VCC Glitch) is PC3, 1=glitch
#define CARD_VCCGLITCH_PIN		A3
#define CARD_VCCGLITCH_PORT		PORTC
#define CARD_VCCGLITCH_TPORT	PINC
#define CARD_VCCGLITCH_BIT		3


// Smartcard RX/TX are on I/O 2 and 3 respectively
// FIXME: 
//#include <SoftwareSerial.h>
//SoftwareSerial cardSer(2,3);  // RX, TX


/***
 * Macros
 */

/// Set Vcc-glitch pin state
#define GLITCH(x)	{ if (x) {CARD_VCCGLITCH_PORT |= (1<<CARD_VCCGLITCH_BIT);} else {CARD_VCCGLITCH_PORT &= ~(1<<CARD_VCCGLITCH_BIT);} }

/// Set clock-out pin state
#define CLK(x)		{ if (x) {CARD_CLKOUT_PORT |= (1<<CARD_CLKOUT_BIT);} else {CARD_CLKOUT_PORT &= ~(1<<CARD_CLKOUT_BIT);} }

/// Set data-out pin state
#define SCDATA(x)	{ if (x) {CARD_DATA_TX_WPORT |= (1<<CARD_DATA_TX_BIT);} else {CARD_DATA_TX_WPORT &= ~(1<<CARD_DATA_TX_BIT);} }





/// Pulse the clock once
#define CLKP1()		{ asm ( \
						"sbi %0, %1 \n" \
						"sbi %0, %1 \n" \
						: : "I" (_SFR_IO_ADDR(CARD_CLKOUT_TPORT)), "I" (CARD_CLKOUT_BIT)	\
						); \
					}
					
/// Pulse the clock twice
#define CLKP2()		{ CLKP1(); CLKP1(); }

/// Pulse the clock four times
#define CLKP4()		{ CLKP2(); CLKP2(); }

/// Pulse the clock eight times
#define CLKP8()		{ CLKP4(); CLKP4(); }


inline static void scClockN(const int n)
{
	byte i = n >> 3;
	byte j = n & 0x07;

	// do blocks of 8
	while (i--) {
		CLKP8();
	}

	// do individual pulses
#if 0
	asm ( \
		"top: \n"
		"sbi %[cktport], %[ckbit] \n"
		"sbi %[cktport], %[ckbit] \n"
		"subi %[j], 1 \n"
		"brne top \n"
		: : [j] "=r" (j), [cktport] "I" (_SFR_IO_ADDR(CARD_CLKOUT_TPORT)), [ckbit] "I" (CARD_CLKOUT_BIT)
	);
#else
	while (j--) {
		CLKP1();
	}
#endif

}

/**
 * Fire the oscilloscope trigger.
 */
inline static void triggerPulse(void)
{
	digitalWrite(SCOPE_TRIGGER_PIN, HIGH);
	digitalWrite(SCOPE_TRIGGER_PIN, HIGH);
	digitalWrite(SCOPE_TRIGGER_PIN, LOW);
}


/**
 * SMARTCARD: Set reset line state.
 * 
 * @param	val		<b>true</b> to reset the card.<br>
 * 					<b>false</b> to release the card from reset.
 */
void scReset(bool val);

/**
 * SMARTCARD: Set power on/off.
 * 
 * @param	val		<b>true</b> to turn Vcc on.<br>
 * 					<b>false</b> to turn Vcc off.
 */
void scPower(bool val);


/**
 * SMARTCARD: Set clock on/off (free running mode).
 * 
 * @note This uses Timer1.
 * 
 * @param	val		<b>true</b> to turn Vcc on.<br>
 * 					<b>false</b> to turn Vcc off.
 */
void scClockFreerun(bool on);


/***************************************************************
 * Function prototypes
 */


/**
 * Initialise the Glitcher hardware.
 */
void glitcherInit();

#endif // HARDWARE_H
