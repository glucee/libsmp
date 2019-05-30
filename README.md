# libsmp

[![Build Status](https://jenkins.actronika.com:8080/buildStatus/icon?job=libsmp/master)](https://jenkins.actronika.com:8080/job/libsmp/master)

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


## Static API

To support the embedded platform on which dynamic allocation is not suitable, we
provide a set of `static` APIs used to construct object using static storage and
a some macros to define the boilerplate code. The code below show usages of
these macros:

```c
#define SMP_ENABLE_STATIC_API
#include <libsmp.h>

static void on_new_message(SmpContext *ctx, SmpMessage *msg, void *userdata)
{
}

static void on_error(SmpContext *ctx, SmpError error, void *userdata)
{
}

static SmpEventCallbacks cbs = {
    .new_message_cb = on_new_message,
    .error_cb = on_error,
};

SMP_DEFINE_STATIC_CONTEXT(my_smp_context, 128, 128, 128, 16);

SMP_DEFINE_STATIC_MESSAGE(my_message, 16);

int main()
{
    SmpContext *ctx;
    SmpMessage *msg;
    int msgid = 42;

    ctx = my_smp_context_create(&cbs, NULL);
    if (ctx == NULL)
        return 1;

    msg = my_message_create(msgid);
    if (msg == NULL)
        return 1;

    smp_context_send_message(ctx, msg);
    return 0;
}
```

Also, when using only static API, you can safely set the following options to 0:
* `message-max-values`
* `serial-frame-max-size`


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
(or higher). Then you can import the resulting zip file into your Arduino IDE. This
repo has already provided a zip file with the default setting in scripts folder.

## Notes about Platformio

This repo has already provided a zip file with the default setting in scripts folder.
You could add it in your platformio.ini:
libsmp=https://github.com/glucee/libsmp/raw/master/scripts/libsmp.zip
