#ifndef _STDCOLOR_H
#define _STDCOLOR_H

#include "types.h"
#include "globals.h"

#define stdColor_LoadPalette_ADDR (0x00433680)
#define stdColor_GammaCorrect_ADDR (0x004336A0)
#define stdColor_FindClosest_ADDR (0x004337A0)
#define stdColor_RGBtoHSV_ADDR (0x00433890)
#define stdColor_HSVtoRGB_ADDR (0x00433A50)
#define stdColor_BuildRGB16LUT_ADDR (0x00433BD0)
#define stdColor_BuildRGBAKEY16LUT_ADDR (0x00433C70)
#define stdColor_BuildRGBA16LUT_ADDR (0x00433D40)
#define stdColor_ColorConvertOneRow_ADDR (0x00433E10)
#define stdColor_ColorConvertOnePixel_ADDR (0x00434040)
#define stdColor_Indexed8ToRGB16_ADDR (0x00434070)

int stdColor_Indexed8ToRGB16(uint8_t idx, rdColor24 *pal, rdTexformat *fmt);
uint32_t stdColor_ColorConvertOnePixel(rdTexformat *formatTo, int color, rdTexformat *formatFrom);
int stdColor_ColorConvertOneRow(uint8_t *outPixels, rdTexformat *formatTo, uint8_t *inPixels, rdTexformat *formatFrom, int numPixels);
int stdColor_GammaCorrect(uint8_t *a1, uint8_t *a2, int a3, flex_d_t a4);

//static int (*stdColor_GammaCorrect)(uint8_t *a1, uint8_t *a2, int a3, flex_d_t a4) = (void*)stdColor_GammaCorrect_ADDR;
//static int (*stdColor_ColorConvertOneRow)(uint8_t *outPixels, rdTexformat *formatTo, uint8_t *inPixels, rdTexformat *formatFrom, int numPixels) = (void*)stdColor_ColorConvertOneRow_ADDR;

uint32_t stdColor_ScaleColorComponent(uint32_t cc, int srcBPP, int deltaBPP);
uint32_t stdColor_EncodeRGB(const rdTexformat* ci, uint8_t r, uint8_t g, uint8_t b);
uint32_t stdColor_EncodeRGBA(const rdTexformat* ci, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void stdColor_DecodeRGB(uint32_t encoded, const rdTexformat* ci, uint8_t* r, uint8_t* g, uint8_t* b);
void stdColor_DecodeRGBA(uint32_t encoded, const rdTexformat* ci, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);
uint32_t stdColor_Recode(uint32_t encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI);

#ifdef TARGET_SSE

__m128i stdColor_ScaleColorComponentSIMD(__m128i cc, int srcBPP, int deltaBPP);
__m128i stdColor_EncodeRGBSIMD(const rdTexformat* ci, __m128i r, __m128i g, __m128i b);
__m128i stdColor_EncodeRGBASIMD(const rdTexformat* ci, __m128i r, __m128i g, __m128i b, __m128i a);
void stdColor_DecodeRGBSIMD(__m128i encoded, const rdTexformat* ci, __m128i* r, __m128i* g, __m128i* b);
void stdColor_DecodeRGBASIMD(__m128i encoded, const rdTexformat* ci, __m128i* r, __m128i* g, __m128i* b, __m128i* a);
__m128i stdColor_RecodeSIMD(__m128i encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI);

#endif

#ifdef TARGET_AVX2
__m256i stdColor_ScaleColorComponentSIMD_AVX2(__m256i cc, int srcBPP, int deltaBPP);
__m256i stdColor_EncodeRGBSIMD_AVX2(const rdTexformat* ci, __m256i r, __m256i g, __m256i b);
__m256i stdColor_EncodeRGBASIMD_AVX2(const rdTexformat* ci, __m256i r, __m256i g, __m256i b, __m256i a);
void stdColor_DecodeRGBSIMD_AVX2(__m256i encoded, const rdTexformat* ci, __m256i* r, __m256i* g, __m256i* b);
void stdColor_DecodeRGBASIMD_AVX2(__m256i encoded, const rdTexformat* ci, __m256i* r, __m256i* g, __m256i* b, __m256i* a);
__m256i stdColor_RecodeSIMD_AVX2(__m256i encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI);
#endif

// Added
int stdColor_FindClosest32(rdColor32* rgb, rdColor24* pal);

#endif // _STDCOLOR_H
