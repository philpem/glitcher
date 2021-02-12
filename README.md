# Cardblaster

Cardblaster is the firmware for a programmable smartcard communications and security analysis platform. It is intended to make it easier to investigate the security of smartcard and microcontroller firmware. Some of the things you could do include:

  * Write tools to read and write data on the card (duh).
  * Monitor the timing of commands under varying conditions to see if they leak information (like whether a password is correct).
  * Trigger an oscilloscope during certain command phases to see if data is leaked in the power consumption of the MCU.
  * Abuse bugs in the card firmware to make it do fun and interesting things (and perhaps leak information).
  * Apply power and clock glitches to cause instructions to be mis-executed or skipped entirely.

Needless to say, this is quite a useful tool for security analysis.


## Hardware

The hardware is a Phoenix interface with a Vcc and clock glitcher, mated to an ATMEGA328P. It's essentially an Arduino with a smartcard interface.

The RS232 port may be connected directly to the Phoenix interface, or to the ATMEGA's serial port to communicate with the Arduino code.


## Future plans

RAM and code space limitations are an issue on the ATMEGA328P, as well as the limited clock options limiting card options.
