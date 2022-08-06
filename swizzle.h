// thank you tellow

#pragma once

#include <stdint.h>
#include <stddef.h>

static void deswizzleColumn(uint8_t* dst, const uint8_t* src, size_t pitch) {
	uint64_t* dst0_pair = (uint64_t*)&dst[0 * pitch];
	uint64_t* dst1_pair = (uint64_t*)&dst[1 * pitch];
	uint64_t* src_pair = (uint64_t*)src;

	dst0_pair[0] = src_pair[0];
	dst0_pair[1] = src_pair[2];
	dst0_pair[2] = src_pair[4];
	dst0_pair[3] = src_pair[6];
	dst1_pair[0] = src_pair[1];
	dst1_pair[1] = src_pair[3];
	dst1_pair[2] = src_pair[5];
	dst1_pair[3] = src_pair[7];
}

static void deswizzleBlock(uint8_t* dst, const uint8_t* src, size_t pitch) {
	for (int i = 0; i < 4; i++) {
		deswizzleColumn(dst, src, pitch);
		dst += pitch * 2;
		src += 64;
	}
}

static void deswizzlePage(uint8_t* dst, const uint8_t* src, size_t pitch) {
	static const uint8_t page_lookup[4][8] = {
		{  0,  1,  4,  5, 16, 17, 20, 21 },
		{  2,  3,  6,  7, 18, 19, 22, 23 },
		{  8,  9, 12, 13, 24, 25, 28, 29 },
		{ 10, 11, 14, 15, 26, 27, 30, 31 },
	};
	const size_t block_advance_y = pitch * 8;
	const size_t block_size = 64 * 4;
	const size_t block_width = 8 * 4;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 8; x++) {
			const uint8_t* block_src = src + page_lookup[y][x] * block_size;
			deswizzleBlock(dst + x * block_width, block_src, pitch);
		}
		dst += block_advance_y;
	}
}

static void deswizzleImage(uint8_t* dst, const uint8_t* src, size_t pages_wide, size_t pages_high) {
	const size_t page_width = 64 * 4;
	const size_t page_size = page_width * 32;
	const size_t pitch = pages_wide * page_width;
	const size_t page_advance_y = pitch * 32;
	for (size_t y = 0; y < pages_high; y++) {
		for (size_t x = 0; x < pages_wide; x++) {
			deswizzlePage(dst + x * page_width, src, pitch);
			src += page_size;
		}
		dst += page_advance_y;
	}
}
