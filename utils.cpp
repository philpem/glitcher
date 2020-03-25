#include <Arduino.h>
#include "utils.h"


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
