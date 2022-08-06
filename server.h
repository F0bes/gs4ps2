#pragma once

#define SERVER_PORT_TCP 18196

#define SERVER_CMD_VER    0x00
#define SERVER_TRANSFER   0x01
#define SERVER_WAIT_VSYNC 0x02
#define SERVER_READ_FIFO  0x03
#define SERVER_SET_REG    0x04
#define SERVER_FREEZE     0x05
#define SERVER_SHUTDOWN   0xFF

#define SERVER_OK    0x80
#define SERVER_RETRY 0x81
s32 server_init(void);
