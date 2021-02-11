#include "hardware.h"
#include "smartcard.h"
#include "utils.h"

/**
 * Utility function: read and display card serial number
 */
static void doSerialNumber(uint8_t *pIssue = NULL, unsigned long *pSerial = NULL)
{
	uint8_t buf[8];
	uint16_t sw1sw2;

	// TODO: Add card power on state readback from main prog
/*
	if (!gCardPowerOn) {
		Serial.println(F("Card power is off."));
		return;
	}
*/
	// read serial number
	sw1sw2 = cardSendApdu(0x53, 0x70, 0, 0, 6, buf, APDU_RECV);
	Serial.print(F("Card issue:  "));
	Serial.println(buf[0] & 0x0F);
	
	unsigned long serial =
		((unsigned long)buf[1] << 24) |
		((unsigned long)buf[2] << 16) |
		((unsigned long)buf[3] << 8)  |
		((unsigned long)buf[4]);
	Serial.print(F("Card serial: "));
	Serial.print(serial);
	Serial.println(F("x"));		// TODO: Luhn checksum?
	Serial.println();

	// pass issue and serial back to caller
	if (pSerial != NULL) {
		*pSerial = serial;
	}
	
	if (pIssue != NULL) {
		*pIssue = buf[0] & 0x0F;
	}
}


/**
 * Command handler: osd
 *
 * Display current VideoCrypt OSD message
 */
void handle_vcosd(String *cmdline)
{
	uint8_t buf[25];
	uint16_t sw1sw2;

	// TODO: Add card power on state readback from main prog
/*
	if (!gCardPowerOn) {
		Serial.println(F("Card power is off."));
		return;
	}
*/

	// read OSD
	sw1sw2 = cardSendApdu(0x53, 0x7A, 0, 0, 25, buf, APDU_RECV);

	uint8_t prio = buf[0] >> 5;
	uint8_t len  = buf[0] & 0x1F;
	
	Serial.print(F("OSD Priority "));
	Serial.print(prio);
	Serial.print(F(", "));
	Serial.print(len);
	Serial.print(F(" characters"));

	// OSD messages must have a priority of >= 4 to show on clear unencrypted video.
	// Ref. 8052INFO.TXT [1.1.2].
	if (prio < 4) {
		Serial.println(F(" *HIDDEN*"));
	} else {
		Serial.println();
	}

	Serial.print("OSD: [");
	for (int i=1; i<=len; i++) {
		if (buf[i] == '\0') {
			Serial.print(F(" "));
		} else {
			Serial.print((char)buf[i]);
		}

		if (i == 12) {
			Serial.print(F("] ["));
		}
	}
	Serial.print(F("]\n"));
}

/**
 * Command handler: serial
 * 
 * Display VideoCrypt card serial number
 */
void handle_vcserial(String *cmdline)
{
	doSerialNumber();
}

/**
 * Command handler: vcdecoem
 * 
 * Based on DECOEM.C
 * VideoCrypt Decoder Emulator
 */

static const PROGMEM uint8_t MSG_P7[] = {
    0xf8, 0x3f, 0x22, 0x35, 0xad, 0x32, 0x0c, 0xb6,   /* a 07 message */
    0xfb, 0x62, 0x10, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9,
    0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9,
    0xf9, 0xf9, 0xf9, 0x54, 0xd5, 0x00, 0x25, 0x86	
};
static const PROGMEM uint8_t MSG_P9[] = {
	0xe8, 0x43, 0x66, 0x3e, 0xc6, 0x1a, 0x0c, 0x9f,   /* a 09 message */
    0x8f, 0x32, 0x6d, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c,
    0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c,
    0x6c, 0x6c, 0x6c, 0x67, 0xe9, 0x44, 0xbc, 0x68
};

 
void handle_vcdecoem(String *cmdline)
{
	const bool debug = false;
	bool ok = false;

    uint8_t msg[32];
    uint8_t answ[8];
    uint8_t msgprev[16];
	
	uint8_t cardIssue;

	// TODO: Add card power on state readback from main prog
/*
	if (!gCardPowerOn) {
		Serial.println("Card power is off.");
		return;
	}
*/
	
	Serial.println(F("DecoEm -- Videocrypt decoder emulator\n\n"));

	// read and print card serial number
	doSerialNumber(&cardIssue);

	// CMD 0x72 -- send message from previous card
	Serial.println(F("CMD72 (Message from Old Card) -->"));
	memset(msgprev, '\0', sizeof(msgprev));
	cardSendApdu(0x53, 0x72, 0, 0, 16, msgprev, APDU_SEND, NULL, debug);

	// Main processing loop...
	
	// CMD 0x74 -- Send Message
	switch (cardIssue) {
		case 7:
			memcpy_P(msg, MSG_P7, sizeof(msg));
			ok = true;
			break;

		case 9:
			memcpy_P(msg, MSG_P9, sizeof(msg));
			ok = true;
			break;

		default:
			Serial.println(F("Sorry, I don't have a CMD74 for this card issue."));
			ok = false;
			break;
	}

	if (ok) {
		Serial.print(F("CMD74 (Issue "));
		Serial.print(cardIssue);
		Serial.println(") -->");
		cardSendApdu(0x53, 0x74, 0, 0, 32, msg, APDU_SEND, NULL, debug);
	}

	// CMD 0x76 AUTHORIZE -- not sent if Authorize not pressed, doesn't really do anything

	// CMD 0x78 -- VIDEOCRYPT READ SEED
	Serial.println(F("CMD78 READ SEED -->"));
	cardSendApdu(0x53, 0x78, 0, 0, 8, answ, APDU_RECV, NULL, debug);
	printHexBuf(answ, 8);
	Serial.println();

	// CMD 0x7A -- READ OSD
	handle_vcosd(NULL);

	// CMD 0x7C -- READ MESSAGE FOR NEXT CARD
	Serial.println(F("CMD7C READ MESSAGE FOR NEXT CARD -->"));
	cardSendApdu(0x53, 0x7c, 0, 0, 16, msgprev, APDU_RECV, NULL, debug);
	printHexBuf(msgprev, 16);
	Serial.println();
}

/**
 * Command handler: vcsecret
 * 
 * VideoCrypt secret command test
 */
void handle_vcsecret(String *cmdline)
{
	uint8_t buf[256];
	const bool debug = true;
	const uint8_t SECRET_CLA = ~0x53;

	// TODO: Add card power on state readback from main prog
/*
	if (!gCardPowerOn) {
		Serial.println(F("Card power is off."));
		return;
	}
*/
	
	Serial.println(F("VideoCrypt secret commands test\n\n"));

	// read and print card serial number
	doSerialNumber();

	Serial.print(F("CMD F0 (Read Checksum), P2=0 --> "));
	cardSendApdu(SECRET_CLA, 0xF0, 0, 0, 2, buf, APDU_RECV, NULL, debug);
	printHexBuf(buf, 2);
	Serial.println();
	uint16_t sum_a = ((uint16_t)buf[0] << 8) | buf[1];

	Serial.print(F("CMD F0 (Read Checksum), P2=1 --> "));
	cardSendApdu(SECRET_CLA, 0xF0, 0, 1, 2, buf, APDU_RECV, NULL, debug);
	printHexBuf(buf, 2);
	Serial.println();
	uint16_t sum_b = ((uint16_t)buf[0] << 8) | buf[1];
	
	Serial.print(F("Delta checksum: "));
	Serial.println(sum_a - sum_b, HEX);	
}
