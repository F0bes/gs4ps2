#include "common.h"
#include <sio.h>
#include <stdarg.h>

#ifdef UDP_OUT
#include <sys/time.h>
#include <ps2ip.h>

#include <string.h>
#include <stdio.h>
#endif

void sio_printf(const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 256, fmt, ap);
	va_end(ap);
	sio_puts(buf);
}

#ifdef UDP_OUT

u32 common_client_dest = htonl(INADDR_BROADCAST);

static u32 udp_inited = 0;
static s32 tty_socket = 0;
void common_udpmsg_init()
{
	// Create udp socket
	if (udp_inited)
		return;

	udp_inited = 1;

	tty_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (tty_socket < 0)
	{
		sio_printf("Error creating socket: %d\n", tty_socket);
		return;
	}
	sio_printf("Socket created: %d\n", tty_socket);
}

void udp_broadcast(const char* msg)
{
	if (!udp_inited)
		return;

	struct sockaddr_in dstaddr;

	dstaddr.sin_family = AF_INET;
	dstaddr.sin_addr.s_addr = common_client_dest;
	dstaddr.sin_port = htons(18194);

	sendto(tty_socket, msg, strlen(msg), 0, (struct sockaddr*)&dstaddr,
		sizeof(dstaddr));
}
#endif


void dprint(const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 256, fmt, ap);
	va_end(ap);
	sio_puts(buf);
#ifdef UDP_OUT
	if(udp_inited)
		udp_broadcast(buf);
#endif
}
