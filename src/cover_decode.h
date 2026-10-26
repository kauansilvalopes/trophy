#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int decode_cover(const unsigned char* bytes, size_t length, uint32_t* pixels, unsigned edge);
int decode_cover_rect(const unsigned char* bytes, size_t length, uint32_t* pixels, unsigned width, unsigned height);
int decode_cover_rect_large(const unsigned char* bytes, size_t length, uint32_t* pixels, unsigned width, unsigned height);
#ifdef __cplusplus
}
#endif
