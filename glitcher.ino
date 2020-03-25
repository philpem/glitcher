/***
   Chip Glitcher

   ATMega328P
*/


// gotta go fast!
#pragma GCC optimize ("-O3")


#include "hardware.h"
#include "smartcard.h"
#include "utils.h"

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










void setup() {
	// put your setup code here, to run once:

	// init serial port
	Serial.begin(57600);
	while (!Serial) { }

	// init glitcher
	glitcherInit();
	cardPower(0);


	Serial.println(">> GLITCHER " __DATE__ " " __TIME__ "\n");

	cardInit();

	// hold the card in reset, power off, no clock
	cardPower(0);
	delay(100);


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

			uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, 1, buf, APDU_RECV);

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
