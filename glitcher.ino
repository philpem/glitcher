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
#include "videocrypt.h"
#include "cryptoworks.h"

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

// ATR buffer
uint8_t atr[32];
uint8_t atrLen = 0;

/**
 * Utility function: power on the card and display the ATR.
 * 
 * Used by the 'on' and 'reset' commands
 */
void doResetAndATR(bool silent=false)
{
	///////
	// POWER UP AND GET ATR

	cardPower(0);

	if (!silent) {
		Serial.println(F("Card powering up..."));
	}
	cardPower(1);

	// wait max of 12ms for ATR
	atrLen = cardGetAtr(atr);

	if (!silent) {
		Serial.print(F("ATR Len="));
		Serial.print(atrLen);
		Serial.println(F(" bytes"));
		
		Serial.print(F("ATR: "));
		printHexBuf(atr, atrLen);
		Serial.println();
	
		Serial.print(F("Convention: "));
		Serial.println(scGetInverseConvention() ? F("Inverse") : F("Direct"));
	
		Serial.println();
		Serial.println();
	}

	gCardPowerOn = true;
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
	Serial.print(F("Powering off card... "));
	cardPower(0);
	Serial.println(F("done."));

	gCardPowerOn = false;
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
 * Command handler: scandebug [i]
 * 
 * Get/set scan debug state
 */
void handle_scan_debug(String *cmdline)
{
	if (cmdline->length() > 0) {
		gScanDebug = cmdline->toInt() != 0;
	}
	
	Serial.print(F("Scan debug is "));
	Serial.println(gScanDebug ? F("on") : F("off"));
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

	uint8_t buf[256];
	uint8_t procByte;
	uint8_t startClass;
	uint8_t endClass;

	if (cmdline->length() == 0) {
		Serial.println(F("**ERROR: Need at least a starting classcode"));
		return;
	} else {
		int ofs;
		String val = *cmdline;

		Serial.print(F("CMD: ["));
		Serial.print(cmdline->c_str());
		Serial.println(']');

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

	Serial.print(F("Scanning from classcode 0x"));
	Serial.print(startClass, HEX);
	Serial.print(F(" to 0x"));
	Serial.print(endClass, HEX);
	Serial.println(F(" inclusive.\n"));

	String reason = "";

	// CLA 0xFF is reserved for PTS
	// Sky 07 cards don't seem to check the classcode. ???
	for (int cla = startClass; cla <= endClass; cla++)
	{
		for (int ins=0; ins<=0xFF; ins += 2) {
			// INS is only valid if LSBit = 0 and MSN is not 6 or 9
			if ( ((ins >> 4) == 6) || ((ins >> 4) == 9) || (ins & 1)) {
				//Serial.print(F("skipping invalid INS "));
				//Serial.println(ins, HEX);
				continue;
			}

			if (gScanDebug) {
				Serial.println();
			}

			uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, 0xff, buf, APDU_RECV, &procByte, gScanDebug);

			if (sw1sw2 >= 0xFFF0) {
				reason = " (comms err, rebooting card) ";
				doResetAndATR(true);
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
				Serial.print(F("CLA/INS "));
				Serial.print(cla, HEX);
				Serial.print('/');
				Serial.print(ins, HEX);
				Serial.print(F(" -- sw1sw2="));
				Serial.print(sw1sw2, HEX);
				Serial.print(reason);
				Serial.print(F("Proc="));
				printHex(procByte);
				Serial.println();

				reason = "";
			}

			// Sky card requires 10ms between commands
			delay(50);
		}
	}
	Serial.println(F("\nAll done."));
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

	uint8_t buf[256];
	uint8_t procByte;

	uint8_t cla;
	uint8_t ins;

	Serial.print(F("CMD: ["));
	Serial.print(cmdline->c_str());
	Serial.println(']');

	if (cmdline->length() == 0) {
		Serial.println(F("**ERROR E100: Syntax = scanlen <cla> <ins>"));
		return;
	} else {
		int ofs;

		// Get class
		ofs = cmdline->indexOf(' ');
		if (ofs == -1) {
			Serial.println(F("**ERROR E101: Syntax = scanlen <cla> <ins>"));
			return;
		} else {
			cla = strtol(cmdline->substring(0, ofs).c_str(), NULL, 16);
			ins = strtol(cmdline->substring(ofs+1).c_str(), NULL, 16);
		}
	}

	doResetAndATR();

	Serial.print(F("Scanning valid lengths for CLA 0x"));
	printHex(cla);
	Serial.print(F(" INS 0x"));
	printHex(ins);
	Serial.println(F("."));

	String reason = "";

	for (int len = 0xFF; len >= 0; len--) {
		if (gScanDebug) {
			Serial.println();
		}

		procByte = 0xFF;

		uint16_t sw1sw2 = cardSendApdu(cla, ins, 0, 0, len, buf, APDU_RECV, &procByte, gScanDebug);
		
		if (sw1sw2 >= 0xFFF0) {
			reason = " (comms err, rebooting card) ";
			doResetAndATR(true);
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
	
	Serial.println(F("\nAll done."));
}



/************************************************************
 * MAIN MENU
 ************************************************************/

// command definition entry
typedef struct {
	const char *cmd PROGMEM;
	const char *descr PROGMEM;
	void (*callback)(String *s);
} CMD;

// command definitions
const CMD COMMANDS[] = {
	{ "off",		"Card power off",					handle_off },			// Card power off
	{ "on",			"Card power on",					handle_reset },			// Power on, Reset and ATR
	{ "reset",		"Card power on (alias of 'on')",	handle_reset },			// Power on, Reset and ATR
	
	{ "scandebug",	"param 0/1: scan debugging off/on",	handle_scan_debug },	// scandebug <n> --> debug on/off
	{ "scancla",	"Scan classcodes",					handle_scan_cla },		// Scan for classcodes
	{ "scanlen",	"Scan instruction lengths",			handle_scan_len },		// Scan valid data lengths for command
	
	{ "vcserial",	"VideoCrypt: card serial number",	handle_vcserial },		// VC: Read serial number and card issue
	{ "vcosd",		"VideoCrypt: read OSD",				handle_vcosd },			// VC: Read OSD
	{ "vcdecoem",	"VideoCrypt: decoder emulation",	handle_vcdecoem },		// VC: Decoder emulation
	{ "vcsecret",	"VideoCrypt: secret command test",	handle_vcsecret },		// VC: Secret command test

	{ "cwinfo",		"CryptoWorks: card information",	handle_cwinfo },
	
	{ "", NULL }
};

void menu(void)
{
	const CMD *p = COMMANDS;

	Serial.println(F("\nCommand list:"));
	while (p->callback != NULL) {
		Serial.print(F("   "));

		// command name padded to 20 characters
		Serial.print(p->cmd);
		int pad = 20 - strlen(p->cmd);
		if (pad > 0) {
			for (int i=0; i<pad; i++) {
				Serial.print(' ');		
			}
		}

		// command description
		Serial.println(p->descr);

		p++;
	}

	Serial.print(F("\n> "));

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
		Serial.print(F("Bad command '"));
		Serial.print(cmp);
		Serial.println('\'');
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


	Serial.println(F(">> GLITCHER " __DATE__ " " __TIME__ ));

	// init card serial port
	cardInit();	
}


/************************************************************
 * MAIN LOOP
 ************************************************************/

void loop() {
	// put your main code here, to run repeatedly:

	menu();
}


#if 0
void glitchtest() {
	/*
	  Serial.println("blink\n");

	  digitalWrite(LED_PIN, HIGH);
	  delay(1000);
	  digitalWrite(LED_PIN, LOW);
	  delay(1000);
	*/

  
	//Serial.println(F("glitch"));

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
}

#endif
