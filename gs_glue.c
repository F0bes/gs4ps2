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

static void _gs_glue_set_privileged(gs_registers_packet* packet);

static gs_registers_packet* privRegCache[2];

static s32 _gs_glue_vsync_handler(s32 cause)
{
	if (privRegCache[0] != NULL && privRegCache[1] != NULL)
	{
		_gs_glue_set_privileged(privRegCache[!(*GS_REG_CSR & (1 << 13))]);
	}

	ExitHandler();
	return 0;
}

static s32 vsyncHandler = 0;
void gs_glue_init(void)
{
	if (*(s32*)CFG_VALS[CFG_OPT_PRIV_CSR_AWARE])
	{
		DIntr();
		if (!vsyncHandler)
		{
			vsyncHandler = AddIntcHandler(INTC_VBLANK_S, _gs_glue_vsync_handler, -1);
			EnableIntc(INTC_VBLANK_S);
		}
		EIntr();
	}
	return;
}

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
		*GIFQWC = transfer_cnt < 0xF000 ? transfer_cnt : 0xF000;
		transfer_cnt -= *GIFQWC;
		FlushCache(0);
		*GIFCHCR = 0x100;
		u32 timeout = *(u32*)CFG_VALS[CFG_OPT_GIF_TIMEOUT];
		while (*GIFCHCR & 0x100)
		{
			if (timeout)
			{
				// I don't think this works, and when I made it work,
				// it didn't work :) (Possibly I had my delay set too low
				// and it was timing out the transfer mid transfer)
				if (!(timeout--))
				{
					dprint("!!!!!!!!!!!WARNING!!!!!!!!!!!\n");
					dprint("GIF TRANSFER TIMEOUT, RESETING GIF AND TRYING TO RECOVER\n");

					*(u32*)0x10003000 = 1; // Reset the GIF

					// Wait a bit so we can see the message above
					timeout = *(u32*)CFG_VALS[CFG_OPT_GIF_MSG_TIMEOUT];
					while (timeout--)
						asm volatile("nop\n");
					break;
				}
			}
		}

	} while (transfer_cnt > 0);
	return;
}

gs_vsync_data_header _gs_glue_frame1_header;
u128* _gs_glue_frame1_data = 0;
gs_vsync_data_header _gs_glue_frame2_header;
u128* _gs_glue_frame2_data = 0;

// Keep a copy of the last display settings
u64 _gs_glue_priv_PMODE;
u64 _gs_glue_priv_SMODE2;
u64 _gs_glue_priv_DISPFB1;
u64 _gs_glue_priv_DISPLAY1;
u64 _gs_glue_priv_DISPFB2;
u64 _gs_glue_priv_DISPLAY2;

static void _gs_glue_read_framebuffer(u64 dispfb, u64 display, gs_vsync_data_header* header, u128** dest)
{
	*GS_REG_CSR = 2; // Clear any previous FINISH events

	qword_t* packet = aligned_alloc(64, sizeof(qword_t) * 6);
	qword_t* q = packet;
	PACK_GIFTAG(q, GIF_SET_TAG(5, 1, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
	q++;
	u32 SBA = (dispfb & 0x1FF) * 32;
	u32 SBW = ((dispfb >> 9) & 0x3F);
	u32 SPSM = ((dispfb >> 15) & 0x1F);
	header->PSM = SPSM;
	q->dw[0] = GS_SET_BITBLTBUF(SBA, SBW, SPSM, 0, 0, 0);
	q->dw[1] = GS_REG_BITBLTBUF;
	q++;
	u32 SSAX = ((dispfb >> 32) & 0x7FF);
	u32 SSAY = ((dispfb >> 43) & 0x7FF);
	q->dw[0] = GS_SET_TRXPOS(SSAX, SSAY, 0, 0, 0);
	q->dw[1] = GS_REG_TRXPOS;
	q++;
	u32 MAGH = (((display >> 23) & 0xF)) + 1;
	u32 RRW = (((display >> 32) & 0xFFF) + 1) / MAGH;
	header->Width = RRW;
	u32 MAGV = (((display >> 27) & 0x3)) + 1;
	u32 RRH = (((display >> 44) & 0x7FF) + 1) / MAGV;
	if (_gs_glue_priv_SMODE2 & 2)
		RRH /= 2;

	header->Height = RRH;
	q->dw[0] = GS_SET_TRXREG(RRW, RRH);
	q->dw[1] = GS_REG_TRXREG;
	q++;
	q->dw[0] = GS_SET_FINISH(1);
	q->dw[1] = GS_REG_FINISH;
	q++;
	q->dw[0] = GS_SET_TRXDIR(1);
	q->dw[1] = GS_REG_TRXDIR;
	q++;

	dma_channel_send_normal(DMA_CHANNEL_GIF, packet, q - packet, 0, 0);
	dma_channel_wait(DMA_CHANNEL_GIF, 0);

	while (!(*GS_REG_CSR & 2))
		;

	// Our transfer should be set up, read the data from the fifo
	u32 size = 0;
	switch (SPSM)
	{
		case 0: // PSMCT32
			size = (RRW * RRH) * 4;
			break;
		case 1: // PSMCT24
			size = (RRW * RRH) * 3;
			break;
		case 2: // PSMCT16
		case 10: // PSMCT16S
			size = (RRW * RRH) * 2;
			break;
		default:
			size = (RRW * RRH) * 4;
			break;
	}
	header->Bytes = size;

	*dest = gs_glue_read_fifo(size / sizeof(qword_t));

	return;
}

static void _gs_glue_set_privileged(gs_registers_packet* packet)
{
	*GS_REG_BGCOLOR = packet->BGCOLOR;
	*GS_REG_EXTWRITE = packet->EXTWRITE;
	*GS_REG_EXTDATA = packet->EXTDATA;
	*GS_REG_EXTBUF = packet->EXTBUF;
	*GS_REG_DISPFB1 = packet->DISP[0].DISPFB;
	_gs_glue_priv_DISPFB1 = packet->DISP[0].DISPFB;
	*GS_REG_DISPLAY1 = packet->DISP[0].DISPLAY;
	_gs_glue_priv_DISPLAY1 = packet->DISP[0].DISPLAY;
	*GS_REG_DISPFB2 = packet->DISP[1].DISPFB;
	_gs_glue_priv_DISPFB2 = packet->DISP[1].DISPFB;
	*GS_REG_DISPLAY2 = packet->DISP[1].DISPLAY;
	_gs_glue_priv_DISPLAY2 = packet->DISP[1].DISPLAY;

	if (*(u32*)CFG_VALS[CFG_OPT_SYNCH_PRIV])
	{
		*GS_REG_SYNCHV = packet->SYNCV;
		*GS_REG_SYNCH2 = packet->SYNCH2;
		*GS_REG_SYNCH1 = packet->SYNCH1;
		*GS_REG_SRFSH = packet->SRFSH;
	}
	else
	{
		dprint("Skipping SYNCHV SYNCH2, SYNCH1, and SRFSH as per config.\n");
	}
	*GS_REG_SMODE2 = packet->SMODE2;
	_gs_glue_priv_SMODE2 = packet->SMODE2;
	*GS_REG_PMODE = packet->PMODE;
	_gs_glue_priv_PMODE = packet->PMODE;
	*GS_REG_SMODE1 = packet->SMODE1;
	return;
}

int gs_glue_vsync(gs_vsync_data_header* circuit1_header, u32* circuit1_ptr, gs_vsync_data_header* circuit2_header, u32* circuit2_ptr)
{
	u32 readCircuits = _gs_glue_priv_PMODE & 3;
	dprint("Vsync with PMODE 0x%08llx", _gs_glue_priv_PMODE);

	if (readCircuits & 1)
	{
		_gs_glue_read_framebuffer(_gs_glue_priv_DISPFB1, _gs_glue_priv_DISPLAY1, circuit1_header, &_gs_glue_frame1_data);
		circuit1_header->Circuit = 1;
		dprint("Read circuit 1");
	}
	if (readCircuits & 2)
	{
		_gs_glue_read_framebuffer(_gs_glue_priv_DISPFB2, _gs_glue_priv_DISPLAY2, circuit2_header, &_gs_glue_frame2_data);
		circuit2_header->Circuit = 2;
		dprint("Read circuit 2");
	}

	*circuit1_ptr = (u32)&_gs_glue_frame1_data[0];
	*circuit2_ptr = (u32)&_gs_glue_frame2_data[0];

	return readCircuits;
}

// FFX logo does a FIFO read
u128* gs_glue_read_fifo(u32 QWtotal)
{
	volatile u32* VIF1CHCR = (u32*)0x10009000;
	volatile u32* VIF1MADR = (u32*)0x10009010;
	volatile u32* VIF1QWC = (u32*)0x10009020;
	volatile u32* VIF1FIFO = (u32*)0x10005000;
	volatile u32* VIF1_STAT = (u32*)0x10003C00;

	*VIF1_STAT = (1 << 23); // Set the VIF FIFO direction to VIF1 -> Main Memory
	*GS_REG_BUSDIR = 1;
	u128* data = (u128*)aligned_alloc(64, QWtotal * sizeof(qword_t)); // TODO: Return this data?
	u32 QWrem = QWtotal;

	*VIF1MADR = (u32)data;
	dprint("Doing a readback of %d QW", QWtotal);
	while (QWrem >= 8) // Data transfers from the FIFO must be 128 byte aligned
	{
		u32 QWC = (((0xF000) < QWrem) ? (0xF000) : QWrem);
		QWC &= ~7;

		*VIF1QWC = QWC;
		QWrem -= QWC;

		dprint("    Reading chunk of %d QW. (Remaining %d)", QWC, QWrem);
		FlushCache(0);
		*VIF1CHCR = 0x100;
		asm __volatile__(" sync.l\n");
		while (*VIF1CHCR & 0x100)
		{
			//dprint("VIF1CHCR %X\nVIF1STAT %X\nVIF1QWC %X\nGIF_STAT %X\n"
			//,*VIF1CHCR,*VIF1_STAT, *VIF1QWC, *(volatile u64*)0x10003020);
		};
	}
	dprint("Finished off DMAC transfer, manually reading the %d QW from the fifo", QWrem);
	// Because we truncate the QWC, finish of the rest of the transfer
	// by reading the FIFO directly. Apparently this is how retail games do it.
	u32 qwFlushed = 0;
	while ((*VIF1_STAT >> 24))
	{
		dprint("VIF1_STAT=%llx", *VIF1_STAT);
		data[QWrem] = *VIF1FIFO;
		QWrem++;
		qwFlushed += 4;
		asm("nop\nnop\nnop\n");
	}
	dprint("Finished off the manual read (%d QW)\n", qwFlushed);
	*GS_REG_BUSDIR = 0; // Reset BUSDIR
	*VIF1_STAT = 0; // Reset FIFO direction

	dprint("Resetting TRXDIR");
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

	free(packet);
	return data;
}

void gs_glue_registers(gs_registers_packet* packet)
{
	// Cache our privileged registers and set them during vsync
	if (*(s32*)CFG_VALS[CFG_OPT_PRIV_CSR_AWARE])
	{
		s32 initial = 0;
		if (privRegCache[0] == NULL || privRegCache[1] == NULL)
		{
			initial = 1;
			dprint("Allocating memory for privileged register storage\n");
			privRegCache[0] = malloc(sizeof(gs_registers_packet));
			privRegCache[1] = malloc(sizeof(gs_registers_packet));
		}

		u32 FIELD = !!(packet->CSR & (1 << 13));
		if (initial)
		{
			memcpy(privRegCache[!FIELD], packet, sizeof(gs_registers_packet));
		}
		memcpy(privRegCache[FIELD], packet, sizeof(gs_registers_packet));
		// Allow the VSYNC handler to set the packet, it will know the proper,
		// expected FIELD
	}
	else // Set our privileged registers immediately if we aren't CSR aware
	{
		_gs_glue_set_privileged(packet);
	}
	return;
}

#define SET_GS_REG(reg) \
	q->dw[0] = *(u64*)data_ptr; \
	q->dw[1] = reg; \
	q++; \
	data_ptr += sizeof(u64);

void gs_glue_freeze(u8* data_ptr, u32 version)
{
	dprint("state version %d\n", version);
	qword_t* reg_packet = aligned_alloc(64, sizeof(qword_t) * 200);
	qword_t* q = reg_packet;

	PACK_GIFTAG(q, GIF_SET_TAG(3, 0, GIF_PRE_DISABLE, 0, GIF_FLG_PACKED, 13),
		GIF_REG_AD | (GIF_REG_AD << 4) | (GIF_REG_AD << 8) | (GIF_REG_AD << 12) | (GIF_REG_AD << 16) |
			(GIF_REG_AD << 20) | (GIF_REG_AD << 24) | ((u64)GIF_REG_AD << 28) | ((u64)GIF_REG_AD << 32) |
			((u64)GIF_REG_AD << 36) | ((u64)GIF_REG_AD << 40) | ((u64)GIF_REG_AD << 44) | ((u64)GIF_REG_AD << 48));

	q++;
	SET_GS_REG(GS_REG_PRIM);

	if (version <= 6)
		data_ptr += sizeof(u64); // PRMODE

	SET_GS_REG(GS_REG_PRMODECONT);
	SET_GS_REG(GS_REG_TEXCLUT);
	SET_GS_REG(GS_REG_SCANMSK);
	SET_GS_REG(GS_REG_TEXA);
	SET_GS_REG(GS_REG_FOGCOL);
	SET_GS_REG(GS_REG_DIMX);
	SET_GS_REG(GS_REG_DTHE);
	SET_GS_REG(GS_REG_COLCLAMP);
	;
	SET_GS_REG(GS_REG_PABE);
	SET_GS_REG(GS_REG_BITBLTBUF);
	SET_GS_REG(GS_REG_TRXDIR);
	SET_GS_REG(GS_REG_TRXPOS);
	SET_GS_REG(GS_REG_TRXREG);
	SET_GS_REG(GS_REG_TRXREG);

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
	return;
}
