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


// Current card power/init state
bool gCardPowerOn = false;

// Debug enable/disable for SCAN
bool gScanDebug = false;



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


/**
 * Utility function: power on the card and display the ATR.
 * 
 * Used by the 'on' and 'reset' commands
 */
void doResetAndATR(void)
{
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

	gCardPowerOn = true;
}


/**
 * Utility function: read and display card serial number
 */
void doSerialNumber(void)
{
	uint8_t buf[8];
	uint16_t sw1sw2;

	if (!gCardPowerOn) {
		Serial.println("Card power is off.");
		return;
	}

	// read serial number
	sw1sw2 = cardSendApdu(0x53, 0x70, 0, 0, 6, buf, APDU_RECV);
	Serial.print("Card issue:  ");
	Serial.println(buf[0] & 0x0F);
	
	unsigned long serial =
		((unsigned long)buf[1] << 24) |
		((unsigned long)buf[2] << 16) |
		((unsigned long)buf[3] << 8)  |
		((unsigned long)buf[4]);
	Serial.print("Card serial: ");
	Serial.print(serial);
	Serial.println("x");
	Serial.println();
}


/**
 * Command handler: cmd84
 * 
 * Based on 84CMD.C.
 * No idea what this does.
 */
void handle_cmd84(String *cmdline)
{
	Serial.println("CMD84 run -- based on 84CMD.C");

	uint8_t buf[256];
	uint16_t sw1sw2;

	doResetAndATR();
	doSerialNumber();

	// send...
	sw1sw2 = cardSendApdu(0x53, 0x84, 0xa3, 0x7d, 0x50, buf, APDU_RECV, NULL, true);

	Serial.print(" -- sw1sw2=");
	Serial.println(sw1sw2, HEX);

}


/**
 * Command handler: off
 * 
 * Switch card power off
 */
void handle_off(String *cmdline)
{
	Serial.print("Powering off card... ");
	cardPower(0);
	Serial.println("done.");

	gCardPowerOn = false;
}


/**
 * Command handler: osd
 *
 * Display current VideoCrypt OSD message
 */
void handle_osd(String *cmdline)
{
	uint8_t buf[25];
	uint16_t sw1sw2;

	if (!gCardPowerOn) {
		Serial.println("Card power is off.");
		return;
	}

	// read OSD
	sw1sw2 = cardSendApdu(0x53, 0x7A, 0, 0, 25, buf, APDU_RECV);

	uint8_t prio = buf[0] >> 5;
	uint8_t len  = buf[0] & 0x1F;
	
	Serial.print("OSD Priority ");
	Serial.print(prio);
	Serial.print(", ");
	Serial.print(len);
	Serial.print(" characters");

	// OSD messages must have a priority of >= 4 to show on clear unencrypted video.
	// Ref. 8052INFO.TXT [1.1.2].
	if (prio < 4) {
		Serial.println(" *HIDDEN*");
	} else {
		Serial.println();
	}

	Serial.print("OSD: [");
	for (int i=1; i<=len; i++) {
		if (buf[i] == '\0') {
			Serial.print(" ");
		} else {
			Serial.print((char)buf[i]);
		}

		if (i == 12) {
			Serial.print("] [");
		}
	}
	Serial.print("]\n");
}


/**
 * Command handler: reset
 * 
 * Cold-reset the card and read the ATR
 */
void handle_reset(String *cmdline)
{
	doResetAndATR();
}


/**
 * Command handler: serial
 * 
 * Display VideoCrypt card serial number
 */
void handle_serial(String *cmdline)
{
	doSerialNumber();
}


/**
 * Command handler: scandebug [i]
 * 
 * Get/set scan debug state
 */
void handle_scan_debug(String *cmdline)
{
	if (cmdline->length() > 0) {
		gScanDebug = cmdline->toInt() != 0;
	}
	
	Serial.print("Scan debug is ");
	Serial.println(gScanDebug ? "on" : "off");
}


/**
 * Command handler: scancla <start> [<end>]
 * 
 * Scan instruction classes.
 */
void handle_scan_cla(String *cmdline)
{
	////////
	// CLA/INS SCAN

	uint8_t atr[32];
	uint8_t atrLen;
	uint8_t buf[256];
	uint8_t procByte;

	uint8_t startClass;
	uint8_t endClass;

	if (cmdline->length() == 0) {
		Serial.println("**ERROR: Need at least a starting classcode");
		return;
	} else {
		int ofs;
		String val = *cmdline;

		// Get starting classcode
		ofs = val.indexOf(' ');
		if (ofs == -1) {
			startClass = strtol(val.c_str(), NULL, 16);
			endClass = startClass;
		} else {
			startClass = strtol(val.substring(0, ofs).c_str(), NULL, 16);
			endClass = strtol(val.substring(ofs+1).c_str(), NULL, 16);
		}
	}

	doResetAndATR();

	Serial.print("Scanning from classcode 0x");
	Serial.print(startClass, HEX);
	Serial.print(" to 0x");
	Serial.print(endClass, HEX);
	Serial.println(" inclusive.\n");

	String reason = "";

	// CLA 0xFF is reserved for PTS
	// Sky 07 cards don't seem to check the classcode. ???
	for (int cla = startClass; cla <= endClass; cla++)
	{
		for (int ins=0; ins<=0xFF; ins += 2) {
			// INS is only valid if LSBit = 0 and MSN is not 6 or 9
			if ( ((ins >> 4) == 6) || ((ins >> 4) == 9) || (ins & 1)) {
				//Serial.print("skipping invalid INS ");
				//Serial.println(ins, HEX);
				continue;
			}

			if (gScanDebug) {
				Serial.println();
			}

			uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, 0xff, buf, APDU_RECV, &procByte, gScanDebug);

			if (sw1sw2 >= 0xFFF0) {
				reason = " (comms err, rebooting card) ";
				// reset and ATR, comms error
				scReset(true);
				delay(10);
				scReset(false);
				atrLen = cardGetAtr(atr);
				delay(10);
			} else if (sw1sw2 == 0x6D00) {
				//reason = " (bad ins)";
			} else {
				reason = " FOUND";

				switch (sw1sw2) {
					case 0x6700: reason += " (BAD_LE)    "; break;
					case 0x6B00: reason += " (BAD P1/P2) "; break;
					case 0x9000: reason += " (SUCCESS)   "; break;
				}
			}

			if (reason.length() > 0) {
				Serial.print("CLA/INS ");
				Serial.print(cla, HEX);
				Serial.print('/');
				Serial.print(ins, HEX);
				Serial.print(" -- sw1sw2=");
				Serial.print(sw1sw2, HEX);
				Serial.print(reason);
				Serial.print("Proc=");
				printHex(procByte);
				Serial.println();

				reason = "";
			}

			// Sky card requires 10ms between commands
			delay(10);
		}
	}
	Serial.println("\nAll done.");
}


// command definition entry
typedef struct {
	const char *cmd;
	void (*callback)(String *s);
} CMD;

// command definitions
const CMD COMMANDS[] = {
	{ "cmd84",		handle_cmd84 },
	{ "off",		handle_off },			// Card power off
	{ "on",			handle_reset },			// Power on, Reset and ATR
	{ "reset",		handle_reset },			// Power on, Reset and ATR
	{ "scandebug",	handle_scan_debug },	// scandebug <n> --> debug on/off
	{ "scancla",	handle_scan_cla },		// Scan for classcodes
	{ "serial",		handle_serial },		// VC: Read serial number and card issue
	{ "osd",		handle_osd },			// VC: Read OSD
	{ "", NULL }
};

void menu(void)
{
	const CMD *p = COMMANDS;

	Serial.println("\nCommand list:");
	while (p->callback != NULL) {
		Serial.print("   [");
		Serial.print(p->cmd);
		Serial.println("]");
		p++;
	}

	Serial.print("\n> ");

	while (Serial.available() == 0) {} 
	String s = Serial.readStringUntil('\n');

	// trim whitespace
	s.trim();

	// echo the command line
	Serial.println(s);
	Serial.println();


	// parse the command string
	int pos;
	String cmp;

	// find the first argument separator
	pos = s.indexOf(' ');
	if (pos != -1) {
		cmp = s.substring(0, pos);
		s.remove(0, pos+1);
	} else {
		cmp = s;
		s = "";
	}

	// try to find the command in the command table
	p = COMMANDS;
	while (p->callback != NULL) {
		if (cmp.equals(p->cmd)) {
			break;
		}
		p++;
	}

	// call the handler callback if the cmd was found
	if (p->callback != NULL) {
		p->callback(&s);
	} else {
		Serial.println("Bad command '" + cmp + "'");
	}
}





void setup() {
	// put your setup code here, to run once:

	// init serial port
	Serial.begin(57600);
	while (!Serial) { }

	// init glitcher
	glitcherInit();
	cardPower(0);


	Serial.println(">> GLITCHER " __DATE__ " " __TIME__ );

	// init card serial port
	cardInit();


#if 0




#if 0
	////////
	// Check some CLA/INS combinations we're curious about

	//// CMD 70

	uint8_t apduBuf[200];
	uint16_t sw1sw2;

	Serial.println("Read CSN...");

	triggerPulse();
	
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

#endif
	
}


void loop() {
	// put your main code here, to run repeatedly:

	menu();
	return;
	
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
