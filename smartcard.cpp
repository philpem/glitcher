#include <Arduino.h>
#include "SoftwareSerialParity.h"
#include "hardware.h"
#include "smartcard.h"
#include "utils.h"


// Define this to display APDU rx/tx data
#define APDU_DEBUG_DATA


// Smartcard serial port
SoftwareSerialParity scSerial(CARD_DATA_RX_PIN, CARD_DATA_TX_PIN);


// Byte convention -- TRUE for inverse, FALSE for direct
static bool gSmartcardConvention = true;


/**
 * Convert inverse-convention to direct-convention
 */
static uint8_t _inverse(const uint8_t val)
{
	uint8_t c=0;
	for (uint8_t i=0; i<8; i++) {
		if (!(val & (1<<i))) {
			c |= 1<<(7-i);
		}
	}
	return c;
}


void cardInit(void)
{
	// init smartcard serial
	scSerial.begin(9600, ODD, 2);	// 9600bd 8E2, defaults to listening
}


/**
 * Turn card power on/off
 */
void cardPower(const uint8_t on)
{
	if (!on) {
		// card power off
		// hold the card in reset, power off, no clock
		scReset(true);
		scClockFreerun(false);
		//SCDATA(0);
		scPower(false);
	} else {
		// ISO7816 card power up procedure
		scReset(true);
		// hold reset for at least 40,000 3.579MHz clocks = 12ms
		delay(12);
		scPower(true);
		SCDATA(1);		// I/O in receive mode
		scClockFreerun(true);
		scReset(false);
	}
}

// debug: trigger the scope on the first ATR byte
#define ATR_SCOPE_TRIG_FIRSTBYTE

/**
 * Get the ATR from the card.
 */
int cardGetAtr(uint8_t *buf)
{
	int val;
	int n = 0;
	int atrLen = 2;		// TS and T0 are mandatory

	// overall timeout. ISO7816 says ATR should start transmitting after max 20ms
	unsigned long atrWait = millis() + 20;

	// keep looping until we have the whole ATR
	while ((millis() < atrWait) && (n < atrLen)) {
		// read serial byte
		val = scSerial.read();
		if (val == -1) {
			continue;
		}

#ifdef ATR_SCOPE_TRIG_FIRSTBYTE
	if (n == 0) {
		triggerPulse();
	}
#endif

		// convert from inverse convention and save
		val = _inverse(val);
		buf[n++] = val;

		// extend delay
		atrWait += 2;

		// ATR decode
		switch (n) {
			case 1:		// TS -- TODO: set direct/inverse convention
						// 0x3F (sent inv. conv.) == inverse convention
						// 0x3B (sent dir. conv.) == direct convention
				break;
				
			case 2:		// T0
				// TA1, TB1, TC1, TD1 presence bits
				if (val & 0x10) atrLen++;
				if (val & 0x20) atrLen++;
				if (val & 0x40) atrLen++;
				if (val & 0x80) atrLen++;
				
				// historical character length
				atrLen += (val & 0x0F);
				break;	
		}
	}

	// return number of bytes received
	return n;
}


int scReadByte(int timeout_ms)
{
	unsigned long tdone = millis() + timeout_ms;	// FIXME need to figure out what the procedure byte timeout should be
	int val = -1;
	
	while ((millis() < tdone) && (val == -1)) {
		val = scSerial.read();
	}
	if (millis() >= tdone) {
		// timeout
		return -1;
	} else {
		// TODO if Direct convention, return without inverting byte/bit convention
		return _inverse(val);
	}
}


void scWriteByte(uint8_t b)
{
	// TODO if Direct convention, send without inverting byte/bit convention
	scSerial.write(_inverse(b));
}



/**
 * Send an APDU to the card.
 */
uint16_t cardSendApdu(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t len, uint8_t *buf, bool isSend)
{
	int val;
	uint8_t n = 0;
	uint8_t ntt;
	uint16_t sw;

#ifdef APDU_DEBUG_DATA
	// Debug, print apdu header
	Serial.print(">> ");
	printHex(cla); Serial.print(' ');
	printHex(ins); Serial.print(' ');
	printHex(p1 ); Serial.print(' ');
	printHex(p2 ); Serial.print(' ');
	printHex(len); Serial.print(' ');
	if (isSend) {
		Serial.print(' ');
		printHexBuf(buf, len);
	}
	Serial.println();
#endif
	
	// Send ISO7816 APDU header -- CLA, INS, P1, P2, LEN
	scSerial.stopListening();
	scWriteByte(cla);
	scWriteByte(ins);
	scWriteByte(p1);
	scWriteByte(p2);
	scWriteByte(len);

	// If this is a receive APDU -- switch back to listen mode
	if (!isSend) {
		scSerial.listen();
	}
		
	while (n < len) {
		// clear byte transfer count
		ntt = 0;
		
		// read procedure byte
		val = scReadByte(200);	// FIXME need to figure out what the byte timeout should be
		
		// check for timeout
		if (val == -1) {
			//Serial.println("PROC tmo");
			return 0xFFFF;		// procedure-byte timeout
		}

#ifdef APDU_DEBUG_DATA
		printHex(val);
		Serial.print(" ");
#endif

		if ((val == ~ins) || (val == ~(ins+1))) {
			// Transfer one data byte
			// TODO: vpp
#ifdef APDU_DEBUG_DATA
			Serial.print("[XFER 1] ");
#endif
			ntt = 1;
		} else if ((val == ins) || (val == (ins+1))) {
			// All remaining data bytes are transferred.
			// TODO: vpp
#ifdef APDU_DEBUG_DATA
			Serial.print("[XFER ALL] ");
#endif
			ntt = (len - n);
		} else if (val == 0x60) {
			// NOP, wait for another procedure byte
#ifdef APDU_DEBUG_DATA
			Serial.print("[NOP/BUSY] ");
#endif
			continue;
		} else if (((val & 0xF0) == 0x60) || ((val & 0xF0) == 0x90)) {
#ifdef APDU_DEBUG_DATA
			Serial.print("[SW1] ");
#endif
			// SW1 received... save SW1 in MSB and receive SW2
			sw = (val << 8);
			val = scReadByte();	// FIXME need to figure out what the byte timeout should be

#ifdef APDU_DEBUG_DATA
			printHex(val);
			Serial.println(" [SW2]");
#endif
			// combine SW1, SW2 and return
			sw = sw | val;
			return sw;
		}

		// any bytes to transfer?
		while (ntt > 0) {
			if (isSend) {
				// transmit
				scWriteByte(buf[n++]);
			} else {
				// receive
				val = scReadByte(); // FIXME need to figure out what the byte timeout should be
				if (val == -1) {
					// Timeout
#ifdef APDU_DEBUG_DATA
					Serial.print("[RX TIMEOUT]");
#endif
					len = 0;
					break;
				} else {
					buf[n++] = val;
#ifdef APDU_DEBUG_DATA
					printHex(val);
					Serial.print(" ");
#endif
				}
			}
			
			ntt--;
		}
	}

#ifdef APDU_DEBUG_DATA
	Serial.println("");
	
	// APDU debug, received data
	Serial.print("<< ");
	printHexBuf(buf, n);
#endif

	// If there was a receive timeout, SW1:SW2 are probably the last two bytes sent
	if (ntt > 0) {
		if (n >= 2) {
			sw = (buf[n-2] << 8) | buf[n-1];
		} else {
			// Insufficient bytes received, rx timeout
			sw = 0xFFFE;
		}
	} else {
		// payload is followed by SW1:SW2
		val = scReadByte();	// FIXME need to figure out what the byte timeout should be
		sw = (val << 8);
	
		// receive SW2
		val = scReadByte();	// FIXME need to figure out what the byte timeout should be
	
		sw = sw | val;
	}

#ifdef APDU_DEBUG_DATA
	// APDU debug, SW1:SW2
	Serial.print(" [");
	Serial.print(sw, HEX);
	Serial.println("]");
#endif
	
	return sw;
}
