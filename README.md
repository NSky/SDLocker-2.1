# SDLocker-2.1
A custom firmware for SDLocker 2 (Atmega328p) device

This project is an extension of the SDLocker 2 firmware that makes it possible
to unlock SD cards protected by an unknown (or forgotten) password.
Additionally, the UART library missing from the original sources was reconstructed.

Example of use:
The genuine multimedia system of many Mitsubishi vehicles contains navigation
maps on a SD card. It's possible to update those maps by inserting SD containing 
newer maps. But after that the SD card is locked (password protected)
and not accessible by any other SD-compliant device. The password is not known.
This firmware extends the original SDLocker 2 with a feature to reset the password.
BE AWARE THAT IN RESULT ALL CONTENTS OF SD IS ERASED DUE TO SECURITY REASONS

How to use:
- read about the original device by the link below.
- press UNLOCK for 5 seconds to ERASE SD and RESET THE PASSWORD

The original SDLocker 2 project:
http://www.seanet.com/~karllunt/sdlocker2.html

UART source code was taken from here:
https://www.appelsiini.net/2011/simple-usart-with-avr-libc
