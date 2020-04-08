#ifndef SMARTCARD_H
#define SMARTCARD_H

#include "SoftwareSerialParity.h"

//extern SoftwareSerialParity scSerial;

void cardInit(void);

/**
 * Turn card power on/off
 */
void cardPower(const uint8_t on);

/**
 * Get the ATR from the card.
 * 
 * @param[out]	buf		Storage buffer. ATR will be stored here.
 * @return 
 */
int cardGetAtr(uint8_t *buf);

// FIXME figure out default timeout
int scReadByte(int timeout_ms = 50);

void scWriteByte(uint8_t b);


#define APDU_SEND true
#define APDU_RECV false

uint16_t cardSendApdu(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t len, uint8_t *buf, bool isSend, uint8_t *procByte=NULL, bool debug=false);

#endif
