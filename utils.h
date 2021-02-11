#ifndef UTILS_H
#define UTILS_H

void printHex(const uint8_t val);
void printHexBuf(const uint8_t *buf, int len);

// From https://www.freertos.org/FreeRTOS_Support_Forum_Archive/February_2012/freertos_Tick_count_overflow_5005076.html

/*  Determine if time a is "after" time b.
 *  Times a and b are unsigned, but performing the comparison
 *  using signed arithmetic automatically handles wrapping.
 *  The disambiguation window is half the maximum value. */
#define timeAfter(a,b)    (((int32_t)(a) - (int32_t)(b)) > 0)

#endif // UTILS_H
