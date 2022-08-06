#pragma once

#include <stdio.h>
#include <sio.h>


void sio_printf(const char *fmt, ...);

//#define dprintf(...) printf(__VA_ARGS__); sio_printf(__VA_ARGS__);
#define dprintf(...) sio_printf(__VA_ARGS__);
