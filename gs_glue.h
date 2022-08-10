#pragma once

#include <tamtypes.h>

typedef struct {
	u8 type;
	u32 size;
	u8* data;
} gs_transfer_packet;

typedef struct 
{
    union
    {
        struct
        {
            u64  PMODE;
            u64         _pad1;
            u64 SMODE1;
            u64         _pad2;
            u64 SMODE2;
            u64         _pad3;
            u64  SRFSH;
            u64         _pad4;
            u64 SYNCH1;
            u64         _pad5;
            u64 SYNCH2;
            u64         _pad6;
            u64  SYNCV;
            u64         _pad7;
            struct
            {
                u64  DISPFB;
                u64          _pad1;
                u64 DISPLAY;
                u64          _pad2;
            } DISP[2];
            u64   EXTBUF;
            u64           _pad8;
            u64  EXTDATA;
            u64           _pad9;
            u64 EXTWRITE;
            u64           _pad10;
            u64  BGCOLOR;
            u64           _pad11;
        };

        u8 _pad12[0x1000];
    };

    union
    {
        struct
        {
            u64           CSR;
            u64           _pad13;
            u64            IMR;
            u64           _pad14;
            u64           _unk1[4];
            u64           BUSDIR;
            u64           _pad15;
            u64           _unk2[6];
            u64 SIGLBLID;
            u64           _pad16;
        };

        u8 _pad17[0x1000];
    };
} gs_registers_packet;

extern u8* gs_glue_transfer_data;

void gs_glue_transfer(u8* packet, u32 size);
void gs_glue_vsync();
void gs_glue_read_fifo(u32 size);
void gs_glue_registers(gs_registers_packet* packet);
void gs_glue_freeze(u8* data, u32 version);
