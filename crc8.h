/*  
 * crc8.c
 * 
 * Computes a 8-bit CRC 
 * 
 */
#pragma once

#include <stdio.h>

#define CRC8_GP 0x107 /* x^8 + x^2 + x + 1 */
#define CRC8_DI 0x07

static unsigned char crc8_table[256]; /* 8-bit table */

static void init_crc8()
/*
      * Should be called before any other crc function.  
      */
{
	int i, j;
	unsigned char crc;

	for (i = 0; i < 256; i++)
	{
		crc = i;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80) ? CRC8_DI : 0);
		crc8_table[i] = crc & 0xFF;
		/* printf("table[%d] = %d (0x%X)\n", i, crc, crc); */
	}
}


static void crc8(unsigned char* crc, unsigned char m)
/*
      * For a byte array whose accumulated crc value is stored in *crc, computes
      * resultant crc obtained by appending m to the byte array
      */
{
	*crc = crc8_table[(*crc) ^ m];
	*crc &= 0xFF;
}

static void crc8_buffer(unsigned char* crc, unsigned char* buffer, int len)

{
	for (int i = 0; i < len; i++)
		crc8(crc, buffer[i]);
}
