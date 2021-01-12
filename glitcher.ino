/***
   ISO7816 development platform and Glitcher
   Phil Pemberton <philpem@philpem.me.uk>

   Glitcher hardware based on the work of Chris Gerlinsky.
   Some ideas taken from Kpyro and Tucker.

   ATMega328P processor.

   Select Tools -> Board: "Chip Glitcher".
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

	cardPower(0);

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

	Serial.print("Convention: ");
	Serial.println(scGetInverseConvention() ? "Inverse" : "Direct");

	Serial.println();
	Serial.println();

	gCardPowerOn = true;
}


/**
 * Utility function: read and display card serial number
 */
void doSerialNumber(uint8_t *pIssue = NULL, unsigned long *pSerial = NULL)
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

	// pass issue and serial back to caller
	if (pSerial != NULL) {
		*pSerial = serial;
	}
	
	if (pIssue != NULL) {
		*pIssue = buf[0] & 0x0F;
	}
}


/************************************************************
 * COMMAND HANDLERS
 ************************************************************/


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

		Serial.println("CLI: [" + *cmdline + "]");

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
				atrLen = cardGetAtr(buf);
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
			delay(50);
		}
	}
	Serial.println("\nAll done.");
}


/**
 * Command handler: scanlen <cla> <ins>
 * 
 * Scan for valid instruction lengths
 */
void handle_scan_len(String *cmdline)
{
	////////
	// LENGTH SCAN

	uint8_t atrLen;
	uint8_t buf[256];
	uint8_t procByte;

	uint8_t cla;
	uint8_t ins;

	if (cmdline->length() == 0) {
		Serial.println("**ERROR: Syntax = scanlen <cla> <ins>");
		return;
	} else {
		int ofs;
		String val = *cmdline;

		// Get class
		ofs = val.indexOf(' ');
		if (ofs == -1) {
			Serial.println("**ERROR: Syntax = scanlen <cla> <ins>");
			return;
		} else {
			cla = strtol(val.substring(0, ofs).c_str(), NULL, 16);
			ins = strtol(val.substring(ofs+1).c_str(), NULL, 16);
		}
	}

	doResetAndATR();

	Serial.print("Scanning valid lengths for CLA 0x");
	printHex(cla);
	Serial.print(" INS 0x");
	printHex(ins);
	Serial.println(".");

	String reason = "";

	for (int len = 0xFF; len >= 0; len--) {
		if (gScanDebug) {
			Serial.println();
		}

		procByte = 0xFF;

		uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, len, buf, APDU_RECV, &procByte, gScanDebug);
		
		if (sw1sw2 >= 0xFFF0) {
			reason = " (comms err, rebooting card) ";
			// reset and ATR, comms error
			scReset(true);
			delay(10);
			scReset(false);
			atrLen = cardGetAtr(buf);
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
			printHex(cla);
			Serial.print("/");
			printHex(ins);
			Serial.print(" LEN ");
			printHex(len);
			Serial.print(" -- sw1sw2=");
			printHex(sw1sw2 >> 8);
			printHex(sw1sw2 & 0xFF);
			Serial.print(reason);
			Serial.print("Proc=");
			printHex(procByte);
			if (procByte == ins) {
				Serial.print(" (ACK    )");
			} else if (procByte == (ins+1)) {
				Serial.print(" (ACK+VPP)");
			} else if (procByte == (~ins)) {
				Serial.print(" (one    )");
			} else if (procByte == (~(ins+1))) {
				Serial.print(" (one+VPP)");
			} else {
				Serial.print("          ");
			}
			if (sw1sw2 == 0x9000) {
				Serial.print("  Data=");
				printHexBuf(buf, len);
			}
			Serial.println();

			reason = "";
		}

		// Sky card requires 10ms between commands
		delay(50);
	}
	
	Serial.println("\nAll done.");
}


/**
 * Command handler: decoem
 * 
 * Based on DECOEM.C
 * VideoCrypt Decoder Emulator
 */
void handle_decoem(String *cmdline)
{
#if 0
	const bool debug = false;
	uint8_t msg_p7[] = {
	    0xf8, 0x3f, 0x22, 0x35, 0xad, 0x32, 0x0c, 0xb6,   /* a 07 message */
	    0xfb, 0x62, 0x10, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9,
	    0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9,
	    0xf9, 0xf9, 0xf9, 0x54, 0xd5, 0x00, 0x25, 0x86	
	};
	uint8_t msg_p9[] = {
		0xe8, 0x43, 0x66, 0x3e, 0xc6, 0x1a, 0x0c, 0x9f,   /* a 09 message */
	    0x8f, 0x32, 0x6d, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c,
	    0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c,
	    0x6c, 0x6c, 0x6c, 0x67, 0xe9, 0x44, 0xbc, 0x68
	};
	uint8_t answ[8];
	uint8_t msgprev[16];
	uint8_t cardIssue;

	if (!gCardPowerOn) {
		Serial.println("Card power is off.");
		return;
	}
	
	Serial.println("DecoEm -- Videocrypt decoder emulator\n\n");

	// read card serial number
	doSerialNumber(&cardIssue);

	// CMD 0x72 -- send message from previous card
	Serial.println("CMD72 (Message from Old Card) -->");
	memset(msgprev, '\0', sizeof(msgprev));
	cardSendApdu(0x53, 0x72, 0, 0, 16, msgprev, APDU_SEND, NULL, debug);

	// Main processing loop...
	
	// CMD 0x74 -- Send Message
	if (cardIssue == 7) {
		Serial.println("CMD74 (Issue 7) -->");
		cardSendApdu(0x53, 0x74, 0, 0, 32, msg_p7, APDU_SEND, NULL, debug);
	} else if (cardIssue == 9) {
		Serial.println("CMD74 (Issue 9) -->");
		cardSendApdu(0x53, 0x74, 0, 0, 32, msg_p9, APDU_SEND, NULL, debug);
	} else {
		Serial.println("Sorry, I don't have a CMD74 for this card.");
	}

	// CMD 0x76 AUTHORIZE -- not sent if Authorize not pressed, doesn't really do anything

	// CMD 0x78 -- VIDEOCRYPT READ SEED
	Serial.println("CMD78 READ SEED -->");
	cardSendApdu(0x53, 0x78, 0, 0, 8, answ, APDU_RECV, NULL, debug);
	printHexBuf(answ, 8);
	Serial.println();

	// CMD 0x7A -- READ OSD
	handle_osd(NULL);

	// CMD 0x7C -- READ MESSAGE FOR NEXT CARD
	Serial.println("CMD7C READ MESSAGE FOR NEXT CARD -->");
	cardSendApdu(0x53, 0x7c, 0, 0, 16, msgprev, APDU_RECV, NULL, debug);
	printHexBuf(msgprev, 16);
	Serial.println();
#endif
}


/************************************************************
 * MAIN MENU
 ************************************************************/

// command definition entry
typedef struct {
	const char *cmd;
	void (*callback)(String *s);
} CMD;

// command definitions
const CMD COMMANDS[] = {
	{ "off",		handle_off },			// Card power off
	{ "on",			handle_reset },			// Power on, Reset and ATR
	{ "reset",		handle_reset },			// Power on, Reset and ATR
	
	{ "scandebug",	handle_scan_debug },	// scandebug <n> --> debug on/off
	{ "scancla",	handle_scan_cla },		// Scan for classcodes
	{ "scanlen",	handle_scan_len },		// Scan valid data lengths for command
	
	{ "serial",		handle_serial },		// VC: Read serial number and card issue
	{ "osd",		handle_osd },			// VC: Read OSD
	{ "decoem",		handle_decoem },		// VC: Decoder emulation
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



/************************************************************
 * SETUP ENTRY POINT
 ************************************************************/

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
}


/************************************************************
 * MAIN LOOP
 ************************************************************/

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
