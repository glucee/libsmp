# libsmp

[![Build Status](http://jenkins.actronika.com:8080/buildStatus/icon?job=libsmp-build)](https://jenkins.actronika.com:8080/job/libsmp-build/)

libsmp stands for Simple Message Protocol library. It aims to provide a simple
protocol to exchange message between two peers over a serial lane.
Messages are identified by an ID, choosen by user application, and contains 0 or
more arguments which are typed.

Protocols are documented in the docs directory.

## Getting started

### Requirements

* Meson build system and ninja

### Compiling

```bash
$ meson build
$ cd build/
$ ninja
```

## Supported platform

* GNU/Linux (Posix systems should works as well)
* AVR (See notes)
* Windows

## Notes about AVR port

AVR port is quite special as we don't have any OS on this platform. The serial
device directly implements a driver for AVR UARTs modules. AVR port currently
supports the following chips:

* Atmega 328p
* Atmega 2560

As these system don't have much RAM, you should tweak all buffers size using
meson configure tool.

The serial device name have the following format: 'serialX' with X a number from
0 to the maximum UART device. For instance, the Atmega 328p has only one uart
device which is named 'serial0'.

The returned fd on this platform is not a file descriptor at all so it shouldn't
be use outside of smp functions.


## Notes about Arduino

It is possible to export this library for the Arduino IDE, but the settings will
be global for all your Arduino sketches. You will set up these settings during
the export. To perform the export, please run the python script
`export-arduino-lib.py` located in the `scripts` subfolder with python 3
(or higher). Then you can import the resulting zip file into your Arduino IDE.
