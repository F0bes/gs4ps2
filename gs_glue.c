#include "gs_glue.h"
#include "swizzle.h"
#include "config.h"

#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <graph.h>
#include <dma.h>
#include <draw.h>
#include <gs_gp.h>
#include <gs_privileged.h>

#include "common.h"

u8* gs_glue_transfer_data = 0;

// Packet should point to a huge chunk of GIF packets
void gs_glue_transfer(u8* packet, u32 size)
{
	volatile u32* GIFCHCR = ((volatile u32*)0x1000A000);
	volatile u32* GIFMADR = ((volatile u32*)0x1000A010);
	volatile u32* GIFQWC = ((volatile u_int*)0x1000A020);

	*GIFMADR = (u32)packet;

	u32 transfer_cnt = size / 16;
	do
	{
		*GIFQWC = transfer_cnt < 0x7FFF ? transfer_cnt : 0x7FFF;
		transfer_cnt -= *GIFQWC;
		FlushCache(0);
		*GIFCHCR = 0x100;
		u32 timeout = *(u32*)CFG_VALS[CFG_OPT_GIF_TIMEOUT];
		dprint("timeout: %d\n", timeout);
		while (*GIFCHCR & 0x100)
		{
			if (timeout)
			{
				if (timeout-- == 0)
				{
					dprint("!!!!!!!!!!!WARNING!!!!!!!!!!!\n");
					dprint("GIF TRANSFER TIMEOUT, RESETING GIF AND TRYING TO RECOVER\n");

					*(u32*)0x10003000 = 1; // Reset the GIF

					// Wait a bit so we can see the message above
					timeout = *(u32*)CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT];
					while (--timeout)
						asm volatile("nop\n");
					break;
				}
			}
		}

	} while (transfer_cnt > 0);
}

void gs_glue_vsync()
{
	graph_wait_vsync();
}

// FFX logo does a FIFO read
void gs_glue_read_fifo(u32 size)
{
	volatile u32* VIF1CHCR = (u32*)0x10009000;
	volatile u32* VIF1MADR = (u32*)0x10009010;
	volatile u32* VIF1QWC = (u32*)0x10009020;
	volatile u32* VIF1FIFO = (u32*)0x10005000;
	volatile u32* VIF1_STAT = (u32*)0x10003C00;

	*GS_REG_BUSDIR = 1;
	*VIF1_STAT = (1 << 23); // Set the VIF FIFO direction to VIF1 -> Main Memory
	u8* unused = (u8*)aligned_alloc(64, size); // TODO: Return this data?
	u32 bytes2read = size;

	while (bytes2read >= 16)
	{
		*VIF1MADR = (u32)unused;
		*VIF1QWC = (((0xFFFF) < (bytes2read / 16)) ? (0xFFFF) : (bytes2read / 16));
		bytes2read -= *VIF1QWC * 16;

		FlushCache(0);
		*VIF1CHCR = 0x100;
		while (*VIF1CHCR & 0x100)
			;
	}

	// Because we truncate the QWC, finish of the rest of the transfer
	// by reading the FIFO directly. Apparently this is how retail games do it.
	while ((*VIF1_STAT >> 24))
	{
		u32 blah = *VIF1FIFO;
		blah += 1; // need to use blah to avoid unused error
		nopdelay();
	}

	*GS_REG_BUSDIR = 0; // Reset BUSDIR
	*VIF1_STAT = 0; // Reset FIFO direction

	// Create a data packet that sets TRXDIR to 3, effectively cancelling whatever
	// transfer may be going on.
	qword_t* packet = aligned_alloc(64, sizeof(qword_t) * 2);
	qword_t* q = packet;
	PACK_GIFTAG(q, GIF_SET_TAG(1, 1, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	q++;
	q->dw[0] = GS_SET_TRXDIR(3);
	q->dw[1] = GS_REG_TRXDIR;
	q++;
	dma_channel_send_normal(DMA_CHANNEL_GIF, packet, q - packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);

	free(unused);
	free(packet);
}

void gs_glue_registers(gs_registers_packet* packet)
{
	// SYNC registers are skipped as I get issues with them.
	// Noteable a black and white image, with a lot of banding.

	*GS_REG_BGCOLOR = packet->BGCOLOR;
	dprint("BGCOLOR: %08X\n", packet->BGCOLOR);
	*GS_REG_EXTWRITE = packet->EXTWRITE;
	dprint("EXTWRITE: %08X\n", packet->EXTWRITE);
	*GS_REG_EXTDATA = packet->EXTDATA;
	dprint("EXTDATA: %08X\n", packet->EXTDATA);
	*GS_REG_EXTBUF = packet->EXTBUF;
	dprint("EXTBUF: %08X\n", packet->EXTBUF);
	*GS_REG_DISPFB1 = packet->DISP[0].DISPFB;
	dprint("DISPFB1: %08X\n", packet->DISP[0].DISPFB);
	*GS_REG_DISPLAY1 = packet->DISP[0].DISPLAY;
	dprint("DISPLAY1: %08X\n", packet->DISP[0].DISPLAY);
	*GS_REG_DISPFB2 = packet->DISP[1].DISPFB;
	dprint("DISPFB2: %08X\n", packet->DISP[1].DISPFB);
	*GS_REG_DISPLAY2 = packet->DISP[1].DISPLAY;
	dprint("DISPLAY2: %08X\n", packet->DISP[1].DISPLAY);

	if (*(u32*)CFG_VALS[CFG_OPT_SYNCH_PRIV])
	{
		*GS_REG_SYNCHV = packet->SYNCV;
		dprint("SYNCHV: %08X\n", packet->SYNCV);
		*GS_REG_SYNCH2 = packet->SYNCH2;
		dprint("SYNCH2: %08X\n", packet->SYNCH2);
		*GS_REG_SYNCH1 = packet->SYNCH1;
		dprint("SYNCH1: %08X\n", packet->SYNCH1);
		*GS_REG_SRFSH = packet->SRFSH;
		dprint("SRFSH: %08X\n", packet->SRFSH);
	}
	else
	{
		dprint("Skipping SYNCHV SYNCH2, SYNCH1, and SRFSH as per config.\n");
	}
	*GS_REG_SMODE2 = packet->SMODE2;
	dprint("SMODE2: %08X\n", packet->SMODE2);
	*GS_REG_PMODE = packet->PMODE;
	dprint("PMODE: %08X\n", packet->PMODE);
	*GS_REG_SMODE1 = packet->SMODE1;

	return;
}

#define SET_GS_REG(reg) \
	q->dw[0] = *(u64*)data_ptr; \
	q->dw[1] = reg; \
	q++; \
	data_ptr += sizeof(u64);

void gs_glue_freeze(u8* data_ptr, u32 version)
{
	dprint("frozen state version %d\n", version);
	qword_t* reg_packet = aligned_alloc(64, sizeof(qword_t) * 200);
	dprint("glue freeze data: %p\n", data_ptr);
	dprint("glue freeze packet: %p\n", reg_packet);
	qword_t* q = reg_packet;

	PACK_GIFTAG(q, GIF_SET_TAG(3, 0, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 13),
		GIF_REG_AD | (GIF_REG_AD << 4) | (GIF_REG_AD << 8) | (GIF_REG_AD << 12) | (GIF_REG_AD << 16) |
			(GIF_REG_AD << 20) | (GIF_REG_AD << 24) | ((u64)GIF_REG_AD << 28) | ((u64)GIF_REG_AD << 32) |
			((u64)GIF_REG_AD << 36) | ((u64)GIF_REG_AD << 40) | ((u64)GIF_REG_AD << 44) | ((u64)GIF_REG_AD << 48));

	q++;
	dprint("prim\n");
	SET_GS_REG(GS_REG_PRIM);

	if (version <= 6)
		data_ptr += sizeof(u64); // PRMODE

	dprint("PRMODECONT\n");
	SET_GS_REG(GS_REG_PRMODECONT);
	dprint("TEXCLUT\n");
	SET_GS_REG(GS_REG_TEXCLUT);
	dprint("GS_REG_SCANMSK\n");
	SET_GS_REG(GS_REG_SCANMSK);
	dprint("GS_REG_TEXA\n");
	SET_GS_REG(GS_REG_TEXA);
	dprint("GS_REG_FOGCOL\n");
	SET_GS_REG(GS_REG_FOGCOL);
	dprint("GS_REG_DIMX\n");
	SET_GS_REG(GS_REG_DIMX);
	dprint("GS_REG_DTHE\n");
	SET_GS_REG(GS_REG_DTHE);
	dprint("GS_REG_COLCLAMP\n");
	SET_GS_REG(GS_REG_COLCLAMP);
	dprint("GS_REG_PABE\n");
	SET_GS_REG(GS_REG_PABE);
	dprint("GS_REG_BITBLTBUF\n");
	SET_GS_REG(GS_REG_BITBLTBUF);
	dprint("GS_REG_TRXDIR\n");
	SET_GS_REG(GS_REG_TRXDIR);
	dprint("GS_REG_TRXPOS\n");
	SET_GS_REG(GS_REG_TRXPOS);
	dprint("GS_REG_TRXREG\n");
	SET_GS_REG(GS_REG_TRXREG);
	dprint("GS_REG_TRXREG\n");
	SET_GS_REG(GS_REG_TRXREG);

	dprint("CTXT 1 regs\n");
	SET_GS_REG(GS_REG_XYOFFSET_1);
	*(u64*)data_ptr |= 1; // Reload clut
	SET_GS_REG(GS_REG_TEX0_1);
	SET_GS_REG(GS_REG_TEX1_1);
	if (version <= 6)
		data_ptr += sizeof(u64); // TEX2
	SET_GS_REG(GS_REG_CLAMP_1);
	SET_GS_REG(GS_REG_MIPTBP1_1);
	SET_GS_REG(GS_REG_MIPTBP2_1);
	SET_GS_REG(GS_REG_SCISSOR_1);
	SET_GS_REG(GS_REG_ALPHA_1);
	SET_GS_REG(GS_REG_TEST_1);
	SET_GS_REG(GS_REG_FBA_1);
	SET_GS_REG(GS_REG_FRAME_1);
	SET_GS_REG(GS_REG_ZBUF_1);

	if (version <= 4)
		data_ptr += sizeof(u32) * 7; // skip ???

	dprint("CTXT 2 regs\n");
	SET_GS_REG(GS_REG_XYOFFSET_2);
	*(u64*)data_ptr |= 1; // Reload clut
	SET_GS_REG(GS_REG_TEX0_2);
	SET_GS_REG(GS_REG_TEX1_2);
	if (version <= 6)
		data_ptr += sizeof(u64); // TEX2
	SET_GS_REG(GS_REG_CLAMP_2);
	SET_GS_REG(GS_REG_MIPTBP1_2);
	SET_GS_REG(GS_REG_MIPTBP2_2);
	SET_GS_REG(GS_REG_SCISSOR_2);
	SET_GS_REG(GS_REG_ALPHA_2);
	SET_GS_REG(GS_REG_TEST_2);
	SET_GS_REG(GS_REG_FBA_2);
	SET_GS_REG(GS_REG_FRAME_2);
	SET_GS_REG(GS_REG_ZBUF_2);
	if (version <= 4)
		data_ptr += sizeof(u32) * 7; // skip ???

	dprint("finished regs\n");
	PACK_GIFTAG(q, GIF_SET_TAG(5, 1, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 1),
		GIF_REG_AD);
	q++;
	SET_GS_REG(GS_REG_RGBAQ);
	SET_GS_REG(GS_REG_ST);
	// UV
	q->dw[0] = *(u32*)data_ptr;
	q->dw[1] = GS_REG_UV;
	q++;
	data_ptr += sizeof(u32);
	// FOG
	q->sw[0] = *(u32*)data_ptr;
	q->dw[1] = GS_REG_FOG;
	q++;
	data_ptr += sizeof(u32);
	SET_GS_REG(GS_REG_XYZ2);
	qword_t* reg_packet_end = q; // Kick the register packet after the vram upload

	data_ptr += sizeof(u64); // obsolete apparently

	// Unsure how to use these
	data_ptr += sizeof(u32); // m_tr.x
	data_ptr += sizeof(u32); // m_tr.y

	qword_t* vram_packet = aligned_alloc(64, sizeof(qword_t) * 0x50005);

	u8* swizzle_vram = aligned_alloc(64, 0x400000);
	// The current data is 'RAW' vram data, so deswizzle it, so when we upload it
	// the GS with swizzle it back to it's 'RAW' format.
	deswizzleImage(swizzle_vram, data_ptr, 1024 / 64, 1024 / 32);

	q = vram_packet;
	// Set up our registers for the transfer
	PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	q++;
	q->dw[0] = GS_SET_BITBLTBUF(0, 0, 0, 0, 16, 0); // Page 0, PSMCT32, TBW 1024
	q->dw[1] = GS_REG_BITBLTBUF;
	q++;
	q->dw[0] = GS_SET_TRXPOS(0, 0, 0, 0, 0);
	q->dw[1] = GS_REG_TRXPOS;
	q++;
	q->dw[0] = GS_SET_TRXREG(1024, 1024);
	q->dw[1] = GS_REG_TRXREG;
	q++;
	q->dw[0] = GS_SET_TRXDIR(0);
	q->dw[1] = GS_REG_TRXDIR;
	q++;
	dma_channel_send_normal(DMA_CHANNEL_GIF, vram_packet, q - vram_packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);

	q = vram_packet;
	qword_t* vram_ptr = (qword_t*)swizzle_vram;
	for (int i = 0; i < 16; i++)
	{
		// Idea, low priority -- this is done once:
		// Upload the GIFTAG
		// Send it down the DMA
		// Point the DMA at the vram ptr
		// Upload that QWC
		// Repeat
		q = vram_packet;
		PACK_GIFTAG(q, GIF_SET_TAG(0x4000, i == 15 ? 1 : 0, 0, 0, 2, 0), 0);
		q++;
		for (int j = 0; j < 0x4000; j++)
		{
			*q = *(qword_t*)vram_ptr;
			vram_ptr++;
			q++;
		}

		dma_channel_send_normal(DMA_CHANNEL_GIF, vram_packet, q - vram_packet, 0, 0);
		dma_channel_wait(DMA_CHANNEL_GIF, 0);
	}

	// Kick the register packet
	dma_channel_send_normal(DMA_CHANNEL_GIF, reg_packet, reg_packet_end - reg_packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	free(reg_packet);
	free(vram_packet);
	free(swizzle_vram);
}
