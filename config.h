#pragma once

enum
{
	CFG_OPT_DHCP = 0, // ptr to int (boolean)
	CFG_OPT_IP, // ptr to ipaddr4
	CFG_OPT_NM, // ptr to ipaddr4
	CFG_OPT_GW, // ptr to ipaddr4
	CFG_OPT_SYNCH_PRIV, // ptr to int (boolean)
	CFG_OPT_PRIV_CSR_AWARE, // ptr to int(boolean)
	CFG_OPT_GIF_TIMEOUT, // ptr to uint
	CFG_OPT_GIF_MSG_TIMEOUT, // ptr to uint
	CFG_OPT_UDPTTY, // ptr to int(boolean)
	CFG_OPT_NET_DBG_MSG, // ptr to int(boolean)
	CFG_OPT_FRAME_DUMP, // ptr to int(boolean)
	COUNT_CFG,
};

extern const char* CFG_NAMES[COUNT_CFG];

extern void* CFG_VALS[COUNT_CFG];

void LoadConfig(void);
