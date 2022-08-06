#include "gs_glue.h"
#include "swizzle.h"

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

void gs_glue_transfer(gs_transfer_packet* packet)
{
	dma_channel_send_normal(DMA_CHANNEL_GIF, packet->data, packet->size / 16, 0, 0); // yolo lol
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	dprintf("Transfer completed\n");
	free(packet->data);
}

void gs_glue_vsync()
{
	graph_wait_vsync();
}

void gs_glue_read_fifo(u32 size)
{
	dprintf("Read fifo size 0x%X\n", size);
	volatile u32* VIF1CHCR = (u32*)0x10009000;
	volatile u32* VIF1MADR = (u32*)0x10009010;
	volatile u32* VIF1QWC = (u32*)0x10009020;
	volatile u32* VIF1FIFO = (u32*)0x10005000;
	volatile u32* VIF1_STAT = (u32*)0x10003C00;

	*GS_REG_BUSDIR = 1;
	*VIF1_STAT = (1 << 23); //VIF1 fifo direction
	u8* unused = (u8*)aligned_alloc(64, size);
	u32 tempsize = size;

	while (tempsize >= 16)
	{
		*VIF1MADR = (u32)unused;
		*VIF1QWC = (((0xFFFF) < (tempsize / 16)) ? (0xFFFF) : (tempsize / 16));
		tempsize -= *VIF1QWC * 16;
		FlushCache(0);
		*VIF1CHCR = 0x100;
		dma_channel_wait(DMA_CHANNEL_VIF1, 0);
	}

	while ((*VIF1_STAT >> 24))
	{
		dprintf("GIF_STAT = %08X VIF1_STAT = %08X, CSR = %08X\n", *(u32*)0x10003020, *(u32*)0x10003C00, *(u32*)0x12001000);
		dprintf("VIF1QWC = %08X \n", *VIF1QWC);
		u32 blah = *VIF1FIFO;
		blah += 1; // need to use blah to avoid unused error
		nopdelay();
	}
	*GS_REG_BUSDIR = 0;
	*VIF1_STAT = 0;

	qword_t *packet = aligned_alloc(64,sizeof(qword_t) * 5);
	qword_t* q = packet;

	PACK_GIFTAG(q, GIF_SET_TAG(1, 1, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	q++;
	// TEX0 (Point to the indexed fontmap and CLUT pallette)
	q->dw[0] = GS_SET_TRXDIR(3);
	q->dw[1] = GS_REG_TRXDIR;
	q++;
	dprintf("Resetting TRXDIR\n");
	dma_channel_send_normal(DMA_CHANNEL_GIF, packet, q - packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	dprintf("Reset TRXDIR\n");


	free(unused);
	free(packet);
}

void gs_glue_registers(gs_registers_packet* packet)
{

	*GS_REG_BGCOLOR = packet->BGCOLOR;
	dprintf("BGCOLOR: %08X\n", packet->BGCOLOR);
	*GS_REG_EXTWRITE = packet->EXTWRITE;
	dprintf("EXTWRITE: %08X\n", packet->EXTWRITE);
	*GS_REG_EXTDATA = packet->EXTDATA;
	dprintf("EXTDATA: %08X\n", packet->EXTDATA);
	*GS_REG_EXTBUF = packet->EXTBUF;
	dprintf("EXTBUF: %08X\n", packet->EXTBUF);
	*GS_REG_DISPFB1 = packet->DISP[0].DISPFB;
	dprintf("DISPFB1: %08X\n", packet->DISP[0].DISPFB);
	*GS_REG_DISPLAY1 = packet->DISP[0].DISPLAY;
	dprintf("DISPLAY1: %08X\n", packet->DISP[0].DISPLAY);
	*GS_REG_DISPFB2 = packet->DISP[1].DISPFB;
	dprintf("DISPFB2: %08X\n", packet->DISP[1].DISPFB);
	*GS_REG_DISPLAY2 = packet->DISP[1].DISPLAY;
	dprintf("DISPLAY2: %08X\n", packet->DISP[1].DISPLAY);
//	*GS_REG_SYNCHV = packet->SYNCV;
	dprintf("SYNCHV: %08X\n", packet->SYNCV);
//	*GS_REG_SYNCH2 = packet->SYNCH2;
	dprintf("SYNCH2: %08X\n", packet->SYNCH2);
	//*GS_REG_SYNCH1 = packet->SYNCH1;
	dprintf("SYNCH1: %08X\n", packet->SYNCH1);
	//*GS_REG_SRFSH = packet->SRFSH;
	dprintf("SRFSH: %08X\n", packet->SRFSH);
	*GS_REG_SMODE2 = packet->SMODE2;
	dprintf("SMODE2: %08X\n", packet->SMODE2);
	*GS_REG_PMODE = packet->PMODE;
	dprintf("PMODE: %08X\n", packet->PMODE);
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
	dprintf("frozen state version %d\n", version);
	qword_t* reg_packet = aligned_alloc(64, sizeof(qword_t) * 200);
	dprintf("glue freeze data: %p\n", data_ptr);
	dprintf("glue freeze packet: %p\n", reg_packet);
	qword_t* q = reg_packet;

	PACK_GIFTAG(q, GIF_SET_TAG(3, 0, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 13),
		GIF_REG_AD | (GIF_REG_AD << 4) | (GIF_REG_AD << 8) | (GIF_REG_AD << 12) | (GIF_REG_AD << 16) |
			(GIF_REG_AD << 20) | (GIF_REG_AD << 24) | ((u64)GIF_REG_AD << 28) | ((u64)GIF_REG_AD << 32) |
			((u64)GIF_REG_AD << 36) | ((u64)GIF_REG_AD << 40) | ((u64)GIF_REG_AD << 44) | ((u64)GIF_REG_AD << 48));

	q++;
	dprintf("prim\n");
	SET_GS_REG(GS_REG_PRIM);

	if(version <= 6)
		data_ptr += sizeof(u64); // PRMODE

	dprintf("PRMODECONT\n");
	SET_GS_REG(GS_REG_PRMODECONT);
	dprintf("TEXCLUT\n");
	SET_GS_REG(GS_REG_TEXCLUT);
	dprintf("GS_REG_SCANMSK\n");
	SET_GS_REG(GS_REG_SCANMSK);
	dprintf("GS_REG_TEXA\n");
	SET_GS_REG(GS_REG_TEXA);
	dprintf("GS_REG_FOGCOL\n");
	SET_GS_REG(GS_REG_FOGCOL);
	dprintf("GS_REG_DIMX\n");
	SET_GS_REG(GS_REG_DIMX);
	dprintf("GS_REG_DTHE\n");
	SET_GS_REG(GS_REG_DTHE);
	dprintf("GS_REG_COLCLAMP\n");
	SET_GS_REG(GS_REG_COLCLAMP);
	dprintf("GS_REG_PABE\n");
	SET_GS_REG(GS_REG_PABE);
	dprintf("GS_REG_BITBLTBUF\n");
	SET_GS_REG(GS_REG_BITBLTBUF);
	dprintf("GS_REG_TRXDIR\n");
	SET_GS_REG(GS_REG_TRXDIR);
	dprintf("GS_REG_TRXPOS\n");
	SET_GS_REG(GS_REG_TRXPOS);
	dprintf("GS_REG_TRXREG\n");
	SET_GS_REG(GS_REG_TRXREG);
	dprintf("GS_REG_TRXREG\n");
	SET_GS_REG(GS_REG_TRXREG);

	dprintf("CTXT 1 regs\n");
	SET_GS_REG(GS_REG_XYOFFSET_1);
	*(u64*)data_ptr |= 1; // Reload clut
	SET_GS_REG(GS_REG_TEX0_1);
	SET_GS_REG(GS_REG_TEX1_1);
	if(version <= 6)
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

	dprintf("CTXT 2 regs\n");
	SET_GS_REG(GS_REG_XYOFFSET_2);
	*(u64*)data_ptr |= 1; // Reload clut
	SET_GS_REG(GS_REG_TEX0_2);
	SET_GS_REG(GS_REG_TEX1_2);
	if(version <= 6)
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
		
	dprintf("finished regs\n");
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

	qword_t* reg_packet_end = q;
	// DO NOT KICK THE REGISTER PACKET HERE, WE ARE GOING TO CLOBBER REGISTERS BELOW!!!
	dma_channel_send_normal(DMA_CHANNEL_GIF, reg_packet, reg_packet_end - reg_packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	// kicks it anyways
	data_ptr += sizeof(u64); // obsolete apparently
	data_ptr += sizeof(u32); // m_tr.x
	data_ptr += sizeof(u32); // m_tr.y
	dprintf("Reading vram data at %p\n", data_ptr);
	qword_t* vram_packet = aligned_alloc(64, sizeof(qword_t) * 0x50005);

	u8* swizzle_vram = aligned_alloc(64, 0x400000);
	deswizzleImage(swizzle_vram, data_ptr, 1024 / 64, 1024 / 32);

	q = vram_packet;

	PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	q++;
	q->dw[0] = GS_SET_BITBLTBUF(0, 0, 0, 0, 16, 0);
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

	//(*(volatile u_int*)0x10003000) = 1;
	dprintf("kicking reg packet\n");
	dma_channel_send_normal(DMA_CHANNEL_GIF, reg_packet, reg_packet_end - reg_packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);
	free(reg_packet);
	free(vram_packet);
	free(swizzle_vram);
}
