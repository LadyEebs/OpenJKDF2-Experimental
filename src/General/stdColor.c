#include "stdColor.h"

#include <float.h>
#include <math.h>
#include "jk.h"

int stdColor_Indexed8ToRGB16(uint8_t idx, rdColor24 *pal, rdTexformat *fmt)
{
    rdColor24 *v3; // esi

    v3 = (rdColor24 *)((char *)pal + 2 * idx + idx);
    return ((uint8_t)((uint8_t)v3->g >> (fmt->g_bitdiff & 0xFF)) << fmt->g_shift) | ((uint8_t)((uint8_t)v3->r >> (fmt->r_bitdiff & 0xFF)) << fmt->r_shift) | ((uint8_t)v3->b >> (fmt->b_bitdiff & 0xFF) << fmt->b_shift);
}

uint32_t stdColor_ColorConvertOnePixel(rdTexformat *formatTo, int color, rdTexformat *formatFrom)
{
    uint32_t tmp;
    stdColor_ColorConvertOneRow((uint8_t*)&tmp, formatTo, (uint8_t*)&color, formatFrom, 1);
    return tmp;
}

int stdColor_ColorConvertOneRow(uint8_t *outPixels, rdTexformat *formatTo, uint8_t *inPixels, rdTexformat *formatFrom, int numPixels)
{
    int v6; // eax
    int v8; // edx
    int v9; // edi
    int result; // eax
    uint32_t v11; // ebx
    uint32_t v12; // bx
    uint32_t v13; // edx
    uint32_t v14; // eax
    unsigned int v15; // ebx
    uint32_t v16; // edx
    uint32_t v17; // eax
    unsigned int v18; // ebx
    unsigned int v19; // eax
    uint8_t *v20; // ecx
    int v21; // zf
    unsigned int v22; // [esp+10h] [ebp-14h]
    unsigned int v23; // [esp+14h] [ebp-10h]
    int v24; // [esp+1Ch] [ebp-8h]
    int v25; // [esp+20h] [ebp-4h]
    int formatToa; // [esp+2Ch] [ebp+8h]
    int formatFroma; // [esp+34h] [ebp+10h]

    v6 = formatFrom->r_bits;
    v22 = 0xFFFFFFFF >> (32 - v6);
    v8 = formatFrom->g_bits;
    v23 = 0xFFFFFFFF >> (32 - v8);
    v9 = formatFrom->b_bits;
    v24 = v6 - formatTo->r_bits;
    result = numPixels;
    v25 = v8 - formatTo->g_bits;
    formatFroma = v9 - formatTo->b_bits;
    if ( numPixels > 0 )
    {
        v11 = (uint32_t)((intptr_t)inPixels & 0xFFFFFFFF);
        formatToa = numPixels;
        do
        {
            switch ( formatFrom->bpp )
            {
                case 8:
                    v11 = *inPixels;
                    break;
                case 16:
                    v11 = *(uint16_t*)inPixels;
                    break;
                case 24:
                    v12 = 0;
                    v12 |= ((uint32_t)inPixels[0]) << 8;
                    v12 |= ((uint32_t)inPixels[1]);
                    v11 = inPixels[2] | (v12 << 8);
                    break;
                case 32:
                    v11 = *(uint32_t*)inPixels;
                    break;
                default:
                    std_pHS->assert(
                        "Unsupported pixel depth.  Only 8, 16, 24, & 32 bits per pixel supported at the moment.",
                        ".\\General\\stdColor.c",
                        525);
                    break;
            }
            v13 = v22 & (v11 >> formatFrom->r_shift);
            v14 = v23 & (v11 >> formatFrom->g_shift);
            v15 = (0xFFFFFFFF >> (32 - v9)) & (v11 >> formatFrom->b_shift);
            if ( v24 <= 0 )
                v16 = v13 << -(char)v24;
            else
                v16 = v13 >> v24;
            if ( v25 <= 0 )
                v17 = v14 << -(char)v25;
            else
                v17 = v14 >> v25;
            if ( v24 <= 0 )
                v18 = v15 << -(char)formatFroma;
            else
                v18 = v15 >> formatFroma;
            v11 = (v16 << formatTo->r_shift) | (v18 << formatTo->b_shift) | (v17 << formatTo->g_shift);
            v19 = formatTo->bpp;
            switch ( v19 )
            {
                case 8u:
                    *outPixels = v11;
                    break;
                case 16u:
                    *(uint16_t*)outPixels = v11;
                    break;
                case 24u:
                    outPixels[0] = (v11 >> 16) & 0xFF;
                    outPixels[1] = (v11 >> 8) & 0xFF;
                    outPixels[2] = v11;
                    break;
                case 32u:
                    *(uint32_t*)outPixels = v11;
                    break;
                default:
                    break;
            }
            v20 = &outPixels[v19 >> 3];
            result = formatToa - 1;
            v21 = formatToa == 1;
            inPixels += (unsigned int)formatFrom->bpp >> 3;
            outPixels = v20;
            --formatToa;
        }
        while ( !v21 );
    }
    return result;
}

// can be sped up by precomputing a table
int stdColor_FindClosest32(rdColor32* rgb, rdColor24* pal)
{
	uint8_t idx = 0;
	int maxDist = INT_MAX;
	for (int k = 0; k < 256; ++k)
	{
		int dr = (int)rgb->r - (int)pal[k].r;
		int dg = (int)rgb->g - (int)pal[k].g;
		int db = (int)rgb->b - (int)pal[k].b;

		int dist = dr * dr + (dg * dg + (db * db));
		if (dist < maxDist)
		{
			idx = k;
			maxDist = dist;
		}
	}
	return idx;
}

int stdColor_GammaCorrect(uint8_t* dest, uint8_t* src, int count, flex_d_t gamma)
{
	const uint8_t MAX_COLOR = 255;

	if (count <= 0)
	{
		return count;
	}

	for (int i = 0; i < count; i++)
	{
		double normalized = src[i] / 255.0;
		double corrected = pow(normalized, gamma);
		double scaled = corrected * MAX_COLOR;
		dest[i] = (scaled > 255.0) ? 255 : ((scaled < 0.0) ? 0 : (uint8_t)scaled);
	}

	return count;
}

// from openjones3d
uint32_t stdColor_ScaleColorComponent(uint32_t cc, int srcBPP, int deltaBPP)
{
	if (deltaBPP <= 0) // Upscale
	{
		// Fixed: Fixed scaling to get correct value from lower bpp.
		//        Original was calculated only "cc >> -deltaBPP" which resulted in dimmer colors
		int dsrcBPP = srcBPP + deltaBPP;
		return (cc << -deltaBPP)
			| (dsrcBPP >= 0
			   ? (cc >> dsrcBPP)
			   : (cc * ((1 << -deltaBPP) - 1))); // Note: works for 1 bit, but might fail for 2 bit & 3 bit
	}

	// Downscale
	return cc >> deltaBPP;
}

#ifdef TARGET_SSE

__m128i stdColor_ScaleColorComponentSIMD(__m128i cc, int srcBPP, int deltaBPP)
{
	if (deltaBPP <= 0)
	{
		int shiftLeft = -deltaBPP;
		int dsrcBPP = srcBPP + deltaBPP;

		__m128i shiftedLeft = _mm_slli_epi32(cc, shiftLeft);

		__m128i shiftedRight;
		if (dsrcBPP >= 0)
		{
			shiftedRight = _mm_srli_epi32(cc, dsrcBPP);
		}
		else
		{
			int mulVal = (1 << shiftLeft) - 1;
			__m128i mulVec = _mm_set1_epi32(mulVal);
			shiftedRight = _mm_mullo_epi32(cc, mulVec);
		}

		return _mm_or_si128(shiftedLeft, shiftedRight);
	}
	else
	{
		return _mm_srli_epi32(cc, deltaBPP);
	}
}

#endif

uint32_t stdColor_EncodeRGB(const rdTexformat* ci, uint8_t r, uint8_t g, uint8_t b)
{
	// Scale color components according to bits per component
	uint32_t redScaled = r;
	uint32_t greenScaled = g;
	uint32_t blueScaled = b;

	// Adjust for component bit depth if needed
	if (ci->r_bits < 8)
	{
		redScaled = redScaled >> (8 - ci->r_bits);
	}
	if (ci->g_bits < 8)
	{
		greenScaled = greenScaled >> (8 - ci->g_bits);
	}
	if (ci->b_bits < 8)
	{
		blueScaled = blueScaled >> (8 - ci->b_bits);
	}

	// Shift components to their positions and combine
	uint32_t encoded = 0;
	if (ci->r_shift >= 0)
	{
		encoded |= (redScaled << ci->r_shift);
	}
	else
	{
		encoded |= (redScaled >> ci->r_bitdiff);
	}

	if (ci->g_shift >= 0)
	{
		encoded |= (greenScaled << ci->g_shift);
	}
	else
	{
		encoded |= (greenScaled >> ci->g_bitdiff);
	}

	if (ci->b_shift >= 0)
	{
		encoded |= (blueScaled << ci->b_shift);
	}
	else
	{
		encoded |= (blueScaled >> ci->b_bitdiff);
	}

	return encoded;
}

uint32_t stdColor_EncodeRGBA(const rdTexformat* ci, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	// Start with RGB encoding
	uint32_t encoded = stdColor_EncodeRGB(ci, r, g, b);

	// Add alpha if supported by the format
	if (ci->unk_40 > 0)
	{
		uint32_t alphaScaled = a;

		// Adjust for alpha bit depth if needed
		if (ci->unk_40 < 8)
		{
			alphaScaled = alphaScaled >> (8 - ci->unk_40);
		}

		// Shift alpha to its position and combine
		if (ci->unk_44 >= 0)
		{
			encoded |= (alphaScaled << ci->unk_44);
		}
		else
		{
			encoded |= (alphaScaled >> ci->unk_48);
		}
	}

	return encoded;
}

#ifdef TARGET_SSE


__m128i stdColor_EncodeRGBSIMD(const rdTexformat* ci, __m128i r, __m128i g, __m128i b)
{
	// If the component has fewer bits than 8, scale down
	if (ci->r_bits < 8)
		r = _mm_srli_epi32(r, 8 - ci->r_bits);
	if (ci->g_bits < 8)
		g = _mm_srli_epi32(g, 8 - ci->g_bits);
	if (ci->b_bits < 8)
		b = _mm_srli_epi32(b, 8 - ci->b_bits);

	// Shift components into their correct positions
	if (ci->r_shift >= 0)
		r = _mm_slli_epi32(r, ci->r_shift);
	else
		r = _mm_srli_epi32(r, ci->r_bitdiff);

	if (ci->g_shift >= 0)
		g = _mm_slli_epi32(g, ci->g_shift);
	else
		g = _mm_srli_epi32(g, ci->g_bitdiff);

	if (ci->b_shift >= 0)
		b = _mm_slli_epi32(b, ci->b_shift);
	else
		b = _mm_srli_epi32(b, ci->b_bitdiff);

	// Combine final encoded value
	__m128i encoded = _mm_or_si128(r, _mm_or_si128(g, b));
	return encoded; // 4x packed pixels
}

__m128i stdColor_EncodeRGBASIMD(const rdTexformat* ci, __m128i r, __m128i g, __m128i b, __m128i a)
{
	__m128i encoded = stdColor_EncodeRGBSIMD(ci, r, g, b);
	if (ci->unk_40 > 0)
	{
		__m128i alphaScaled = a;

		// Adjust for alpha bit depth if needed
		if (ci->unk_40 < 8)
			alphaScaled = _mm_srli_epi32(alphaScaled, 8 - ci->unk_40);

		// Shift alpha into position
		__m128i alphaPart;
		if (ci->unk_44 >= 0)
			alphaPart = _mm_slli_epi32(alphaScaled, ci->unk_44);
		else
			alphaPart = _mm_srli_epi32(alphaScaled, ci->unk_48);

		// Combine alpha with RGB
		encoded = _mm_or_si128(encoded, alphaPart);
	}

	return encoded;
}
#endif

void stdColor_DecodeRGB(uint32_t encoded, const rdTexformat* ci, uint8_t* r, uint8_t* g, uint8_t* b)
{
	// Create masks based on bit depths
	uint32_t redMask = ((1 << ci->r_bits) - 1);
	uint32_t greenMask = ((1 << ci->g_bits) - 1);
	uint32_t blueMask = ((1 << ci->b_bits) - 1);

	// Extract components using shifts and masks
	uint32_t redVal;
	if (ci->r_shift >= 0)
	{
		redVal = (encoded >> ci->r_shift) & redMask;
	}
	else
	{
		redVal = (encoded << ci->r_bitdiff) & redMask;
	}

	uint32_t greenVal;
	if (ci->g_shift >= 0)
	{
		greenVal = (encoded >> ci->g_shift) & greenMask;
	}
	else
	{
		greenVal = (encoded << ci->g_bitdiff) & greenMask;
	}

	uint32_t blueVal;
	if (ci->b_shift >= 0)
	{
		blueVal = (encoded >> ci->b_shift) & blueMask;
	}
	else
	{
		blueVal = (encoded << ci->b_bitdiff) & blueMask;
	}

	// Scale back to 8-bit range if needed
	if (ci->r_bits < 8)
	{
		// Scale up to fill 8 bits by replicating the MSBs
		redVal = (redVal << (8 - ci->r_bits)) | (redVal >> (2 * ci->r_bits - 8));
	}

	if (ci->g_bits < 8)
	{
		greenVal = (greenVal << (8 - ci->g_bits)) | (greenVal >> (2 * ci->g_bits - 8));
	}

	if (ci->b_bits < 8)
	{
		blueVal = (blueVal << (8 - ci->b_bits)) | (blueVal >> (2 * ci->b_bits - 8));
	}

	// Store the results
	*r = (uint8_t)redVal;
	*g = (uint8_t)greenVal;
	*b = (uint8_t)blueVal;
}

void stdColor_DecodeRGBA(uint32_t encoded, const rdTexformat* ci, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a)
{
	// First decode the RGB components
	stdColor_DecodeRGB(encoded, ci, r, g, b);

	// Then handle alpha if the format supports it
	if (ci->unk_40 > 0)
	{
		uint32_t alphaMask = ((1 << ci->unk_40) - 1);
		uint32_t alphaVal;

		// Extract alpha component
		if (ci->unk_44 >= 0)
		{
			alphaVal = (encoded >> ci->unk_44) & alphaMask;
		}
		else
		{
			alphaVal = (encoded << ci->unk_48) & alphaMask;
		}

		// Scale back to 8-bit range if needed
		if (ci->unk_40 < 8)
		{
			alphaVal = (alphaVal << (8 - ci->unk_40)) | (alphaVal >> (2 * ci->unk_40 - 8));
		}

		*a = (uint8_t)alphaVal;
	}
	else
	{
		// If format doesn't support alpha, set to fully opaque
		*a = 255;
	}
}

#ifdef TARGET_SSE
void stdColor_DecodeRGBSIMD(__m128i encoded, const rdTexformat* ci, __m128i* r, __m128i* g, __m128i* b)
{
	__m128i rMask = _mm_set1_epi32((1u << ci->r_bits) - 1);
	__m128i gMask = _mm_set1_epi32((1u << ci->g_bits) - 1);
	__m128i bMask = _mm_set1_epi32((1u << ci->b_bits) - 1);

	__m128i red, green, blue;

	// Extract channels
	if (ci->r_shift >= 0)
		red = _mm_and_si128(_mm_srli_epi32(encoded, ci->r_shift), rMask);
	else
		red = _mm_and_si128(_mm_slli_epi32(encoded, ci->r_bitdiff), rMask);

	if (ci->g_shift >= 0)
		green = _mm_and_si128(_mm_srli_epi32(encoded, ci->g_shift), gMask);
	else
		green = _mm_and_si128(_mm_slli_epi32(encoded, ci->g_bitdiff), gMask);

	if (ci->b_shift >= 0)
		blue = _mm_and_si128(_mm_srli_epi32(encoded, ci->b_shift), bMask);
	else
		blue = _mm_and_si128(_mm_slli_epi32(encoded, ci->b_bitdiff), bMask);

	// Expand to 8-bit range if needed
	if (ci->r_bits < 8)
	{
		int shift = 8 - ci->r_bits;
		int revShift = 2 * ci->r_bits - 8;
		red = _mm_or_si128(_mm_slli_epi32(red, shift), _mm_srli_epi32(red, revShift));
	}

	if (ci->g_bits < 8)
	{
		int shift = 8 - ci->g_bits;
		int revShift = 2 * ci->g_bits - 8;
		green = _mm_or_si128(_mm_slli_epi32(green, shift), _mm_srli_epi32(green, revShift));
	}

	if (ci->b_bits < 8)
	{
		int shift = 8 - ci->b_bits;
		int revShift = 2 * ci->b_bits - 8;
		blue = _mm_or_si128(_mm_slli_epi32(blue, shift), _mm_srli_epi32(blue, revShift));
	}

	*r = red;
	*g = green;
	*b = blue;
}

void stdColor_DecodeRGBASIMD(__m128i encoded, const rdTexformat* ci, __m128i* r, __m128i* g, __m128i* b, __m128i* a)
{
	stdColor_DecodeRGBSIMD(encoded, ci, r, g, b);

	__m128i alpha;
	if (ci->unk_40 > 0)
	{
		__m128i alphaMask = _mm_set1_epi32((1u << ci->unk_40) - 1);

		if (ci->unk_44 >= 0)
		{
			alpha = _mm_and_si128(_mm_srli_epi32(encoded, ci->unk_44), alphaMask);
		}
		else
		{
			alpha = _mm_and_si128(_mm_slli_epi32(encoded, ci->unk_48), alphaMask);
		}

		if (ci->unk_40 < 8)
		{
			int lshift = 8 - ci->unk_40;
			int rshift = 2 * ci->unk_40 - 8;

			alpha = _mm_or_si128(
				_mm_slli_epi32(alpha, lshift),
				_mm_srli_epi32(alpha, rshift)
			);
		}
	}
	else
	{
		// If no alpha, set all lanes to 255
		alpha = _mm_set1_epi32(255);
	}
	*a = alpha;
}
#endif

#ifdef TARGET_AVX2

__m256i stdColor_ScaleColorComponentSIMD_AVX2(__m256i cc, int srcBPP, int deltaBPP)
{
	if (deltaBPP <= 0)
	{
		int shiftLeft = -deltaBPP;
		int dsrcBPP = srcBPP + deltaBPP;

		__m256i shiftedLeft = _mm256_slli_epi32(cc, shiftLeft);

		__m256i shiftedRight;
		if (dsrcBPP >= 0)
		{
			shiftedRight = _mm256_srli_epi32(cc, dsrcBPP);
		}
		else
		{
			int mulVal = (1 << shiftLeft) - 1;
			__m256i mulVec = _mm256_set1_epi32(mulVal);
			shiftedRight = _mm256_mullo_epi32(cc, mulVec);
		}

		return _mm256_or_si256(shiftedLeft, shiftedRight);
	}
	else
	{
		return _mm256_srli_epi32(cc, deltaBPP);
	}
}

__m256i stdColor_EncodeRGBSIMD_AVX2(const rdTexformat* ci, __m256i r, __m256i g, __m256i b)
{
	if (ci->r_bits < 8)
		r = _mm256_srli_epi32(r, 8 - ci->r_bits);
	if (ci->g_bits < 8)
		g = _mm256_srli_epi32(g, 8 - ci->g_bits);
	if (ci->b_bits < 8)
		b = _mm256_srli_epi32(b, 8 - ci->b_bits);

	if (ci->r_shift >= 0)
		r = _mm256_slli_epi32(r, ci->r_shift);
	else
		r = _mm256_srli_epi32(r, ci->r_bitdiff);

	if (ci->g_shift >= 0)
		g = _mm256_slli_epi32(g, ci->g_shift);
	else
		g = _mm256_srli_epi32(g, ci->g_bitdiff);

	if (ci->b_shift >= 0)
		b = _mm256_slli_epi32(b, ci->b_shift);
	else
		b = _mm256_srli_epi32(b, ci->b_bitdiff);

	return _mm256_or_si256(r, _mm256_or_si256(g, b));
}

__m256i stdColor_EncodeRGBASIMD_AVX2(const rdTexformat* ci, __m256i r, __m256i g, __m256i b, __m256i a)
{
	__m256i encoded = stdColor_EncodeRGBSIMD_AVX2(ci, r, g, b);
	if (ci->unk_40 > 0)
	{
		__m256i alphaScaled = a;
		if (ci->unk_40 < 8)
			alphaScaled = _mm256_srli_epi32(alphaScaled, 8 - ci->unk_40);

		__m256i alphaPart;
		if (ci->unk_44 >= 0)
			alphaPart = _mm256_slli_epi32(alphaScaled, ci->unk_44);
		else
			alphaPart = _mm256_srli_epi32(alphaScaled, ci->unk_48);

		encoded = _mm256_or_si256(encoded, alphaPart);
	}
	return encoded;
}

void stdColor_DecodeRGBSIMD_AVX2(__m256i encoded, const rdTexformat* ci, __m256i* r, __m256i* g, __m256i* b)
{
	__m256i rMask = _mm256_set1_epi32((1u << ci->r_bits) - 1);
	__m256i gMask = _mm256_set1_epi32((1u << ci->g_bits) - 1);
	__m256i bMask = _mm256_set1_epi32((1u << ci->b_bits) - 1);

	__m256i red, green, blue;

	if (ci->r_shift >= 0)
		red = _mm256_and_si256(_mm256_srli_epi32(encoded, ci->r_shift), rMask);
	else
		red = _mm256_and_si256(_mm256_slli_epi32(encoded, ci->r_bitdiff), rMask);

	if (ci->g_shift >= 0)
		green = _mm256_and_si256(_mm256_srli_epi32(encoded, ci->g_shift), gMask);
	else
		green = _mm256_and_si256(_mm256_slli_epi32(encoded, ci->g_bitdiff), gMask);

	if (ci->b_shift >= 0)
		blue = _mm256_and_si256(_mm256_srli_epi32(encoded, ci->b_shift), bMask);
	else
		blue = _mm256_and_si256(_mm256_slli_epi32(encoded, ci->b_bitdiff), bMask);

	if (ci->r_bits < 8)
	{
		int shift = 8 - ci->r_bits;
		int revShift = 2 * ci->r_bits - 8;
		red = _mm256_or_si256(_mm256_slli_epi32(red, shift), _mm256_srli_epi32(red, revShift));
	}

	if (ci->g_bits < 8)
	{
		int shift = 8 - ci->g_bits;
		int revShift = 2 * ci->g_bits - 8;
		green = _mm256_or_si256(_mm256_slli_epi32(green, shift), _mm256_srli_epi32(green, revShift));
	}

	if (ci->b_bits < 8)
	{
		int shift = 8 - ci->b_bits;
		int revShift = 2 * ci->b_bits - 8;
		blue = _mm256_or_si256(_mm256_slli_epi32(blue, shift), _mm256_srli_epi32(blue, revShift));
	}

	*r = red;
	*g = green;
	*b = blue;
}

void stdColor_DecodeRGBASIMD_AVX2(__m256i encoded, const rdTexformat* ci, __m256i* r, __m256i* g, __m256i* b, __m256i* a)
{
	stdColor_DecodeRGBSIMD_AVX2(encoded, ci, r, g, b);

	__m256i alpha;
	if (ci->unk_40 > 0)
	{
		__m256i alphaMask = _mm256_set1_epi32((1u << ci->unk_40) - 1);

		if (ci->unk_44 >= 0)
			alpha = _mm256_and_si256(_mm256_srli_epi32(encoded, ci->unk_44), alphaMask);
		else
			alpha = _mm256_and_si256(_mm256_slli_epi32(encoded, ci->unk_48), alphaMask);

		if (ci->unk_40 < 8)
		{
			int lshift = 8 - ci->unk_40;
			int rshift = 2 * ci->unk_40 - 8;
			alpha = _mm256_or_si256(_mm256_slli_epi32(alpha, lshift), _mm256_srli_epi32(alpha, rshift));
		}
	}
	else
	{
		alpha = _mm256_set1_epi32(255);
	}
	*a = alpha;
}

__m256i stdColor_RecodeSIMD_AVX2(__m256i encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI)
{
	unsigned int rMask = 0xFFFFFFFF >> (32 - (pSrcCI->r_bits & 0xFF));
	unsigned int gMask = 0xFFFFFFFF >> (32 - (pSrcCI->g_bits & 0xFF));
	unsigned int bMask = 0xFFFFFFFF >> (32 - (pSrcCI->b_bits & 0xFF));
	unsigned int aMask = 0;

	if (pSrcCI->unk_40)
		aMask = 0xFFFFFFFF >> (32 - (pSrcCI->unk_40 & 0xFF));

	int rDelta = pSrcCI->r_bits - pDestCI->r_bits;
	int gDelta = pSrcCI->g_bits - pDestCI->g_bits;
	int bDelta = pSrcCI->b_bits - pDestCI->b_bits;
	int aDelta = 0;
	if (pSrcCI->unk_40)
		aDelta = pSrcCI->unk_40 - pDestCI->unk_40;

	__m256i rMaskVec = _mm256_set1_epi32(rMask);
	__m256i gMaskVec = _mm256_set1_epi32(gMask);
	__m256i bMaskVec = _mm256_set1_epi32(bMask);
	__m256i aMaskVec = _mm256_set1_epi32(aMask);

	__m256i r, g, b, a;

	if (pSrcCI->r_shift >= 0)
		r = _mm256_and_si256(_mm256_srli_epi32(encoded, pSrcCI->r_shift), rMaskVec);
	else
		r = _mm256_and_si256(_mm256_slli_epi32(encoded, pSrcCI->r_bitdiff), rMaskVec);

	if (pSrcCI->g_shift >= 0)
		g = _mm256_and_si256(_mm256_srli_epi32(encoded, pSrcCI->g_shift), gMaskVec);
	else
		g = _mm256_and_si256(_mm256_slli_epi32(encoded, pSrcCI->g_bitdiff), gMaskVec);

	if (pSrcCI->b_shift >= 0)
		b = _mm256_and_si256(_mm256_srli_epi32(encoded, pSrcCI->b_shift), bMaskVec);
	else
		b = _mm256_and_si256(_mm256_slli_epi32(encoded, pSrcCI->b_bitdiff), bMaskVec);

	if (pSrcCI->unk_40)
	{
		if (pSrcCI->unk_44 >= 0)
			a = _mm256_and_si256(_mm256_srli_epi32(encoded, pSrcCI->unk_44), aMaskVec);
		else
			a = _mm256_and_si256(_mm256_slli_epi32(encoded, pSrcCI->unk_48), aMaskVec);
	}
	else
	{
		a = _mm256_setzero_si256();
	}

	r = stdColor_ScaleColorComponentSIMD_AVX2(r, pSrcCI->r_bits, rDelta);
	g = stdColor_ScaleColorComponentSIMD_AVX2(g, pSrcCI->g_bits, gDelta);
	b = stdColor_ScaleColorComponentSIMD_AVX2(b, pSrcCI->b_bits, bDelta);

	if (pDestCI->b_shift >= 0)
		b = _mm256_slli_epi32(b, pDestCI->b_shift);
	else
		b = _mm256_srli_epi32(b, -pDestCI->b_shift);

	if (pDestCI->g_shift >= 0)
		g = _mm256_slli_epi32(g, pDestCI->g_shift);
	else
		g = _mm256_srli_epi32(g, -pDestCI->g_shift);

	if (pDestCI->r_shift >= 0)
		r = _mm256_slli_epi32(r, pDestCI->r_shift);
	else
		r = _mm256_srli_epi32(r, -pDestCI->r_shift);

	__m256i result = _mm256_or_si256(_mm256_or_si256(r, g), b);

	if (pSrcCI->unk_40)
	{
		a = stdColor_ScaleColorComponentSIMD_AVX2(a, pSrcCI->unk_40, aDelta);
		if (pDestCI->unk_44 >= 0)
			a = _mm256_slli_epi32(a, pDestCI->unk_44);
		else
			a = _mm256_srli_epi32(a, -pDestCI->unk_44);

		result = _mm256_or_si256(result, a);
	}

	return result;
}

#endif

uint32_t stdColor_Recode(uint32_t encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI)
{
	unsigned int redMask = 0xFFFFFFFF >> (32 - (pSrcCI->r_bits & 0xFF));
	unsigned int greenMask = 0xFFFFFFFF >> (32 - (pSrcCI->g_bits & 0xFF));
	unsigned int blueMask = 0xFFFFFFFF >> (32 - (pSrcCI->b_bits & 0xFF));
	unsigned int alphaMask = 0;
	unsigned int maxAlphaValue = 0;

	if (pSrcCI->unk_40)
	{
		alphaMask = 0xFFFFFFFF >> (32 - (pSrcCI->unk_40 & 0xFF));
		if ((255 >> pSrcCI->unk_48) / 2 <= 1)
		{
			maxAlphaValue = 1;
		}
		else
		{
			maxAlphaValue = (255 >> pSrcCI->unk_48) / 2;
		}
	}

	int redDelta = pSrcCI->r_bits - pDestCI->r_bits;
	int greenDelta = pSrcCI->g_bits - pDestCI->g_bits;
	int blueDelta = pSrcCI->b_bits - pDestCI->b_bits;
	int alphaDelta = 0;
	if (pSrcCI->unk_40)
	{
		alphaDelta = pSrcCI->unk_40 - pDestCI->unk_40;
	}

	// Decode
	uint32_t r = redMask & (encoded >> pSrcCI->r_shift);
	uint32_t g = greenMask & (encoded >> pSrcCI->g_shift);
	uint32_t b = blueMask & (encoded >> pSrcCI->b_shift);
	uint32_t a = 0;
	if (pSrcCI->unk_40)
	{
		a = alphaMask & (encoded >> pSrcCI->unk_44);
	}

	// Encode
	r = stdColor_ScaleColorComponent(r, pSrcCI->r_bits, redDelta);
	g = stdColor_ScaleColorComponent(g, pSrcCI->g_bits, greenDelta);
	b = stdColor_ScaleColorComponent(b, pSrcCI->b_bits, blueDelta);

	encoded = (b << pDestCI->b_shift) | (g << pDestCI->g_shift) | (r << pDestCI->r_shift);
	if (pSrcCI->unk_40)
	{
		a = stdColor_ScaleColorComponent(a, pSrcCI->unk_40, alphaDelta);
		encoded |= a << pDestCI->unk_44;
	}

	return encoded;
}

#ifdef TARGET_SSE

__m128i stdColor_RecodeSIMD(__m128i encoded, const rdTexformat* pSrcCI, const rdTexformat* pDestCI)
{
	unsigned int rMask = 0xFFFFFFFF >> (32 - (pSrcCI->r_bits & 0xFF));
	unsigned int gMask = 0xFFFFFFFF >> (32 - (pSrcCI->g_bits & 0xFF));
	unsigned int bMask = 0xFFFFFFFF >> (32 - (pSrcCI->b_bits & 0xFF));
	unsigned int aMask = 0;
	int maxAlphaValue = 0;

	if (pSrcCI->unk_40)
	{
		aMask = 0xFFFFFFFF >> (32 - (pSrcCI->unk_40 & 0xFF));
		if ((255 >> pSrcCI->unk_48) / 2 <= 1)
			maxAlphaValue = 1;
		else
			maxAlphaValue = (255 >> pSrcCI->unk_48) / 2;
	}

	int rDelta = pSrcCI->r_bits - pDestCI->r_bits;
	int gDelta = pSrcCI->g_bits - pDestCI->g_bits;
	int bDelta = pSrcCI->b_bits - pDestCI->b_bits;
	int aDelta = 0;
	if (pSrcCI->unk_40)
		aDelta = pSrcCI->unk_40 - pDestCI->unk_40;

	__m128i rMaskVec = _mm_set1_epi32(rMask);
	__m128i gMaskVec = _mm_set1_epi32(gMask);
	__m128i bMaskVec = _mm_set1_epi32(bMask);
	__m128i aMaskVec = _mm_set1_epi32(aMask);

	__m128i r, g, b, a;

	if (pSrcCI->r_shift >= 0)
		r = _mm_and_si128(_mm_srli_epi32(encoded, pSrcCI->r_shift), rMaskVec);
	else
		r = _mm_and_si128(_mm_slli_epi32(encoded, pSrcCI->r_bitdiff), rMaskVec);

	if (pSrcCI->g_shift >= 0)
		g = _mm_and_si128(_mm_srli_epi32(encoded, pSrcCI->g_shift), gMaskVec);
	else
		g = _mm_and_si128(_mm_slli_epi32(encoded, pSrcCI->g_bitdiff), gMaskVec);

	if (pSrcCI->b_shift >= 0)
		b = _mm_and_si128(_mm_srli_epi32(encoded, pSrcCI->b_shift), bMaskVec);
	else
		b = _mm_and_si128(_mm_slli_epi32(encoded, pSrcCI->b_bitdiff), bMaskVec);

	if (pSrcCI->unk_40)
	{
		if (pSrcCI->unk_44 >= 0)
			a = _mm_and_si128(_mm_srli_epi32(encoded, pSrcCI->unk_44), aMaskVec);
		else
			a = _mm_and_si128(_mm_slli_epi32(encoded, pSrcCI->unk_48), aMaskVec);
	}
	else
	{
		a = _mm_setzero_si128();
	}

	r = stdColor_ScaleColorComponentSIMD(r, pSrcCI->r_bits, rDelta);
	g = stdColor_ScaleColorComponentSIMD(g, pSrcCI->g_bits, gDelta);
	b = stdColor_ScaleColorComponentSIMD(b, pSrcCI->b_bits, bDelta);

	if (pDestCI->b_shift >= 0)
		b = _mm_slli_epi32(b, pDestCI->b_shift);
	else
		b = _mm_srli_epi32(b, -pDestCI->b_shift);

	if (pDestCI->g_shift >= 0)
		g = _mm_slli_epi32(g, pDestCI->g_shift);
	else
		g = _mm_srli_epi32(g, -pDestCI->g_shift);

	if (pDestCI->r_shift >= 0)
		r = _mm_slli_epi32(r, pDestCI->r_shift);
	else
		r = _mm_srli_epi32(r, -pDestCI->r_shift);

	__m128i result = _mm_or_si128(_mm_or_si128(r, g), b);

	if (pSrcCI->unk_40)
	{
		a = stdColor_ScaleColorComponentSIMD(a, pSrcCI->unk_40, aDelta);
		if (pDestCI->unk_44 >= 0)
			a = _mm_slli_epi32(a, pDestCI->unk_44);
		else
			a = _mm_srli_epi32(a, -pDestCI->unk_44);

		result = _mm_or_si128(result, a);
	}

	return result;
}

#endif
