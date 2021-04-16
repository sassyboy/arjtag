# Arjtag
Arjtag is an Arduino-based JTAG Programmer for CPLDs and FPGAs that supports SVF (Serial Vector Format, revision C) files. I made this JTAG interface when I realized my Altera USBBlaster cable is incompatible with my old ATF15xx CPLDs. So, I made a super slow, but effective JTAG interface with a simple Arduino Uno and a C++ program that reads an SVF (rev. C) file, sends the commands to Arduino, which is connected to the TCK, TDI, TDO, TMS pins of the CPLD and relays those commands to the CPLD.

## How to use Arjtag?

- Compile and upload the Arduino sketch in the `arduino` directory to an ArduinoUno
- `make` the svf-player. You'll need `make` and `g++` installed on your Linux machine.
- Connect the arduino pins (2, 3, 4, 5) to (TDI, TMS, TCK, TDO) of the CPLD and turn it on.
- **WARNING: Arduino's pin are 5v TTL. Use level shifters if your CPLD can't handle good old 5v logic**
- Run the svf-player in a terminal window. Usage: `svf-player your-svf-file arduino-usb-device-address`.
