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
		"transfer-timeout",
		"transfer-msg-timeout",
};

void* CFG_VALS[COUNT_CFG];

const char* CFG_DEFAULT =
	"# Configuration generated by gs4ps2.\n"

	"\n# Networking settings.\n"
	"# Use DHCP\n"
	"dhcp=1\n"
	"# Static addresses to use when DHCP is disabled\n"
	"ip=192.168.1.10\n"
	"nm=255.255.255.0\n"
	"gw=192.168.1.1\n"

	"\n# Interpretation settings.\n"
	"# Optionally set SYNCHV, SYNCH1, SYNCH2, SRFSH priviledged registers\n"
	"# Some dumps can set them to values incompatible with your television\n"
	"priv-synch=0\n"
	"# GIF transfer timeout in (0 to disable, decimal)\n"
	"transfer-timeout=300\n"
	"# Transfer timeout message timeout\n"
	"transfer-msg-timeout=1000\n";

int cfg_parse_s32(u32 cfg_index, const char* str)
{
	dprint("Parsing int: %s, index %d\n", str, cfg_index);
	if (CFG_VALS[cfg_index] == NULL)
		CFG_VALS[cfg_index] = malloc(sizeof(s32));

	if (sscanf(str, "%d", (s32*)CFG_VALS[cfg_index]) != 1)
		return -1;
	return 0;
}

int cfg_parse_u32(u32 cfg_index, const char* str)
{
	dprint("Parsing int: %s, index %d\n", str, cfg_index);
	if (CFG_VALS[cfg_index] == NULL)
		CFG_VALS[cfg_index] = malloc(sizeof(u32));

	if (sscanf(str, "%u", (u32*)CFG_VALS[cfg_index]) != 1)
		return -1;
	return 0;
}

int cfg_parse_ip4_addr(u32 cfg_index, const char* str)
{
	dprint("Parsing ip: %s\n", str);
	if (CFG_VALS[cfg_index] == NULL)
		CFG_VALS[cfg_index] = malloc(sizeof(struct ip4_addr));
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
		cfg_parse_u32,
		cfg_parse_u32,
};

FILE* g_cfgFile;

void LoadDefaults(void)
{
	// Wish I had templating, why did I choose C. xP
	CFG_VALS[CFG_OPT_DHCP] = malloc(sizeof(int));
	*(int*)CFG_VALS[CFG_OPT_DHCP] = 1;
	CFG_VALS[CFG_OPT_IP] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_IP]);
	CFG_VALS[CFG_OPT_NM] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_NM]);
	CFG_VALS[CFG_OPT_GW] = malloc(sizeof(struct ip4_addr));
	ip4_addr_set_zero((struct ip4_addr*)CFG_VALS[CFG_OPT_GW]);

	CFG_VALS[CFG_OPT_SYNCH_PRIV] = malloc(sizeof(int));
	*(int*)CFG_VALS[CFG_OPT_SYNCH_PRIV] = 0;
	CFG_VALS[CFG_OPT_GIF_TIMEOUT] = malloc(sizeof(int));
	*(int*)CFG_VALS[CFG_OPT_GIF_TIMEOUT] = 300;
	CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT] = malloc(sizeof(int));
	*(int*)CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT] = 1000;

	g_cfgFile = fopen("host:config.txt", "w");
	if (g_cfgFile == NULL)
	{
		dprint("Failed to generate a config.txt, defaults have been loaded still.\n");
		return;
	}

	fwrite(CFG_DEFAULT, strlen(CFG_DEFAULT), 1, g_cfgFile);

	fclose(g_cfgFile);
	dprint("Generated a config.txt, defaults have been loaded.\n");
}


void LoadConfig(void)
{
	memset(CFG_VALS, 0, sizeof(CFG_VALS));
	g_cfgFile = fopen("host:config.txt", "r");

	if (g_cfgFile == NULL)
	{
		dprint("Failed to open config file, using defaults\n");
		LoadDefaults();
		return;
	}

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
				if (CFG_VALS[i] != NULL)
					dprint("Warning, duplicate config entry for %s\n", line);
				if (CFG_PARSE[i](i, equals) != 0)
					dprint("Failed to parse %s\n", line);
				break;
			}
		}
	}
}
