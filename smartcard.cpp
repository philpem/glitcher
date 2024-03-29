#include <Arduino.h>
#include "SoftwareSerialParity.h"
#include "hardware.h"
#include "smartcard.h"
#include "utils.h"


// Define this to display APDU rx/tx data
#define APDU_DEBUG_DATA


// Card clock rate
#define CARD_CLOCK_HZ (3579545)

// Initial ATR baud rate. Always 372 clocks per Etu.
#define ATR_BAUD (CARD_CLOCK_HZ / 372)


// Guard time. 372(etudiv) / 3.579545MHz = ~104us
const unsigned int GUARDTIME = (104*5);


// Smartcard serial port
SoftwareSerialParity scSerial(CARD_DATA_RX_PIN, CARD_DATA_TX_PIN);

// Byte convention -- TRUE for inverse, FALSE for direct
static bool gInverseConvention = false;

// Current smartcard baud rate
static uint32_t gBaudRate = ATR_BAUD;


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
	// 9600bd 8O2, defaults to listening
	cardBaud(ATR_BAUD);

	// turn listening off
	scSerial.stopListening();
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


void cardBaud(const uint32_t baud)
{
	// Direct convention uses even parity
	// Inverse convention uses odd parity
	scSerial.begin(baud, gInverseConvention ? ODD : EVEN, 2);
	gBaudRate = baud;
}



/**
 * Read byte from smartcard
 */
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
		return gInverseConvention ? _inverse(val) : val;
	}
}


/**
 * Write byte to smartcard
 */
void scWriteByte(uint8_t b)
{
	// If Direct convention, send without inverting byte/bit convention
	if (gInverseConvention) {
		scSerial.write(_inverse(b));
	} else {
		scSerial.write(b);
	}
}


// debug: trigger the scope on the first ATR byte
//#define ATR_SCOPE_TRIG_FIRSTBYTE

// ATR state machine states
typedef enum {
	ATRS_TS,
	ATRS_T0,
	ATRS_TA,
	ATRS_TB,
	ATRS_TC,
	ATRS_TD,
} ATR_STATE;

int cardGetAtr(uint8_t *buf)
{
	int val;					// current incoming data byte
	int n = 0;					// byte count
	int atrLen = 2;				// TS and T0 are mandatory
	ATR_STATE state = ATRS_TS;	// ATR state machine state variable
	uint8_t histLen = 0;		// historical character length
	uint8_t tdFlags = 0;		// flags from most recent TDn
	uint8_t atr_ta;				// TA1 value
	bool atr_has_ta = false;	// has TA1? (atr_ta is valid)

	// overall timeout. ISO7816 says ATR should start transmitting after max 20ms
	//  = 40,000 clock cycles
	//  = 11.17ms at 3.579545MHz
	// (we double this for safety)
	// Increased (again!!!) to 500ms because Cryptoworks cards are slow to start up
	//   CW ROM 01 and 03 take 300ms... 05 takes almost a full second!
	unsigned long atrWait = millis() + 1000;

	// reset to ATR baud rate
	cardBaud(ATR_BAUD);

	// start listening for ATR data
	scSerial.listen();

	// keep looping until we have the whole ATR
	while ((millis() < atrWait) && (n < atrLen)) {
		// read serial byte
		val = scSerial.read();
		if (val == -1) {
			continue;
		}

		// extend wait time for every successful byte received
		unsigned long now = millis();
		if (timeAfter(now, atrWait - 10)) {
			// ATR wait remaining is less than 10ms, extend to now plus 10ms
			atrWait = now + 10;
		}

#ifdef ATR_SCOPE_TRIG_FIRSTBYTE
	if (n == 0) {
		triggerPulse();
	}
#endif
		// account for current card convention setting
		val = gInverseConvention ? _inverse(val) : val;

		// If this is TS, use it to identify the card convention (inverse/direct)
		if (n == 0) {
			if ((val == 0x03) || (val == 0x23)) {
				// 0x03: 0x3F sent as Inverse Convention, but we're in Direct Convention
				// 0x23: 0x3B sent as Direct Convention, but we're in Inverse Convention
				// Fix the value then change the card convention.
				val = _inverse(val);
				scSetInverseConvention(!gInverseConvention);
			}
		}
		buf[n++] = val;

		// ATR decode
		switch (state) {
			case ATRS_TS:		// TS
				// This is handled above. Next byte should be T0
				state = ATRS_T0;
				break;

			case ATRS_T0:		// T0 or TD
			case ATRS_TD:
				// TAn, TBn, TCn, TDn presence bits -- in T0 and TDn
				// save the TDn flag and adjust the ATR length
				tdFlags = val;
				if (val & 0x10) atrLen++;
				if (val & 0x20) atrLen++;
				if (val & 0x40) atrLen++;
				if (val & 0x80) atrLen++;

				// historical character length -- only in T0
				if (state == ATRS_T0) {
					histLen = (val & 0x0F);
					atrLen += histLen;
				}

				// next byte will be...
				if (tdFlags & 0x10) {
					state = ATRS_TA;
				} else if (tdFlags & 0x20) {
					state = ATRS_TB;
				} else if (tdFlags & 0x40) {
					state = ATRS_TC;
				} else if (tdFlags & 0x80) {
					state = ATRS_TD;
				}
				break;

			case ATRS_TA:
				if (!atr_has_ta) {
					atr_ta = val;
					atr_has_ta = true;
				}
			case ATRS_TB:
			case ATRS_TC:
				// advance
				if ((tdFlags & 0x10) && (state < ATRS_TB)) {
					state = ATRS_TB;
				} else if ((tdFlags & 0x20) && (state < ATRS_TC)) {
					state = ATRS_TC;
				} else if ((tdFlags & 0x40) && (state < ATRS_TD)) {
					state = ATRS_TD;
				}
				break;	
		}
	}

	// switch baudrate to match TA
	if (atr_has_ta) {
		const uint16_t DI_TABLE[16] = { 0, 1, 2, 4, 8, 16, 32, 64, 12, 20, 0, 0, 0, 0, 0, 0 };
		const uint16_t FI_TABLE[16] = { 372, 372, 558, 744, 1116, 1488, 1860, 0, 0, 512, 768, 1024, 1536, 2048, 0, 0 };
		const uint8_t  FREQ_TABLE[16] = { 40, 50, 60, 80, 120, 160, 200, 0, 0, 50, 75, 100, 150, 200 };		// MHz * 10

		uint16_t di = DI_TABLE[atr_ta & 0x0F];
		uint16_t fi = FI_TABLE[(atr_ta >> 4) & 0x0F];
		uint8_t  freq = FREQ_TABLE[(atr_ta >> 4) & 0x0F];
		uint32_t baud;

		if ((di == 0) || (fi == 0)) {
			Serial.print(F("ERROR: Card has invalid Ta1=0x"));
			Serial.println(atr_ta, HEX);
		} else {
 			baud = (CARD_CLOCK_HZ * di) / fi;
 			Serial.print(F("Card TA1 config: TA1=0x"));
 			Serial.print(atr_ta, HEX);
 			Serial.print(F(" Di="));
 			Serial.print(di);
 			Serial.print(F(" Fi="));
 			Serial.print(fi);
 			Serial.print(F(" Fclk(max)="));
 			Serial.print(freq / 10);
 			Serial.print('.');
 			Serial.print(freq % 10);
 			Serial.print(F(" MHz -- Etu/clk="));
 			Serial.print(fi / di);
 			Serial.print(F("; calculated Baud="));
 			Serial.println(baud);
 			cardBaud(baud);
		}
	}

	// stop listening
	scSerial.stopListening();

	// return number of bytes received
	return n;
}

/**
 * This is the Waiting Time -- WT. ISO7816-3:2006 section 7.2 and 10.2.
 * 
 * WT = WI x 960 x (Fi/f)
 * WT = WI x 960 x (372 / 3579545)
 * WT = 10 x 960 x (372 / 3579545)
 * WT = 1 second
 */
#define APDU_RX_TIMEOUT 1000

/**
 * Send an APDU to the card.
 */
uint16_t cardSendApdu(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t len, uint8_t *buf, bool isSend, uint8_t *procByte, bool debug)
{
	int val;
	uint8_t n = 0;
	uint8_t ntt = 0;
	uint16_t sw;

	if (debug) {
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
	}

	// Send ISO7816 APDU header -- CLA, INS, P1, P2, LEN
	scSerial.stopListening();
	scWriteByte(cla);
	scWriteByte(ins);
	scWriteByte(p1);
	scWriteByte(p2);
	scWriteByte(len);

	// Switch back to listen mode to get the procedure byte
	scSerial.listen();
		
	while (n < len) {
		// clear byte transfer count
		ntt = 0;
		
		// read procedure byte
		val = scReadByte(APDU_RX_TIMEOUT);	// FIXME need to figure out what the byte timeout should be

		if (procByte != NULL) {
			*procByte = val;
		}
		
		// check for timeout
		if (val == -1) {
			if (debug) {
				Serial.println("[PROC tmo]");
			}
			sw = 0xFFFF;		// procedure-byte timeout
			goto done;
		}

		if (debug) {
			printHex(val);
			Serial.print(" ");
		}

		if ((val == ~ins) || (val == ~(ins+1))) {
			// Transfer one data byte
			// TODO: vpp
			if (debug) {
				Serial.print("[XFER 1] ");
			}
			ntt = 1;
		} else if ((val == ins) || (val == (ins+1))) {
			// All remaining data bytes are transferred.
			// TODO: vpp
			if (debug) {
				Serial.print("[XFER ALL] ");
			}
			ntt = (len - n);
		} else if (val == 0x60) {
			// NOP, wait for another procedure byte
			if (debug) {
				Serial.print("[NOP/BUSY] ");
			}
			continue;
		} else if (((val & 0xF0) == 0x60) || ((val & 0xF0) == 0x90)) {
			if (debug) {
				Serial.print("[SW1] ");
			}

			// SW1 received... save SW1 in MSB and receive SW2
			sw = (val << 8);
			val = scReadByte(APDU_RX_TIMEOUT);	// FIXME need to figure out what the byte timeout should be

			if (debug) {
				printHex(val);
				Serial.println(" [SW2]");
			}

			// combine SW1, SW2 and return
			sw = sw | val;
			goto done;
		}

		// any bytes to transfer?
		while (ntt > 0) {
			if (isSend) {
				// transmit
				delayMicroseconds(GUARDTIME);
				scWriteByte(buf[n++]);
			} else {
				// receive
				val = scReadByte(APDU_RX_TIMEOUT); // FIXME need to figure out what the byte timeout should be
				if (val == -1) {
					// Timeout
					if (debug) {
						Serial.print("[RX TIMEOUT]");
					}
					len = 0;
					break;
				} else {
					buf[n++] = val;
					if (debug) {
						printHex(val);
						Serial.print(" ");
					}
				}
			}
			
			ntt--;
		}
	}

	if (debug && !isSend) {
		Serial.println("");
		
		// APDU debug, received data
		Serial.print("<< ");
		printHexBuf(buf, n);
	}

	// If there was a receive timeout, bail
	if (ntt > 0) {
		// Insufficient bytes received, rx timeout
		sw = 0xFFFE;
	} else {
		// payload is followed by SW1:SW2
		val = scReadByte(APDU_RX_TIMEOUT);	// FIXME need to figure out what the byte timeout should be
		sw = (val << 8);
	
		// receive SW2
		val = scReadByte(APDU_RX_TIMEOUT);	// FIXME need to figure out what the byte timeout should be
	
		sw = sw | val;
	}

	// cleanup before exiting
done:
	if (debug) {
		// APDU debug, SW1:SW2
		Serial.print(" [");
		Serial.print(sw, HEX);
		Serial.println("]");
	}

	scSerial.stopListening();
	
	return sw;
}

// Get card convention (autodetected during ATR)
bool scGetInverseConvention(void)
{
	return gInverseConvention;
}

// Set card convention (do this after ATR)
void scSetInverseConvention(bool inv)
{
	gInverseConvention = inv;
	cardBaud(gBaudRate);
}
