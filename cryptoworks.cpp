#include "config.h"
#include "hardware.h"
#include "smartcard.h"
#include "utils.h"

#ifdef ENABLE_CRYPTOWORKS

static void debugtraffic(uint16_t sw1sw2, uint8_t *buf, size_t len)
{
	Serial.print(sw1sw2, HEX);
	Serial.print(':');
	if (len > 0) {
		printHexBuf(buf, len);
		Serial.println();
	} else {
		Serial.println(F("none"));
	}
}

/*
static char *chid_date(uchar *ptr, char *buf, int l)
{
	if (buf)
	{
		snprintf(buf, l, "%04d/%02d/%02d", 1990+(ptr[0]>>1), ((ptr[0]&1)<<3)|(ptr[1]>>5), ptr[1]&0x1f);
	}
	return(buf);
}
*/


/**
 * Cryptoworks CLA A4, INS A4: Select File
 */
static int select_file(const uint16_t fid)
{
	uint8_t buf[2];
	uint16_t sw1sw2;

	buf[0] = fid >> 8;
	buf[1] = fid & 0xFF;

	sw1sw2 = cardSendApdu(0xA4, 0xA4, 0, 0, 2, buf, APDU_SEND);

	// SW1SW2 = 0x9Fxx --> Success, xx bytes available
	if (sw1sw2 == 0x9F11) {
		return sw1sw2 & 0xFF;
	} else {
		// Error
		return -1;
	}
}

/**
 * Cryptoworks CLA A4, INS A2/B2: Read Record
 */
static int read_record(const uint8_t rec, uint8_t *buf, const uint8_t buf_len)
{
	uint16_t sw1sw2;
	uint8_t recnum = rec;
	uint8_t lc;

	// A4:A2 -- Seek / Select Record
	sw1sw2 = cardSendApdu(0xA4, 0xA2, 0, 0, 1, &recnum, APDU_SEND);
	if ((sw1sw2 & 0xFF00) != 0x9F00) {
		Serial.println(F("SELECT RECORD failed"));
		return -1;
	}

	// Yet Another Cryptoworks Hack (tm)
	// Sending the 
	delay(1);

	// SW1SW2 = 9Fxx where xx = length
	lc = (sw1sw2 & 0xFF);

	// Check there is sufficient buffer space to read
	if (buf_len >= lc) {
		// A4:B2 -- Read Record
		sw1sw2 = cardSendApdu(0xA4, 0xB2, 0, 0, lc, buf, APDU_RECV);
		if (sw1sw2 != 0x9000) {
			Serial.println(F("READ RECORD failed"));
			return -2;
		} else {
			return lc;
		}
	} else {
		return -3;
	}
}


/**
 * Command handler: cwinfo
 *
 * CryptoWorks: ATR the card and read its data
 */
void handle_cwinfo(String *cmdline)
{
	extern uint8_t atr[32];
	extern uint8_t atrLen;
	uint8_t buf[25];
	uint16_t sw1sw2;

	// cycle card power and read ATR
	cardPower(0);
	cardPower(1);
	atrLen = cardGetAtr(atr);

	// print ATR
	Serial.print(F("ATR: "));
	printHexBuf(atr, atrLen);
	Serial.println();

	// check for Cryptoworks ATR
	if ((atr[6]!=0xC4) || (atr[9]!=0x8F) || (atr[10]!=0xF1)) {
		Serial.println(F("Not a CryptoWorks card"));
		//return;
	}
	Serial.print(F("CryptoWorks card version:"));
	Serial.print(atr[7]);
	Serial.print(F(" PIN_tries:"));
	Serial.println(atr[8]);

	delay(100);

	// CryptoWorks hack: force 9600 Baud
	// the card advertises TA1=0x12 = 19200bd but expects 9600 after ATR
	cardBaud(9600);


	///
	int i;
	uint16_t mfid=0x3F20;

//	uchar issuerid=0;
//	char issuer[20]={0};
//	char *unknown="unknown", *pin=unknown, ptxt[CS_MAXPROV<<2]={0};
//	int nprov=0;

	// Get Master File ID
	sw1sw2 = cardSendApdu(0xA4, 0xC0, 0, 0, 17, buf, APDU_RECV);
	if (sw1sw2 != 0xFFFF) {
		debugtraffic(sw1sw2, buf, 17);
		if ((buf[0] == 0xDF) && (buf[1] >= 6)) {
			mfid = (buf[6] << 8) | buf[7];
		}
	} else {
		return;
	}
	Serial.print(F("MFID: 0x"));
	Serial.println(mfid, HEX);

	// select file
	i = select_file(mfid);
	if (i > 0) {
		Serial.print(F("SELECT MFID okay, "));
		Serial.print(i);
		Serial.println(F(" bytes available"));
	}

	// Set up to read first MF file block (Provider IDs)

#define MAXPROVIDERS 8

	uint8_t providerIds[MAXPROVIDERS];
	uint8_t nProviders = 0;

	// Get first block
	// READ SERIAL command, p1p2 = 0000 = first block
	sw1sw2 = cardSendApdu(0xA4, 0xB8, 0, 0, 0x0C, buf, APDU_RECV);
	while (sw1sw2 == 0x9000) {
		debugtraffic(sw1sw2, buf, 0x0C);

		if (buf[0] != 0xDF) {
			break;
		}
		
		if (((buf[4] & 0x1F) == 0x1F) && (nProviders < MAXPROVIDERS)) {
			providerIds[nProviders++] = buf[5];
		}

		// Get next block
		sw1sw2 = cardSendApdu(0xA4, 0xB8, 0xFF, 0xFF, 0x0C, buf, APDU_RECV);
	}

	Serial.print(F("Provider IDs on card (hex): "));
	if (i == 0) {
		Serial.println(F("NONE"));
		return;
	}
	
	for (uint8_t i=0; i<nProviders; i++) {
		if (i > 0) {
			Serial.print(',');
		}
		Serial.print(providerIds[i], HEX);
	}
	Serial.println();


	// Read CA ID
	Serial.print(F("SELECT FILE: "));
	i = select_file(0x2F01);
	Serial.println(i);
	
	Serial.print(F("READ CAID: "));
	if (i = read_record(0xD1, buf, 4) >= 0) {
		// decode caid
		debugtraffic(0xD104, buf, 4);
	}
	Serial.println(i);

	// Read serial number
	Serial.print(F("READ SERIAL: "));
	if (i = read_record(0x80, buf, sizeof(buf)) >= 0) {
		// decode serial
		debugtraffic(0x8007, buf, 7);
	}
	Serial.println(i);
}

#endif // ENABLE_CRYPTOWORKS
