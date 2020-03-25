/***
   Chip Glitcher

   ATMega328P
*/


// gotta go fast!
#pragma GCC optimize ("-O3")



#include "SoftwareSerialParity.h"
#include "hardware.h"

//
// next task -- 
//   1. reset.
//   2. clock card until I/O pad goes low (count number of clocks)
//   3. receive byte, count cycles, wait for pad to go high again
//   repeat from 2.
//

//
// also todo
//   - simple power analysis
//       (try to find glitchable loops)
//   - ATR decode
//   - try to find a way to get the hidden commands working? some might be glitchable?
//



/*
void scSendByte(const byte val)
{
	// calculate parity
	byte parity = 0;
	byte n = val;
	while (n) {
		parity = !parity;
		n = n & (n-1);
	}

	// prepare to send
	n=val;

	// send start bit
	SCDATA(0);
	_delay_loop_2(372);	// 1 etu (4 cycles per iteration)

	// send data bits -- inverse convention
	for (byte i=0; i<8; i++) {
		SCDATA((n & 0x80) == 0);
		n <<= 1;
		_delay_loop_2(372);
	}

	// send parity bit
	SCDATA(parity);
	_delay_loop_2(372);	// 1 etu (4 cycles per iteration)

	// send stop bits
	// TODO card can pull low half way thru for 1 or 2 bits in order to indicate parity error -- we should retry
	SCDATA(1);
	_delay_loop_2(372);	// 1 etu (4 cycles per iteration)
	_delay_loop_2(372);	// 1 etu (4 cycles per iteration)
}


// TODO scReadByte with timeout
*/


SoftwareSerialParity scSerial(CARD_DATA_RX_PIN, CARD_DATA_TX_PIN);


/**
 * Turn card power on/off
 */
void cardPower(uint8_t on)
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

/**
 * Convert inverse-convention to direct-convention
 */
uint8_t _inverse(const uint8_t val)
{
	uint8_t c=0;
	for (uint8_t i=0; i<8; i++) {
		if (!(val & (1<<i))) {
			c |= 1<<(7-i);
		}
	}
	return c;
}


void printHex(const uint8_t val)
{
	// zero padded hex
	if (val < 0x10) Serial.print('0');
			
	Serial.print(val, HEX);
}

void printHexBuf(const uint8_t *buf, int len)
{
	for (int i = 0; i < len; i++) {
		printHex(buf[i]);
		if (i != (len-1)) {
			Serial.print(' ');
		}
	}
}

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

int scReadByte(int timeout_ms = 50)		// FIXME figure out default timeout
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


#define APDU_SEND true
#define APDU_RECV false

#define APDU_DEBUG

/**
 * Send an APDU to the card.
 */
uint16_t cardSendApdu(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t len, uint8_t *buf, bool isSend, bool debug=true)
{
	int val;
	uint8_t n = 0;
	uint8_t ntt;
	uint16_t sw;

#ifdef APDU_DEBUG
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

		if ((val == ~ins) || (val == ~(ins+1))) {
			// Transfer one data byte
			// TODO: vpp
			//Serial.println("XFER 1");
			ntt = 1;
		} else if ((val == ins) || (val == (ins+1))) {
			// All remaining data bytes are transferred.
			// TODO: vpp
			//Serial.print(ins, HEX);
			//Serial.println("--> XFER ALL");
			ntt = (len - n);
		} else if (val == 0x60) {
			// NOP, wait for another procedure byte
			//Serial.println("NOP");
			continue;
		} else if (((val & 0xF0) == 0x60) || ((val & 0xF0) == 0x90)) {
#ifdef APDU_DEBUG
			Serial.print("SW1: ");
			Serial.print(val, HEX);
			Serial.println("...");
#endif
			// SW1 received but not 0x60. save SW1 in MSB.
			sw = (val << 8);

			// receive SW2
			val = scReadByte();	// FIXME need to figure out what the byte timeout should be

			sw = sw | val;
			return sw;
		}

#ifdef APDU_DEBUG
		Serial.println("DoXfer");
#endif
		// any bytes to transfer?
		while (ntt > 0) {
			if (isSend) {
				// transmit
				scSerial.write(_inverse(buf[n++]));
			} else {
				// receive
				val = scReadByte(); // FIXME need to figure out what the byte timeout should be
				if (val == -1) {
					// Timeout
#ifdef APDU_DEBUG
					Serial.print("[RX TIMEOUT]\n");
#endif
					len = 0;
					break;
				}
				buf[n++] = val;
			}
			
			ntt--;
		}
	}

#ifdef APDU_DEBUG
	// APDU debug, received data
	Serial.print("<< ");
	printHexBuf(buf, n);
#endif

	// If there was a receive timeout, SW1:SW2 are probably the last two bytes sent
	if (ntt > 0) {
		sw = (buf[n-2] << 8) | buf[n-1];
	} else {
		// payload is followed by SW1:SW2
		val = scReadByte();	// FIXME need to figure out what the byte timeout should be
		sw = (val << 8);
	
		// receive SW2
		val = scReadByte();	// FIXME need to figure out what the byte timeout should be
	
		sw = sw | val;
	}

#ifdef APDU_DEBUG
	// APDU debug, SW1:SW2
	Serial.print(" [");
	Serial.print(sw, HEX);
	Serial.println("]");
#endif
	
	return sw;
}

void setup() {
	// put your setup code here, to run once:

	// init serial port
	Serial.begin(57600);
	while (!Serial) { }

	// init glitcher
	glitcherInit();
	cardPower(0);


	Serial.println(">> GLITCHER " __DATE__ " " __TIME__ "\n");


	// hold the card in reset, power off, no clock
	cardPower(0);
	delay(100);

	// init smartcard serial
	scSerial.begin(9600, ODD, 2);	// 9600bd 8E2, defaults to listening

	///////
	// POWER UP AND GET ATR

	Serial.println("Card powering up...");
	cardPower(1);

	// wait max of 12ms for ATR
	uint8_t atr[32];
	uint8_t atrLen;
	atrLen = cardGetAtr(atr);

	Serial.print("ATR Len=");
	Serial.print(atrLen);
	Serial.println(" bytes");
	
	Serial.print("ATR: ");
	printHexBuf(atr, atrLen);

	Serial.println();
	Serial.println();


#if 1
	////////
	// CLA/INS SCAN

	uint8_t buf[32];

	// CLA 0xFF is reserved for PTS
	// Sky 07 cards don't seem to check the classcode. ???
	uint8_t cla = 0x53;
	{
		Serial.println();
		Serial.print("CLA ");
		Serial.print(cla, HEX);
		Serial.print(": ");

		//uint8_t ins = 0x70; {
		for (uint16_t ins=0x6c; ins<0xFE; ins += 2) {
			// INS is only valid if LSBit = 0 and MSN is not 6 or 9
			if ( ((ins >> 8) == 6) || ((ins >> 8) == 9) || (ins & 1)) {
				continue;
			}

			// skip known instructions
			if ((ins >= 0x70) && (ins <= 0x82)) {
				Serial.print("skipping known INS ");
				Serial.println(ins, HEX);
				continue;
			}

			uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, 0, buf, false);

				Serial.print(cla, HEX);
				Serial.print('/');
				Serial.print(ins, HEX);
				Serial.print(" -- sw1sw2=");
				Serial.print(sw1sw2, HEX);
				Serial.println();

			if (sw1sw2 == 0xFFFF) {
				Serial.println("comms err, rebooting card");
				// reset and ATR, comms error
				scReset(true);
				delay(10);
				scReset(false);
				atrLen = cardGetAtr(atr);
				delay(10);
			}
				
#if 0
			
			if (sw1sw2 == 0xFFFF) {
				Serial.print(".");
//				continue;
			}
			if ((sw1sw2 >> 8) != 0x6D) {
				Serial.print("Found valid CLA/INS: ");
				Serial.print(cla, HEX);
				Serial.print('/');
				Serial.print(ins, HEX);
				Serial.print(" -- sw1sw2=");
				Serial.print(sw1sw2, HEX);
				Serial.println();
			}
#endif

			Serial.println();

		}
	}
	Serial.println("All done.");
#endif



#if 0
	////////
	// Check some CLA/INS combinations we're curious about

	//// CMD 70

	uint8_t apduBuf[200];
	uint16_t sw1sw2;

	Serial.println("Read CSN...");

	digitalWrite(SCOPE_TRIGGER_PIN, HIGH);
	digitalWrite(SCOPE_TRIGGER_PIN, HIGH);
	digitalWrite(SCOPE_TRIGGER_PIN, LOW);
	
	sw1sw2 = cardSendApdu(0x53, 0x70, 0, 0, 6, apduBuf, APDU_RECV);
	Serial.print("send apdu, sw1sw2: ");
	Serial.println(sw1sw2, HEX);

	// send buffer
	Serial.print("card serial:");
	printHexBuf(apduBuf, 6);
	Serial.println("\n");


	// CMD 6C. Seems to be card reset.
	Serial.println("CLA 53 INS 6C...");
	sw1sw2 = cardSendApdu(0x53, 0x6C, 0, 0, 32, apduBuf, APDU_SEND);
	Serial.print("send apdu, sw1sw2: ");
	Serial.println(sw1sw2, HEX);
	Serial.print("BUF: ");
	printHexBuf(apduBuf, 32);
	Serial.println("\n");


	Serial.println("CLA 53 INS 88...");
	sw1sw2 = cardSendApdu(0x53, 0x88, 0, 0, 2, apduBuf, APDU_RECV);
	Serial.print("send apdu, sw1sw2: ");
	Serial.println(sw1sw2, HEX);
	Serial.print("BUF: ");
	printHexBuf(apduBuf, 2);
	Serial.println("\n");


	memset(apduBuf, '\0', sizeof(apduBuf));

	Serial.println("CLA 53 INS 84...");
	sw1sw2 = cardSendApdu(0x53, 0x84, 0, 0, 200, apduBuf, APDU_SEND);
	Serial.print("send apdu, sw1sw2: ");
	Serial.println(sw1sw2, HEX);
	Serial.print("BUF: ");
	printHexBuf(apduBuf, 32);
	Serial.println("\n");

	Serial.println("CLA 53 INS 86...");
	sw1sw2 = cardSendApdu(0x53, 0x86, 0, 0, 200, apduBuf, APDU_RECV);
	Serial.print("send apdu, sw1sw2: ");
	Serial.println(sw1sw2, HEX);
	Serial.print("BUF: ");
	printHexBuf(apduBuf, 32);
	Serial.println("\n");
#endif
	
}


void loop() {
	// put your main code here, to run repeatedly:
#if 0

	/*
	  Serial.println("blink\n");

	  digitalWrite(LED_PIN, HIGH);
	  delay(1000);
	  digitalWrite(LED_PIN, LOW);
	  delay(1000);
	*/

  
	//Serial.println("glitch");

	digitalWrite(LED_PIN, HIGH);


	// interrupts off
	noInterrupts();
	
#if 0
	digitalWrite(CARD_VCCGLITCH_PIN, HIGH);
	digitalWrite(CARD_VCCGLITCH_PIN, LOW);
#else
	GLITCH(1);
	GLITCH(0);
#endif

	//scReset(true);
	//scReset(false);

	//digitalWrite(CARD_CLKOUT_PIN, HIGH);
	//digitalWrite(CARD_CLKOUT_PIN, LOW);

	// clock loop
	scClockN(5);

	// gap
	CLK(0);
	CLK(0);
	CLK(0);


/*
	// Glitched clock
	GLITCH(1);
	CLK(1);
	GLITCH(0);
	CLK(0);
*/
	//scReset(true);
	//scReset(false);

	scSendByte(0x3F);


	for (byte i=0; i<100; i++) {
		CLK(0);
	}


	//delay(50);
	digitalWrite(LED_PIN, LOW);
	//delay(50);

	// interrupts on
	interrupts();
#endif
}
