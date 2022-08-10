#include "common.h"
#include <sio.h>
#include <stdarg.h>

void sio_printf(const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 256, fmt, ap);
	va_end(ap);
	sio_puts(buf);
}
