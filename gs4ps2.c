#include "common.h"
#include "config.h"
#include "irx/irx.h"
#include "network.h"
#include "server.h"

#include <kernel.h>
#include <sio.h>

#include <sifrpc.h>
#include <sifcmd.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>

void loadIOPModules()
{
	SifInitRpc(0);
	while (!SifIopReset("", 0))
	{
	};
	while (!SifIopSync())
	{
	};
	SifInitRpc(0);
	sbv_patch_enable_lmb();

	int ret = SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, NULL);

	dprint("Loaded ps2dev9.irx: %d\n", ret);
	ret = SifExecModuleBuffer(netman_irx, size_netman_irx, 0, NULL, NULL);
	dprint("Loaded netman.irx: %d\n", ret);
	ret = SifExecModuleBuffer(ps2ip_irx, size_ps2ip_irx, 0, NULL, NULL);
	dprint("Loaded ps2ip.irx: %d\n", ret);
	ret = SifExecModuleBuffer(smap_irx, size_smap_irx, 0, NULL, NULL);
	dprint("Loaded smap.irx: %d\n", ret);
	return;
}

int main(void)
{
	(*(volatile u_int*)0x10003000) = 1;
	sio_puts("Hello, world!\n");

	LoadConfig();

	loadIOPModules();
	if (network_init() < 0)
		SleepThread();

	if (server_init() < 0)
		SleepThread();

	SleepThread();
}
