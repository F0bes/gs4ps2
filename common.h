#pragma once

#include <stdio.h>
#include <sio.h>

#define UDP_OUT

#ifdef UDP_OUT
extern u32 common_client_dest;
void common_udpmsg_init();
#endif

void sio_printf(const char* fmt, ...);

void dprint(const char* fmt, ...);
