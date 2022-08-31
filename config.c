#include "common.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ps2ip.h>

const char* CFG_NAMES[COUNT_CFG] =
	{
		"dhcp",
		"ip",
		"nm",
		"gw",
		"priv-synch",
		"priv-csr-aware",
		"transfer-timeout",
		"transfer-msg-timeout",
		"udptty",
		"net-dbg-msg",
		"dump-frames"};

void* CFG_VALS[COUNT_CFG];

const char* CFG_DEFAULT =
	R"(# Configuration generated by gs4ps2.

# Networking settings #

# Use DHCP
dhcp=0
# Static addresses to use when DHCP is disabled
ip=192.168.1.10
nm=255.255.255.0
gw=192.168.1.1

# GS settings.

# Optionally set SYNCHV, SYNCH1, SYNCH2, SRFSH priviledged registers
# Some dumps can set them to values incompatible with your television
priv-synch=1
# Match CSR:FIELD privileged register  with hardware CSR:FIELD
priv-csr-aware=1
# GIF transfer timeout in (0 to disable, decimal)
transfer-timeout=300
# Transfer timeout message timeout
transfer-msg-timeout=1000

# Communication settings.

# Toggles 'udptty', might be slower but you can use ps2client to see logging
udptty=1
# Toggles networking messages, great speedup when off
net-dbg-msg=0
# Toggles dumping frames back over the network
dump-frames=1
)";

int cfg_parse_s32(u32 cfg_index, const char* str)
{
	dprint("Parsing int: %s, index %d\n", str, cfg_index);

	if (sscanf(str, "%d", (s32*)CFG_VALS[cfg_index]) != 1)
		return -1;
	return 0;
}

int cfg_parse_u32(u32 cfg_index, const char* str)
{
	dprint("Parsing int: %s, index %d\n", str, cfg_index);

	if (sscanf(str, "%u", (u32*)CFG_VALS[cfg_index]) != 1)
		return -1;
	return 0;
}

int cfg_parse_ip4_addr(u32 cfg_index, const char* str)
{
	dprint("Parsing ip: %s\n", str);
	u32 addr1, addr2, addr3, addr4;
	if (sscanf(str, "%d.%d.%d.%d", &addr1, &addr2, &addr3, &addr4) != 4)
		return -1;

	IP4_ADDR((struct ip4_addr*)CFG_VALS[cfg_index], addr1, addr2, addr3, addr4);
	return 0;
}

int (*CFG_PARSE[COUNT_CFG])(u32 cfg_index, const char* str) =
	{
		cfg_parse_s32,
		cfg_parse_ip4_addr,
		cfg_parse_ip4_addr,
		cfg_parse_ip4_addr,
		cfg_parse_s32,
		cfg_parse_s32,
		cfg_parse_u32,
		cfg_parse_u32,
		cfg_parse_s32,
		cfg_parse_s32,
		cfg_parse_s32,
};

FILE* g_cfgFile;

// Returns 0 on default options loaded
// Returns 1 on configuration file
u32 LoadDefaults(void)
{
	// Wish I had templating, why did I choose C. xP
	CFG_VALS[CFG_OPT_DHCP] = malloc(sizeof(s32));
	*(s32*)CFG_VALS[CFG_OPT_DHCP] = 0;
	CFG_VALS[CFG_OPT_IP] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_IP]);
	CFG_VALS[CFG_OPT_NM] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_NM]);
	CFG_VALS[CFG_OPT_GW] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_GW]);

	CFG_VALS[CFG_OPT_SYNCH_PRIV] = malloc(sizeof(s32));
	*(s32*)CFG_VALS[CFG_OPT_SYNCH_PRIV] = 1;
	CFG_VALS[CFG_OPT_PRIV_CSR_AWARE] = malloc(sizeof(s32));
	*(s32*)CFG_VALS[CFG_OPT_PRIV_CSR_AWARE] = 1;
	CFG_VALS[CFG_OPT_GIF_TIMEOUT] = malloc(sizeof(u32));
	*(u32*)CFG_VALS[CFG_OPT_GIF_TIMEOUT] = 300;
	CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT] = malloc(sizeof(u32));
	*(u32*)CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT] = 1000;

	CFG_VALS[CFG_OPT_UDPTTY] = malloc(sizeof(u32));
	*(s32*)CFG_VALS[CFG_OPT_UDPTTY] = 1;
	CFG_VALS[CFG_OPT_NET_DBG_MSG] = malloc(sizeof(s32));
	*(s32*)CFG_VALS[CFG_OPT_NET_DBG_MSG] = 0;
	CFG_VALS[CFG_OPT_FRAME_DUMP] = malloc(sizeof(s32));
	*(s32*)CFG_VALS[CFG_OPT_FRAME_DUMP] = 1;

	g_cfgFile = fopen("host:config.txt", "r");
	if (g_cfgFile == NULL)
	{
		// The configuration file does not exist, create one
		dprint("No configuration file exists! Attempting to create one.\n");
		g_cfgFile = fopen("host:config.txt", "w");

		if (g_cfgFile != NULL)
		{
			// We have created a configuration file
			fwrite(CFG_DEFAULT, strlen(CFG_DEFAULT), 1, g_cfgFile);
			dprint("Created a configuration file with default values.\n");
			fclose(g_cfgFile); // Possibly fflush would work better here?
			g_cfgFile = fopen("host:config.txt", "r");

			return 1;
		}
		else
		{
			dprint("Failed to generate a config.txt\n");
		}
		return 0;
	}
	return 1;
}


void LoadConfig(void)
{
	memset(CFG_VALS, 0, sizeof(CFG_VALS));

	if (LoadDefaults())
	{
		char line[256];
		while (fgets(line, 256, g_cfgFile) != NULL)
		{
			char* equals = strchr(line, '=');
			if (equals == NULL)
				continue;
			*equals = '\0';
			equals++;
			for (int i = 0; i < COUNT_CFG; i++)
			{
				if (strcmp(line, CFG_NAMES[i]) == 0)
				{
					if (CFG_PARSE[i](i, equals) != 0)
						dprint("Failed to parse %s\n", line);
					break;
				}
			}
		}
		fclose(g_cfgFile);
	}
}
