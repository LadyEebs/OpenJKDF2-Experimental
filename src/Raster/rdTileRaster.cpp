#include "fixed.h"

#include <math.h>
#include <bitset>

#include "rdRaster.h"

#include "SDL.h"

extern "C" {

#include "Engine/rdroid.h"
#include "Engine/rdActive.h"
#include "Platform/std3D.h"
#include "Engine/rdColormap.h"
#include "General/stdMath.h"
#include "General/stdColor.h"
#include "Win95/stdDisplay.h"

#ifdef JOB_SYSTEM
#include "Modules/std/stdJob.h"
#endif

}

#ifdef TILE_SW_RASTER

typedef numeric::fixed<24, 8> fixed_t;

rdTexformat rdRaster_16bitMode = { STDCOLOR_RGBA, 16, 5, 5, 5, 10, 5, 0, 3, 3, 3, 1, 15, 7};
rdTexformat rdRaster_32bitMode = { STDCOLOR_RGBA, 32, 8, 8, 8, 16, 8, 0, 0, 0, 0, 8, 24, 0 };

static int       rdRaster_numTilePrimitives = 0; // number of primitives in the stream
static uint32_t  rdRaster_aTilePrimitiveOffsets[RDCACHE_MAX_TILE_TRIS]; // offsets per primitive into rdRaster_aTilePrimitiveStream
static uint8_t   rdRaster_aTilePrimitiveStream[RDCACHE_MAX_TILE_TRIS * 1024]; // primitive command byte stream

// Each face gets exactly this many bytes of stream space when jobs are dispatched
#define RD_FACE_STREAM_SLOT_SIZE (sizeof(rdRaster_aTilePrimitiveStream) / RDCACHE_MAX_TILE_TRIS)

// pointers to write to the command stream
static uint8_t*  rdRaster_pTileStreamStart = &rdRaster_aTilePrimitiveStream[0];
static uint8_t*  rdRaster_pTileStream = rdRaster_pTileStreamStart;
static uint8_t** rdRaster_ppTileStream = &rdRaster_pTileStream;

// per-thread tile framebuffer cache
static thread_local alignas(16) uint8_t rdRaster_TileColor[RDCACHE_FINE_TILE_SIZE * RDCACHE_FINE_TILE_SIZE];
static thread_local alignas(16) uint16_t rdRaster_TileDepth[RDCACHE_FINE_TILE_SIZE * RDCACHE_FINE_TILE_SIZE];
//static thread_local uint16_t rdRaster_TileHiZ = 0xFFFF;
static thread_local alignas(16) uint32_t rdRaster_TileColorRGBA[RDCACHE_FINE_TILE_SIZE * RDCACHE_FINE_TILE_SIZE];

static flex_t rdRaster_fogOffset = 0.0f;
static flex_t rdRaster_fogScale = 1.0f;

//flex_t stdMath_Rcp(fixed_t x)
//{
//	return 1.0f / (flex_t)x;
//}

//typedef float fixed_t;

//static inline __m128i mm_min_epu16_sse2(__m128i a, __m128i b)
//{
//	__m128i mask = _mm_cmplt_epi16(_mm_xor_si128(a, _mm_set1_epi16(0x8000)),
//								   _mm_xor_si128(b, _mm_set1_epi16(0x8000)));
//	return _mm_or_si128(_mm_and_si128(mask, a), _mm_andnot_si128(mask, b));
//}

typedef struct rdTriSetup
{
	fixed_t A0, A1, A2;
	fixed_t B0, B1, B2;
	fixed_t C0, C1, C2;
	fixed_t area;
} rdTriSetup;

typedef struct rdPoint2Fixed
{
	fixed_t x, y;
} rdPoint2Fixed;

typedef struct rdTri8
{
	uint8_t i1, i2, i3;
} rdTri8;

typedef struct rdTilePrimitiveHeader
{
	uint32_t numBytes;

	// Control flags
	uint32_t geoMode : 3;
	uint32_t texMode : 1;
	uint32_t lightMode : 3;
	uint32_t blend : 1;
	uint32_t discard : 1;
	uint32_t colorDepth : 2;
	uint32_t numTris : 5;

	union
	{
		uint64_t cmpHandle;
		rdColormap* pColormap;
	};
	int32_t minX, maxX;
	int32_t minY, maxY;
} rdTilePrimitiveHeader;

typedef struct rdTilePrimitiveTex
{
	// Pixel data pointer and UV wrap state
	union
	{
		uint64_t handle;
		uint8_t* pixels;
	};
	uint32_t uWrap, vWrap;
	uint32_t texRowShift, padding;
} rdTilePrimitiveTex;

typedef struct rdTilePrimitiveFmt
{
	// RGBA Format masks
	uint32_t r_mask : 8;
	uint32_t r_shift : 8;
	uint32_t r_loss : 8;
	uint32_t a_mask : 8;

	uint32_t g_mask : 8;
	uint32_t g_shift : 8;
	uint32_t g_loss : 8;
	uint32_t a_shift : 8;

	uint32_t b_mask : 8;
	uint32_t b_shift : 8;
	uint32_t b_loss : 8;
	uint32_t a_loss : 8;
} rdTilePrimitiveFmt;

typedef struct rdTileTriangleHeader
{
	// Tile min/max
	int32_t minX, maxX;
	int32_t minY, maxY;

	// Interpolants (edge functions), stored as fixed<24,8> raw integers
	int32_t w0_dx, w0_dy, w0_offset;
	int32_t w1_dx, w1_dy, w1_offset;
	int32_t w2_dx, w2_dy, w2_offset;

	flex_t z_dx, z_dy, z_offset;
} rdTileTriangleHeader;

typedef struct rdTileTriangleUVs
{
	flex_t u_dx, u_dy, u_offset;
	flex_t v_dx, v_dy, v_offset;
} rdTileTriangleUVs;

typedef struct rdTileTriangleLights
{
	//flex_t l0, l1, l2;
	flex_t l_dx, l_dy, l_offset;
} rdTileTriangleLights;


#undef min
#undef max


// Primitive draw commands are stream compacted (this way we can avoid having multiple different struct definitions across every pipeline)
struct rdRaster_PrimitiveEncoderDecoder
{
	rdRaster_PrimitiveEncoderDecoder(uint8_t** ppData, uint32_t* pNumBytesPtr = NULL)
		: ppData(ppData)
	{
		if(pNumBytesPtr)
			pNumBytes = pNumBytesPtr;
		else
			pNumBytes = (uint32_t*)(*ppData);
		*pNumBytes = 0;
	}

	template <typename T>
	T* Advance()
	{
		T* pPtr = (T*)(*ppData);
		(*ppData) += sizeof(T);
		*pNumBytes += sizeof(T);
		return pPtr;
	}

	void Finalize()
	{
		uint32_t* pEndOfList = Advance<uint32_t>();
		*pEndOfList = 0x80007FFF;
	}

	uint8_t** ppData;
	uint32_t* pNumBytes;
};

flex_t stdMath_Rcp(flex_t x)
{
	//flex_t rcp = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x)));
	//rcp = rcp * (2.0f - x * rcp);
	//return rcp;

	__m128 val = _mm_set_ss(x);               // Load scalar
	__m128 rcp = _mm_rcp_ss(val);             // Approximate (12-bit)
	rcp = _mm_mul_ss(rcp, _mm_sub_ss(_mm_set_ss(2.0f),
									 _mm_mul_ss(rcp, val)));    // Refine to ~23-bit precision
	return _mm_cvtss_f32(rcp);
//		union { float f; uint32_t i; } conv;
//		conv.f = x;
//
//		// Magic constant for reciprocal guess:
//		// Found empirically or through research (similar to fast inverse sqrt)
//		conv.i = 0x7EF311C3 - conv.i;
//
//		float y = conv.f;
//		y = y * (2.0f - x * y);
//		y = y * (2.0f - x * y);
//
//		return y;

	//unsigned i = *(unsigned*)&f;
	//i = 0x7effffffU - i;
	//return *(float*)&i;
}

int rdRaster_IsTopLeft(const rdPoint2Fixed* v0, const rdPoint2Fixed* v1)
{
	return (v0->y == v1->y && v0->x > v1->x) || (v0->y < v1->y);
}

bool rdRaster_SetupTriangle(rdTriSetup* out, const rdPoint2Fixed* v0, const rdPoint2Fixed* v1, const rdPoint2Fixed* v2)
{
	out->A0 = v2->y - v1->y;
	out->B0 = v1->x - v2->x;
	out->C0 = v1->y * v2->x - v1->x * v2->y;

	out->A1 = v0->y - v2->y;
	out->B1 = v2->x - v0->x;
	out->C1 = v2->y * v0->x - v2->x * v0->y;

	out->A2 = v1->y - v0->y;
	out->B2 = v0->x - v1->x;
	out->C2 = v0->y * v1->x - v0->x * v1->y;

	out->area = out->A2 * v2->x + out->B2 * v2->y + out->C2;
	//out->area = out->B1 * out->A2 - out->B2 * out->A1;
	//out->area = (v1->x - v0->x) * (v2->y - v0->y) - (v1->y - v0->y) * (v2->x - v0->x);

	if (out->area <= 1e-6 && out->area >= 1e-6)//fabs(out->area) <= 0.0f)
		return false;

	// If winding is opposite of area sign, flip edge equations
	if (out->area < 0.0f)
	{
		out->A0 = -out->A0; out->B0 = -out->B0; out->C0 = -out->C0;
		out->A1 = -out->A1; out->B1 = -out->B1; out->C1 = -out->C1;
		out->A2 = -out->A2; out->B2 = -out->B2; out->C2 = -out->C2;
		out->area = -out->area;
	}

	// bias for top left rule
	out->C0 += rdRaster_IsTopLeft(v1, v2) ? 0 : fixed_t::from_base(-1);
	out->C1 += rdRaster_IsTopLeft(v2, v0) ? 0 : fixed_t::from_base(-1);
	out->C2 += rdRaster_IsTopLeft(v0, v1) ? 0 : fixed_t::from_base(-1);

	return true;
}

int rdRaster_GetMipmapLevel(rdTexinfo* texinfo, flex_t z_min)
{
	int mipmap_level = 1;
	if (texinfo->texture_ptr->num_mipmaps == 2)
	{
		if (z_min <= (flex_d_t)rdroid_aMipDistances.y)
			mipmap_level = 0;
	}
	else if (texinfo->texture_ptr->num_mipmaps == 3)
	{
		if (z_min <= (flex_d_t)rdroid_aMipDistances.x)
			mipmap_level = 0;
		else if (z_min > (flex_d_t)rdroid_aMipDistances.y)
			mipmap_level = 2;
	}
	else if (texinfo->texture_ptr->num_mipmaps == 4)
	{
		if (z_min <= (flex_d_t)rdroid_aMipDistances.x)
		{
			mipmap_level = 0;
		}
		else if (z_min > (flex_d_t)rdroid_aMipDistances.y)
		{
			if (z_min > (flex_d_t)rdroid_aMipDistances.z)
				mipmap_level = 3;
			else
				mipmap_level = 2;
		}
	}
	else if (texinfo->texture_ptr->num_mipmaps == 1)
	{
		mipmap_level = 0;
	}
	return mipmap_level;
}

int rdRaster_SetupTiled(rdProcEntry* pFace, uint8_t** pData, int explicitSlot = -1)
{
	if (!pFace || pFace->geometryMode == 0 || !pFace->material)
		return 0;

	if (pFace->numVertices == 0 || pFace->numVertices > 32)
		return 0;

	int cel = (pFace->wallCel == 0xFFFFFFFF) ? pFace->material->celIdx : (int)pFace->wallCel;
	cel = stdMath_ClampInt(cel, 0, pFace->material->num_texinfo - 1);

	rdTexinfo* pTexinfo = pFace->material->texinfos[cel];
	if (!pTexinfo)
		return 0;

	int primitiveSlot = (explicitSlot >= 0) ? explicitSlot : rdRaster_numTilePrimitives++;
	rdRaster_aTilePrimitiveOffsets[primitiveSlot] = (uint8_t*)(*pData) - rdRaster_pTileStreamStart;

	int geoMode = (int8_t)(rdroid_curGeometryMode < pFace->geometryMode ? rdroid_curGeometryMode : pFace->geometryMode);
	int lightMode = (int8_t)(rdroid_curLightingMode < pFace->lightingMode ? rdroid_curLightingMode : pFace->lightingMode);
	int texMode = (int8_t)(rdroid_curTextureMode < pFace->textureMode ? rdroid_curTextureMode : pFace->textureMode);
	if (geoMode == 4 && (!(pTexinfo->header.texture_type & 8) || !pTexinfo->texture_ptr))
		geoMode = 3;

	int mipmap_level = 0;
	stdVBuffer* pTexture = NULL;
	if (geoMode == RD_GEOMODE_TEXTURED)
	{
		mipmap_level = rdRaster_GetMipmapLevel(pTexinfo, pFace->z_min * rdCamera_GetMipmapScalar());
		pTexture = pTexinfo->texture_ptr->texture_struct[mipmap_level];
	}

	flex_t invFar = stdMath_Rcp(rdCamera_pCurCamera->pClipFrustum->zFar);

	// precompute 1/z
#ifdef TARGET_SSE
	{
		const __m128 invFarV = _mm_set1_ps(invFar);
		const __m128 two     = _mm_set1_ps(2.0f);
		int i = 0;
		for (; i + 4 <= pFace->numVertices; i += 4)
		{
			// Gather the four z values (stride = sizeof(rdVector3) = 12 bytes)
			__m128 z = _mm_mul_ps(invFarV, _mm_set_ps(
				pFace->vertices[i+3].z, pFace->vertices[i+2].z,
				pFace->vertices[i+1].z, pFace->vertices[i+0].z));

			// rcp_ps + one Newton-Raphson refinement
			__m128 rcp = _mm_rcp_ps(z);
			rcp = _mm_mul_ps(rcp, _mm_sub_ps(two, _mm_mul_ps(z, rcp)));

			// Scatter back
			pFace->vertices[i+0].z = _mm_cvtss_f32(rcp);
			pFace->vertices[i+1].z = _mm_cvtss_f32(_mm_shuffle_ps(rcp, rcp, _MM_SHUFFLE(1,1,1,1)));
			pFace->vertices[i+2].z = _mm_cvtss_f32(_mm_shuffle_ps(rcp, rcp, _MM_SHUFFLE(2,2,2,2)));
			pFace->vertices[i+3].z = _mm_cvtss_f32(_mm_shuffle_ps(rcp, rcp, _MM_SHUFFLE(3,3,3,3)));
		}
		for (; i < pFace->numVertices; ++i)
			pFace->vertices[i].z = stdMath_Rcp(pFace->vertices[i].z * invFar);
	}
#else
	for (int i = 0; i < pFace->numVertices; ++i)
		pFace->vertices[i].z = stdMath_Rcp(pFace->vertices[i].z * invFar);
#endif

	// prescale/divide uvs
	if (geoMode == RD_GEOMODE_TEXTURED)
	{
		flex_t mip_scale = stdMath_Rcp((flex_t)(1 << mipmap_level));
#ifdef TARGET_SSE
		int i = 0;
		if (texMode == RD_TEXTUREMODE_PERSPECTIVE)
		{
			// scale varies per vertex: mip_scale * vertices[i].z  (z is already 1/z_world)
			const __m128 vMipScale = _mm_set1_ps(mip_scale);
			for (; i + 4 <= pFace->numVertices; i += 4)
			{
				__m128 scale = _mm_mul_ps(vMipScale, _mm_set_ps(
					pFace->vertices[i+3].z, pFace->vertices[i+2].z,
					pFace->vertices[i+1].z, pFace->vertices[i+0].z));

				// vertexUVs is packed {x,y,x,y,...}; load 4 vertices as two __m128
				__m128 uv01 = _mm_loadu_ps(&pFace->vertexUVs[i+0].x); // u0 v0 u1 v1
				__m128 uv23 = _mm_loadu_ps(&pFace->vertexUVs[i+2].x); // u2 v2 u3 v3

				// Broadcast each scale to both components: s0 s0 s1 s1 / s2 s2 s3 s3
				__m128 scale01 = _mm_unpacklo_ps(scale, scale);
				__m128 scale23 = _mm_unpackhi_ps(scale, scale);
				_mm_storeu_ps(&pFace->vertexUVs[i+0].x, _mm_mul_ps(uv01, scale01));
				_mm_storeu_ps(&pFace->vertexUVs[i+2].x, _mm_mul_ps(uv23, scale23));
			}
		}
		else
		{
			// Affine: uniform scale across all vertices
			const __m128 vScale = _mm_set1_ps(mip_scale);
			for (; i + 4 <= pFace->numVertices; i += 4)
			{
				__m128 uv01 = _mm_loadu_ps(&pFace->vertexUVs[i+0].x);
				__m128 uv23 = _mm_loadu_ps(&pFace->vertexUVs[i+2].x);
				_mm_storeu_ps(&pFace->vertexUVs[i+0].x, _mm_mul_ps(uv01, vScale));
				_mm_storeu_ps(&pFace->vertexUVs[i+2].x, _mm_mul_ps(uv23, vScale));
			}
		}
		for (; i < pFace->numVertices; ++i)
		{
			flex_t scale = mip_scale;
			if (texMode == RD_TEXTUREMODE_PERSPECTIVE)
				scale *= pFace->vertices[i].z;
			pFace->vertexUVs[i].x *= scale;
			pFace->vertexUVs[i].y *= scale;
		}
#else
		for (int i = 0; i < pFace->numVertices; ++i)
		{
			flex_t scale = mip_scale;
			if (texMode == RD_TEXTUREMODE_PERSPECTIVE)
				scale *= pFace->vertices[i].z;
			pFace->vertexUVs[i].x *= scale;
			pFace->vertexUVs[i].y *= scale;
		}
#endif
	}

	// preapply extra/ambient light
	flex_t ambientLight = 0.0f;
	if ((rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT) != 0)
		ambientLight = pFace->ambientLight;

	uint32_t flatLight = 0;
	if (lightMode == RD_LIGHTMODE_NOTLIT)
	{
		flex_t light = stdMath_Saturate(pFace->extralight);
		light = stdMath_Clamp(light, ambientLight, 1.0);
		if (rdCamera_pCurCamera->canvas->vbuffer->format.format.colorMode == 0)
			light *= 63.0f;
		else
			light *= 255.0f;
		flatLight = (uint32_t)(light);// * 256;
	}
	else if (lightMode == RD_LIGHTMODE_DIFFUSE)
	{
		flex_t light = pFace->extralight + pFace->light_level_static;
		light = stdMath_Clamp(light, ambientLight, 1.0);
		if (rdCamera_pCurCamera->canvas->vbuffer->format.format.colorMode == 0)
			light *= 63.0f;
		else
			light *= 255.0f;
		flatLight = (uint32_t)(light);// * 256;
	}
	else if (lightMode == RD_LIGHTMODE_GOURAUD)
	{
		const bool isPal = rdCamera_pCurCamera->canvas->vbuffer->format.format.colorMode == 0;
#ifdef TARGET_SSE
		{
			const __m128 extralight = _mm_set1_ps(pFace->extralight);
			const __m128 ambient    = _mm_set1_ps(ambientLight);
			const __m128 zero       = _mm_setzero_ps();
			const __m128 one        = _mm_set1_ps(1.0f);

			// PAL mode scales to 0-63; RGBA mode keeps 0-1 for direct channel multiply
			const __m128 scale      = _mm_set1_ps(isPal ? 63.0f : 1.0f);
			int i = 0;
			for (; i + 4 <= (int)pFace->numVertices; i += 4)
			{
				__m128 intensity = _mm_loadu_ps(&pFace->vertexIntensities[i]);

				// saturate(extralight + intensity)
				__m128 sum = _mm_min_ps(_mm_max_ps(_mm_add_ps(extralight, intensity), zero), one);

				// max(sum, ambient), then clamp to 1
				__m128 lit = _mm_min_ps(_mm_max_ps(sum, ambient), one);
				lit = _mm_mul_ps(lit, scale);
				_mm_storeu_ps(&pFace->vertexIntensities[i], lit);
			}
			for (; i < (int)pFace->numVertices; ++i)
			{
				float sum = stdMath_Saturate(pFace->extralight + pFace->vertexIntensities[i]);
				float lit = stdMath_Saturate(ambientLight < sum ? sum : ambientLight);
				if (isPal) lit *= 63.0f;
				pFace->vertexIntensities[i] = lit;
			}
		}
#else
		for (uint32_t i = 0; i < pFace->numVertices; ++i)
		{
			float sum = stdMath_Saturate(pFace->extralight + pFace->vertexIntensities[i]);
			float lit = stdMath_Saturate(ambientLight < sum ? sum : ambientLight);
			if (isPal) lit *= 63.0f;
			pFace->vertexIntensities[i] = lit;
		}
#endif
	}

	rdRaster_PrimitiveEncoderDecoder encoder(pData);

	// Setup the primitive header
	rdTilePrimitiveHeader* pHeader = encoder.Advance<rdTilePrimitiveHeader>();

	pHeader->geoMode = geoMode;
	pHeader->lightMode = lightMode;
	pHeader->texMode = texMode;
	if (pHeader->geoMode == RD_GEOMODE_TEXTURED)
	{
		pHeader->colorDepth = pTexture->format.format.bpp >> 3;
		pHeader->discard = (pTexinfo->texture_ptr->alpha_en & 1) != 0;
	}
	else
	{
		pHeader->discard = 0;
	}
	pHeader->blend = (pFace->type & 2) != 0;
	pHeader->pColormap = pFace->colormap;
	pHeader->minX = pFace->x_min;
	pHeader->maxX = pFace->x_max;
	pHeader->minY = pFace->y_min;
	pHeader->maxY = pFace->y_max;
	pHeader->numTris = pFace->numVertices - 2;
	
	// Texture/color setup
	if (geoMode == RD_GEOMODE_SOLIDCOLOR)
	{
		uint32_t* pSolidColor = encoder.Advance<uint32_t>();
		*pSolidColor = pTexinfo->header.field_4;
	}
	else if (geoMode == RD_GEOMODE_TEXTURED)
	{
		flex_t mip_scale = stdMath_Rcp((flex_t)(1 << mipmap_level));

		rdTilePrimitiveTex* pTex = encoder.Advance<rdTilePrimitiveTex>();
		pTex->pixels = (uint8_t*)pTexture->surface_lock_alloc;

		pTex->texRowShift = pTexinfo->texture_ptr->width_bitcnt - mipmap_level;
		pTex->uWrap = (pTexinfo->texture_ptr->width_minus_1 >> (mipmap_level & 31)) << 16;
		pTex->vWrap = (pTexinfo->texture_ptr->height_minus_1 >> (mipmap_level & 31)) << (pTex->texRowShift & 31);

		if (pTexture->format.format.colorMode > STDCOLOR_PAL)
		{
			rdTilePrimitiveFmt* pFormat = encoder.Advance<rdTilePrimitiveFmt>();
			pFormat->r_shift = (int8_t)pTexture->format.format.r_shift;
			pFormat->g_shift = (int8_t)pTexture->format.format.g_shift;
			pFormat->b_shift = (int8_t)pTexture->format.format.b_shift;

			pFormat->r_loss = (int8_t)pTexture->format.format.r_bitdiff;
			pFormat->g_loss = (int8_t)pTexture->format.format.g_bitdiff;
			pFormat->b_loss = (int8_t)pTexture->format.format.b_bitdiff;

			pFormat->r_mask = (int8_t)((1u << pTexture->format.format.r_bits) - 1);
			pFormat->g_mask = (int8_t)((1u << pTexture->format.format.g_bits) - 1);
			pFormat->b_mask = (int8_t)((1u << pTexture->format.format.b_bits) - 1);
		
			pFormat->a_mask = (int8_t)((1u << pTexture->format.format.unk_40) - 1);
			pFormat->a_shift = (int8_t)pTexture->format.format.unk_44;
			pFormat->a_loss = (int8_t)pTexture->format.format.unk_48;
		}
	}

	// Push face light data
	if (lightMode == RD_LIGHTMODE_NOTLIT || lightMode == RD_LIGHTMODE_DIFFUSE)
	{
		uint32_t* pFlatLight = encoder.Advance<uint32_t>();
		*pFlatLight = flatLight;
	}

	// Run through the face and generate triangles
	int numPrims = 0;
	int numTris = pFace->numVertices - 2;

	rdTri8 tri;
	tri.i1 = 0;
	tri.i2 = 1;
	tri.i3 = (uint8_t)pFace->numVertices - 1;
	for (int triIdx = 0; triIdx < numTris; ++triIdx)
	{
		const rdVector3* v0 = &pFace->vertices[tri.i1];
		const rdVector3* v1 = &pFace->vertices[tri.i2];
		const rdVector3* v2 = &pFace->vertices[tri.i3];

		const rdPoint2Fixed p0 = { v0->x, v0->y };
		const rdPoint2Fixed p1 = { v1->x, v1->y };
		const rdPoint2Fixed p2 = { v2->x, v2->y };

		// Setup the edge functions
		rdTriSetup setup;
		if (!rdRaster_SetupTriangle(&setup, &p0, &p1, &p2))
		{
			if ((triIdx & 1) != 0)
				tri.i1 = tri.i3--;
			else
				tri.i1 = tri.i2++;
			continue; // Degenerate triangle
		}

		uint32_t* pTriIdx = encoder.Advance<uint32_t>();
		*pTriIdx = triIdx;

		flex_t invArea = stdMath_Rcp(setup.area);

		rdTileTriangleHeader* pTriHeader = encoder.Advance<rdTileTriangleHeader>();

		pTriHeader->minX = (int32_t)std::floor((flex_t)std::min(std::min(p0.x, p1.x), p2.x));
		pTriHeader->maxX = (int32_t)std::ceil((flex_t)std::max(std::max(p0.x, p1.x), p2.x));
		pTriHeader->minY = (int32_t)std::floor((flex_t)std::min(std::min(p0.y, p1.y), p2.y));
		pTriHeader->maxY = (int32_t)std::ceil((flex_t)std::max(std::max(p0.y, p1.y), p2.y));

		flex_t z0 = pFace->vertices[tri.i1].z;
		flex_t z1 = (pFace->vertices[tri.i2].z - z0) * invArea;
		flex_t z2 = (pFace->vertices[tri.i3].z - z0) * invArea;

		// Interpolants (edge functions), stored as fixed<24,8> raw integers
		pTriHeader->w0_dx = setup.A0.to_raw();
		pTriHeader->w0_dy = setup.B0.to_raw();
		pTriHeader->w0_offset = (setup.C0 + 0.5f * (setup.A0 + setup.B0)).to_raw(); // half texel offset

		pTriHeader->w1_dx = setup.A1.to_raw();
		pTriHeader->w1_dy = setup.B1.to_raw();
		pTriHeader->w1_offset = (setup.C1 + 0.5f * (setup.A1 + setup.B1)).to_raw(); // half texel offset

		pTriHeader->w2_dx = setup.A2.to_raw();
		pTriHeader->w2_dy = setup.B2.to_raw();
		pTriHeader->w2_offset = (setup.C2 + 0.5f * (setup.A2 + setup.B2)).to_raw(); // half texel offset

		static constexpr float kEdgeScale = 1.0f / (1 << 8);
		pTriHeader->z_offset = z2 * ((flex_t)pTriHeader->w2_offset * kEdgeScale) + z1 * ((flex_t)pTriHeader->w1_offset * kEdgeScale) + z0;
		pTriHeader->z_dx = z2 * ((flex_t)setup.A2.to_raw() * kEdgeScale) + z1 * ((flex_t)setup.A1.to_raw() * kEdgeScale);
		pTriHeader->z_dy = z2 * ((flex_t)setup.B2.to_raw() * kEdgeScale) + z1 * ((flex_t)setup.B1.to_raw() * kEdgeScale);

		if (geoMode == RD_GEOMODE_TEXTURED)
		{
			flex_t u0 = pFace->vertexUVs[tri.i1].x;
			flex_t u1 = (pFace->vertexUVs[tri.i2].x - u0) * invArea;
			flex_t u2 = (pFace->vertexUVs[tri.i3].x - u0) * invArea;

			flex_t v0 = pFace->vertexUVs[tri.i1].y;
			flex_t v1 = (pFace->vertexUVs[tri.i2].y - v0) * invArea;
			flex_t v2 = (pFace->vertexUVs[tri.i3].y - v0) * invArea;

			rdTileTriangleUVs* pUVs = encoder.Advance<rdTileTriangleUVs>();
			pUVs->u_offset = u2 * ((flex_t)pTriHeader->w2_offset * kEdgeScale) + u1 * ((flex_t)pTriHeader->w1_offset * kEdgeScale) + u0;
			pUVs->u_dx = u2 * ((flex_t)setup.A2.to_raw() * kEdgeScale) + u1 * ((flex_t)setup.A1.to_raw() * kEdgeScale);
			pUVs->u_dy = u2 * ((flex_t)setup.B2.to_raw() * kEdgeScale) + u1 * ((flex_t)setup.B1.to_raw() * kEdgeScale);

			pUVs->v_offset = v2 * ((flex_t)pTriHeader->w2_offset * kEdgeScale) + v1 * ((flex_t)pTriHeader->w1_offset * kEdgeScale) + v0;
			pUVs->v_dx = v2 * ((flex_t)setup.A2.to_raw() * kEdgeScale) + v1 * ((flex_t)setup.A1.to_raw() * kEdgeScale);
			pUVs->v_dy = v2 * ((flex_t)setup.B2.to_raw() * kEdgeScale) + v1 * ((flex_t)setup.B1.to_raw() * kEdgeScale);
		}

		if (lightMode == RD_LIGHTMODE_GOURAUD)
		{
			flex_t l0 = pFace->vertexIntensities[tri.i1];
			flex_t l1 = (pFace->vertexIntensities[tri.i2] - l0) * invArea;
			flex_t l2 = (pFace->vertexIntensities[tri.i3] - l0) * invArea;

			rdTileTriangleLights* pLights = encoder.Advance<rdTileTriangleLights>();
			pLights->l_offset = l2 * ((flex_t)pTriHeader->w2_offset * kEdgeScale) + l1 * ((flex_t)pTriHeader->w1_offset * kEdgeScale) + l0;
			pLights->l_dx = l2 * ((flex_t)setup.A2.to_raw() * kEdgeScale) + l1 * ((flex_t)setup.A1.to_raw() * kEdgeScale);
			pLights->l_dy = l2 * ((flex_t)setup.B2.to_raw() * kEdgeScale) + l1 * ((flex_t)setup.B1.to_raw() * kEdgeScale);
		}

		// Move to next triangle
		if ((triIdx & 1) != 0)
			tri.i1 = tri.i3--;
		else
			tri.i1 = tri.i2++;

		numPrims++;
	}

	encoder.Finalize();

	return numPrims;
}

// Conditional types
struct empty {};
template <bool Enabled, typename T> using maybe = std::conditional_t<Enabled, T, empty>;

template <bool Enabled> using uint8_maybe = maybe<Enabled, uint8_t>;
template <bool Enabled> using flex_maybe = maybe<Enabled, flex_t>;
template <bool Enabled> using int_maybe = maybe<Enabled, int32_t>;
template <bool Enabled> using uint_maybe = maybe<Enabled, uint32_t>;

typedef struct rdTileDrawCommand
{
	rdRaster_PrimitiveEncoderDecoder* pDecoder;
	const rdTilePrimitiveHeader* pHeader;
	const rdTilePrimitiveTex* pTexData;
	const rdTilePrimitiveFmt* pFormat;
	uint32_t solidColor;
	uint32_t flatLight;
} rdTileDrawCommand;

static const uint32_t rdRaster_DitherLUT[16] = {
	0, 4, 1, 5,
	6, 2, 7, 3,
	1, 5, 0, 4,
	7, 3, 6, 2
};

uint32_t rdRaster_GetDither(int x, int y)
{
	return rdRaster_DitherLUT[(x & 3) + (y & 3) * 4];
}

rdVector2 rdRaster_GetTexDither(int x, int y)
{
	static const rdVector2 rdRaster_TexDitherLUT[4] = {
		{0.25, 0.00}, {0.50, 0.75},
		{0.75, 0.50}, {0.00, 0.25}
	};

	return rdRaster_TexDitherLUT[2 * (y & 1) + (x&1)];
}

#ifdef TARGET_AVX2
template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage, int8_t LightMode, bool UseDiscard, bool UseAlpha>
void rdRaster_DrawToTileSIMD_AVX2(/*rdTilePrimitive* prim,*/ rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	// Setup a few compile time options
	static constexpr bool ReadZ = ZMethod == RD_ZBUFFER_READ_NOWRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool WriteZ = ZMethod == RD_ZBUFFER_NOREAD_WRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool UseFlatLight = (LightMode == RD_LIGHTMODE_NOTLIT) || (LightMode == RD_LIGHTMODE_DIFFUSE);
	static constexpr bool UseGouraud = LightMode == RD_LIGHTMODE_GOURAUD;
	static constexpr bool UseSolidColor = GeoMode == RD_GEOMODE_SOLIDCOLOR;
	static constexpr bool UseTexture = GeoMode == RD_GEOMODE_TEXTURED && TextureMode >= 0;
	static constexpr bool Perspective = TextureMode == RD_TEXTUREMODE_PERSPECTIVE;
	static constexpr bool IsIndexedColor = std::is_same_v<TextureStorage, uint8_t>;
	static constexpr bool IsRGBAOutput = (ColorMode != STDCOLOR_PAL);

	int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
	int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;
	int tileMaxX = tileMinX + RDCACHE_FINE_TILE_SIZE - 1;
	int tileMaxY = tileMinY + RDCACHE_FINE_TILE_SIZE - 1;

	rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
	stdVBuffer* pVBuffer = pCanvas->vbuffer;

	uint_maybe<IsRGBAOutput> canvas_r_mask, canvas_g_mask, canvas_b_mask;

	if constexpr (IsRGBAOutput)
	{
		canvas_r_mask = (int8_t)((1u << pVBuffer->format.format.r_bits) - 1);
		canvas_g_mask = (int8_t)((1u << pVBuffer->format.format.g_bits) - 1);
		canvas_b_mask = (int8_t)((1u << pVBuffer->format.format.b_bits) - 1);
	}

	// todo: I assume this gets compiled out for permutations that don't use them?
	rdColor24* palette = pCommand->pHeader->pColormap->colors;
	uint8_t* transparency = ((uint8_t*)pCommand->pHeader->pColormap->transparency);
	uint8_t* lightLevels = ((uint8_t*)pCommand->pHeader->pColormap->lightlevel);
	uint8_t* searchTable = pCommand->pHeader->pColormap->searchTable;

	rdRaster_PrimitiveEncoderDecoder* pDecoder = pCommand->pDecoder;

	// SIMD constants
	const __m256 stride = _mm256_set1_ps(8.0f);

	const __m256 indices = _mm256_set_ps(
		7.0f, 6.0f, 5.0f, 4.0f,
		3.0f, 2.0f, 1.0f, 0.0f);

	const __m256 uvFixedScale = _mm256_set1_ps(rdRaster_fixedScale);
	const __m256i uvCenter = _mm256_set1_epi32(0x8000);

	const float alpha = 90.0f / 255.0f;
	const __m256 alpha_ps = _mm256_set1_ps(alpha);
	const __m256 one_minus_alpha = _mm256_set1_ps(1.0f - alpha);

	maybe<!UseTexture, __m256i> solidColor;

	if constexpr (!UseTexture)
	{
		if constexpr (IsRGBAOutput)
		{
			rdColor24 rgb24 = palette[pCommand->solidColor & 0xFF];

			solidColor = _mm256_set1_epi32(
				stdColor_EncodeRGBA(
					&rdRaster_32bitMode,
					rgb24.r,
					rgb24.g,
					rgb24.b,
					pCommand->solidColor > 0 ? 255 : 0));
		}
		else
		{
			solidColor = _mm256_set1_epi32(0x01010101u * (pCommand->solidColor & 0xFF));
		}
	}

	uint_maybe<UseTexture> texRowShift;
	maybe<UseTexture, TextureStorage*> pixels;
	maybe<UseTexture, __m256i> uWrap, vWrap;

	if constexpr (UseTexture)
	{
		texRowShift = pCommand->pTexData->texRowShift;
		pixels = (TextureStorage*)pCommand->pTexData->pixels;

		uWrap = _mm256_set1_epi32(pCommand->pTexData->uWrap);
		vWrap = _mm256_set1_epi32(pCommand->pTexData->vWrap);
	}

	maybe<!IsIndexedColor, __m256i> r_mask, g_mask, b_mask, a_mask;
	uint_maybe<!IsIndexedColor> r_shift, g_shift, b_shift, a_shift;
	uint_maybe<!IsIndexedColor> r_loss, g_loss, b_loss, a_loss;

	if constexpr (!IsIndexedColor)
	{
		r_mask = _mm256_set1_epi32(pCommand->pFormat->r_mask);
		g_mask = _mm256_set1_epi32(pCommand->pFormat->g_mask);
		b_mask = _mm256_set1_epi32(pCommand->pFormat->b_mask);
		a_mask = _mm256_set1_epi32(pCommand->pFormat->a_mask);

		r_shift = pCommand->pFormat->r_shift;
		g_shift = pCommand->pFormat->g_shift;
		b_shift = pCommand->pFormat->b_shift;
		a_shift = pCommand->pFormat->a_shift;

		r_loss = pCommand->pFormat->r_loss;
		g_loss = pCommand->pFormat->g_loss;
		b_loss = pCommand->pFormat->b_loss;
		a_loss = pCommand->pFormat->a_loss;
	}

	maybe<UseFlatLight, __m256> flatLight;

	if constexpr (UseFlatLight)
		flatLight = _mm256_set1_ps(pCommand->flatLight / 255.0f);

	// Pre-decode all triangles from the stream into a contiguous local array so that
	// the hot rasterization loop operates on cache-friendly sequential data, rather than
	// pointer-chasing through the encoded byte stream
	struct rdPreDecodedTri
	{
		rdTileTriangleHeader hdr;
		maybe<UseTexture, rdTileTriangleUVs> uvs;
		maybe<UseGouraud, rdTileTriangleLights> lights;
	};

	constexpr int kMaxTris = 30;

	rdPreDecodedTri decoded[kMaxTris];
	int numDecoded = 0;

	{
		uint32_t triIdx = *pDecoder->Advance<const uint32_t>();

		while ((triIdx != 0x80007FFF) &&
			   (triIdx < pCommand->pHeader->numTris) &&
			   (numDecoded < kMaxTris))
		{
			auto& t = decoded[numDecoded++];

			t.hdr = *pDecoder->Advance<const rdTileTriangleHeader>();

			if constexpr (UseTexture)
				t.uvs = *pDecoder->Advance<const rdTileTriangleUVs>();

			if constexpr (UseGouraud)
				t.lights = *pDecoder->Advance<const rdTileTriangleLights>();

			triIdx = *pDecoder->Advance<const uint32_t>();
		}
	}

	// Rasterize using the pre-decoded, cache-friendly triangle array
	for (int triNum = 0; triNum < numDecoded; triNum++)
	{
		const rdTileTriangleHeader* pTriHeader = &decoded[triNum].hdr;

		int minX = std::min(std::max(pTriHeader->minX, tileMinX), tileMaxX);
		int maxX = std::min(std::max(pTriHeader->maxX, tileMinX), tileMaxX);
		int minY = std::min(std::max(pTriHeader->minY, tileMinY), tileMaxY);
		int maxY = std::min(std::max(pTriHeader->maxY, tileMinY), tileMaxY);

		int tileLocalMinX = minX - tileMinX;
		int tileLocalMaxX = maxX - tileMinX;

		// Align for 8 pixel processing
		int simdStartX = tileMinX + (tileLocalMinX & ~0x7);
		int simdEndX = tileMinX + (tileLocalMaxX & ~0x7);

		simdEndX = std::min(simdEndX, tileMaxX - 7);

		int tileOffsetX = simdStartX - tileMinX;
		int tileOffset = tileOffsetX - tileMinY * RDCACHE_FINE_TILE_SIZE;

		const __m256 x_offsets = _mm256_add_ps(_mm256_set1_ps((flex_t)simdStartX), indices);
		const __m256 y_offsets = _mm256_set1_ps((flex_t)minY);

		// Edge function stepping
		const __m256i lane_offsets_i = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);

		const __m256i w0_dy_i = _mm256_set1_epi32(pTriHeader->w0_dy);
		const __m256i w1_dy_i = _mm256_set1_epi32(pTriHeader->w1_dy);
		const __m256i w2_dy_i = _mm256_set1_epi32(pTriHeader->w2_dy);

		// Row start per lane: base + dx * lane
		__m256i w0_row = _mm256_add_epi32(
			_mm256_set1_epi32(pTriHeader->w0_dx * simdStartX + pTriHeader->w0_dy * minY + pTriHeader->w0_offset),
			_mm256_mullo_epi32(_mm256_set1_epi32(pTriHeader->w0_dx), lane_offsets_i));

		__m256i w1_row = _mm256_add_epi32(
			_mm256_set1_epi32(pTriHeader->w1_dx * simdStartX + pTriHeader->w1_dy * minY + pTriHeader->w1_offset),
			_mm256_mullo_epi32(_mm256_set1_epi32(pTriHeader->w1_dx), lane_offsets_i));

		__m256i w2_row = _mm256_add_epi32(
			_mm256_set1_epi32(pTriHeader->w2_dx * simdStartX + pTriHeader->w2_dy * minY + pTriHeader->w2_offset),
			_mm256_mullo_epi32(_mm256_set1_epi32(pTriHeader->w2_dx), lane_offsets_i));

		// Stride step: dx * 8 (one step per 8-wide SIMD group)
		const __m256i w0_stride = _mm256_set1_epi32(pTriHeader->w0_dx * 8);
		const __m256i w1_stride = _mm256_set1_epi32(pTriHeader->w1_dx * 8);
		const __m256i w2_stride = _mm256_set1_epi32(pTriHeader->w2_dx * 8);

		// Z at corner and delta
		__m256 z_dx = _mm256_set1_ps(pTriHeader->z_dx);
		__m256 z_dy = _mm256_set1_ps(pTriHeader->z_dy);

		const __m256 z_offset = _mm256_set1_ps(pTriHeader->z_offset);

		__m256 z_row = _mm256_fmadd_ps(
			z_dx,
			x_offsets,
			_mm256_fmadd_ps(z_dy, y_offsets, z_offset));

		z_dx = _mm256_mul_ps(z_dx, stride);

		// UV at corner and delta
		maybe<UseTexture, __m256> u_dx, u_dy, u_row, v_dx, v_dy, v_row;

		if constexpr (UseTexture)
		{
			const rdTileTriangleUVs* pUVS = &decoded[triNum].uvs;

			u_dx = _mm256_set1_ps(pUVS->u_dx);
			u_dy = _mm256_set1_ps(pUVS->u_dy);

			const __m256 u_offset = _mm256_set1_ps(pUVS->u_offset);

			u_row = _mm256_fmadd_ps(
				u_dx,
				x_offsets,
				_mm256_fmadd_ps(u_dy, y_offsets, u_offset));

			u_dx = _mm256_mul_ps(u_dx, stride);

			v_dx = _mm256_set1_ps(pUVS->v_dx);
			v_dy = _mm256_set1_ps(pUVS->v_dy);

			const __m256 v_offset = _mm256_set1_ps(pUVS->v_offset);

			v_row = _mm256_fmadd_ps(
				v_dx,
				x_offsets,
				_mm256_fmadd_ps(v_dy, y_offsets, v_offset));

			v_dx = _mm256_mul_ps(v_dx, stride);
		}

		// Light at corner and delta
		maybe<UseGouraud, __m256> l_dx, l_dy, l_row;

		if constexpr (UseGouraud)
		{
			const rdTileTriangleLights* pLights = &decoded[triNum].lights;

			l_dx = _mm256_set1_ps(pLights->l_dx);
			l_dy = _mm256_set1_ps(pLights->l_dy);

			const __m256 l_offset = _mm256_set1_ps(pLights->l_offset);

			l_row = _mm256_fmadd_ps(
				l_dx,
				x_offsets,
				_mm256_fmadd_ps(l_dy, y_offsets, l_offset));

			l_dx = _mm256_mul_ps(l_dx, stride);
		}

		for (int y = minY; y <= maxY; y++)
		{
			// Current barycentric weights
			__m256i w0 = w0_row;
			__m256i w1 = w1_row;
			__m256i w2 = w2_row;

			// Z at row start
			__m256 z = z_row;

			// UV at row start
			maybe<UseTexture, __m256> u, v;

			if constexpr (UseTexture)
			{
				u = u_row;
				v = v_row;
			}

			// Light at row start
			maybe<UseGouraud, __m256> l;

			if constexpr (UseGouraud)
				l = l_row;

			// Tile coordinate at row start
			int offset = y * RDCACHE_FINE_TILE_SIZE + tileOffset;

			for (int x = simdStartX; x <= simdEndX; x += 8)
			{
				// Barycentric test: sign bit clear on all three weights means inside
				// OR the three values per lane, if any sign bit is set, that lane is outside
				const __m256i sign_or = _mm256_or_si256(_mm256_or_si256(w0, w1), w2);

				// Broadcast sign bit to all 32 bits: 0xFFFFFFFF if negative (outside), 0 if inside
				const __m256i coverageMask = _mm256_andnot_si256(
					_mm256_srai_epi32(sign_or, 31),
					_mm256_set1_epi32(-1));

				if (_mm256_movemask_ps(_mm256_castsi256_ps(coverageMask)))
				{
					int dither = 0;

					// Rcp division approx
					__m256 iz = _mm256_rcp_ps(z);

					iz = _mm256_mul_ps(
						iz,
						_mm256_sub_ps(_mm256_set1_ps(2.0f), _mm256_mul_ps(z, iz)));

					const __m256 zif = _mm256_fmadd_ps(
						iz,
						_mm256_set1_ps(65535.0f),
						_mm256_set1_ps(0.5f));

					const __m256i zi = _mm256_cvtps_epi32(zif);

					// Read depth buffer values if needed
					__m128i depth_u16 = _mm_loadu_si128((__m128i*) & rdRaster_TileDepth[offset]);
					__m256i depth_vals = _mm256_cvtepu16_epi32(depth_u16);

					__m256i depthMask = _mm256_set1_epi32(0xFFFFFFFF);

					// Early Z (try to avoid texture accesses when mask fails, not sure how helpful it is)
					if constexpr (!UseDiscard && ReadZ)
						depthMask = _mm256_andnot_si256(
							_mm256_cmpgt_epi32(zi, depth_vals),
							_mm256_set1_epi32(-1));

					// Texture/Color
					__m256i index;

					if constexpr (UseSolidColor)
					{
						index = solidColor;
					}
					else if constexpr (UseTexture)
					{
						__m256 uz = u;
						__m256 vz = v;

						if constexpr (Perspective)
						{
							uz = _mm256_mul_ps(uz, iz);
							vz = _mm256_mul_ps(vz, iz);
						}

						// Fixed point (int)(u * fixedScale)
						const __m256 scaled_uz = _mm256_mul_ps(uz, uvFixedScale);
						const __m256 scaled_vz = _mm256_mul_ps(vz, uvFixedScale);

						__m256i x_fixed = _mm256_cvtps_epi32(scaled_uz);
						__m256i y_fixed = _mm256_cvtps_epi32(scaled_vz);

						// Center at 0x8000 for wrapping
						x_fixed = _mm256_add_epi32(x_fixed, uvCenter);
						y_fixed = _mm256_add_epi32(y_fixed, uvCenter);

						// Coordinate wrapping
						// x_wrapped = (x_fixed & uWrap) >> 16
						// y_wrapped = (y_fixed >> (16 - texRowShift)) & vWrap
						const __m256i x_wrapped = _mm256_srli_epi32(_mm256_and_si256(x_fixed, uWrap), 16);

						const __m256i y_wrapped = _mm256_and_si256(
							_mm256_srli_epi32(y_fixed, 16 - texRowShift),
							vWrap);

						const __m256i texcoords = _mm256_add_epi32(x_wrapped, y_wrapped);

						if constexpr (IsIndexedColor)
						{
							alignas(32) uint32_t tc[8];
							_mm256_store_si256((__m256i*)tc, texcoords);

							alignas(32) uint32_t texels[8];

							for (int lane = 0; lane < 8; lane++)
								texels[lane] = pixels[tc[lane]];

							if constexpr (!IsRGBAOutput)
							{
								index = _mm256_load_si256((__m256i*)texels);
							}
							else
							{
								alignas(32) uint32_t rgbs[8];

								for (int lane = 0; lane < 8; ++lane)
								{
									rdColor24 rgb = palette[texels[lane]];

									rgbs[lane] = stdColor_EncodeRGBA(
										&rdRaster_32bitMode,
										rgb.r,
										rgb.g,
										rgb.b,
										texels[lane] > 0 ? 255 : 0);
								}

								index = _mm256_load_si256((__m256i*)rgbs);
							}
						}
						else
						{
							__m256i pix = _mm256_i32gather_epi32(
								(const int*)pixels,
								texcoords,
								sizeof(TextureStorage));

							__m256i r = _mm256_slli_epi32(
								_mm256_and_si256(
									_mm256_srli_epi32(pix, r_shift),
									r_mask),
								r_loss);

							__m256i g = _mm256_slli_epi32(
								_mm256_and_si256(
									_mm256_srli_epi32(pix, g_shift),
									g_mask),
								g_loss);

							__m256i b = _mm256_slli_epi32(
								_mm256_and_si256(
									_mm256_srli_epi32(pix, b_shift),
									b_mask),
								b_loss);

							__m256i a = _mm256_slli_epi32(
								_mm256_and_si256(
									_mm256_srli_epi32(pix, a_shift),
									a_mask),
								a_loss);

							if constexpr (!IsRGBAOutput)
							{
								r = _mm256_srli_epi32(r, 2);
								g = _mm256_srli_epi32(g, 2);
								b = _mm256_srli_epi32(b, 2);

								__m256i lutOffset = _mm256_or_si256(
									_mm256_or_si256(
										_mm256_slli_epi32(b, 12),
										_mm256_slli_epi32(g, 6)),
									r);

								alignas(32) uint32_t offsets[8];
								_mm256_store_si256((__m256i*)offsets, lutOffset);

								alignas(32) uint32_t out[8];

								for (int lane = 0; lane < 8; ++lane)
									out[lane] = searchTable[offsets[lane]];

								index = _mm256_load_si256((__m256i*)out);
							}
							else
							{
								index = stdColor_EncodeRGBASIMD_AVX2(
									&rdRaster_32bitMode,
									r,
									g,
									b,
									a);
							}
						}
					}

					// Alpha test
					__m256i discardMask = _mm256_setzero_si256();

					if constexpr (UseDiscard)
					{
						if constexpr (!IsRGBAOutput)
						{
							discardMask = _mm256_cmpeq_epi32(index, _mm256_setzero_si256());
						}
						else
						{
							discardMask = _mm256_cmpeq_epi32(index, _mm256_setzero_si256());
						}
					}

					// Late Z
					if constexpr (UseDiscard && ReadZ)
						depthMask = _mm256_andnot_si256(
							_mm256_cmpgt_epi32(zi, depth_vals),
							_mm256_set1_epi32(-1));

					// Merge the masks and determine what we keep from src and from dst
					__m256i srcMask = _mm256_andnot_si256(
						discardMask,
						_mm256_and_si256(coverageMask, depthMask));

					__m256i dstMask = _mm256_xor_si256(srcMask, _mm256_set1_epi32(-1));

					// Depth write
					if constexpr (WriteZ)
					{
						// Blend new and old Z values
						__m256i new_depth = _mm256_or_si256(
							_mm256_and_si256(srcMask, zi),
							_mm256_and_si256(dstMask, depth_vals));

						// Pack down to 16-bit and write
						__m128i lo = _mm256_castsi256_si128(new_depth);
						__m128i hi = _mm256_extracti128_si256(new_depth, 1);

						__m128i packed_depth = _mm_packus_epi32(lo, hi);

						_mm_storeu_si128((__m128i*) & rdRaster_TileDepth[offset], packed_depth);
					}

					if constexpr (!IsRGBAOutput)
					{
						alignas(32) uint32_t index32[8];
						_mm256_store_si256((__m256i*)index32, index);

						alignas(32) uint8_t oldIndex_arr[8];
						memcpy(oldIndex_arr, &rdRaster_TileColor[offset], 8);

#ifndef FOG
						if constexpr (UseGouraud || UseFlatLight || UseAlpha)
#endif
						{
							alignas(32) uint8_t index_arr[8];

							for (int i = 0; i < 8; ++i)
								index_arr[i] = (uint8_t)index32[i];

							alignas(32) uint32_t laneMasks[8];
							_mm256_store_si256((__m256i*)laneMasks, srcMask);

							flex_maybe<UseGouraud> l_arr[8];

							if constexpr (UseGouraud)
								_mm256_store_ps(l_arr, l);

							for (int lane = 0; lane < 8; lane++)
							{
								if constexpr (UseGouraud)
									index_arr[lane] = laneMasks[lane]
									? lightLevels[(stdMath_ClampInt((uint32_t)l_arr[lane] + dither, 0, 63) << 8) + index_arr[lane]]
									: 0;

								if constexpr (UseFlatLight)
									index_arr[lane] = laneMasks[lane]
									? lightLevels[(stdMath_ClampInt(pCommand->flatLight + dither, 0, 63) << 8) + index_arr[lane]]
									: 0;

								if constexpr (UseAlpha)
									index_arr[lane] = laneMasks[lane]
									? transparency[(oldIndex_arr[lane] << 8) + index_arr[lane]]
									: 0;
							}

							for (int i = 0; i < 8; ++i)
								index32[i] = index_arr[i];

							index = _mm256_load_si256((__m256i*)index32);
						}

						alignas(32) uint32_t src[8];
						alignas(32) uint32_t dst[8];

						_mm256_store_si256((__m256i*)src, index);
						_mm256_store_si256((__m256i*)dst, dstMask);

						for (int i = 0; i < 8; ++i)
						{
							if (dst[i])
								src[i] = oldIndex_arr[i];
						}

						alignas(32) uint8_t out[8];

						for (int i = 0; i < 8; ++i)
							out[i] = (uint8_t)src[i];

						memcpy(&rdRaster_TileColor[offset], out, 8);
					}
					else
					{
						// Load 8 pixels starting at offset
						__m256i oldPixels = _mm256_loadu_si256(
							(__m256i*)(rdRaster_TileColorRGBA + offset));

						// Lighting and blending
						if constexpr (UseGouraud || UseFlatLight || UseAlpha)
						{
							// Extract color channels
							__m256i r, g, b, a;

							stdColor_DecodeRGBASIMD_AVX2(
								index,
								&rdRaster_32bitMode,
								&r,
								&g,
								&b,
								&a);

							// Convert to float
							__m256 rf = _mm256_cvtepi32_ps(r);
							__m256 gf = _mm256_cvtepi32_ps(g);
							__m256 bf = _mm256_cvtepi32_ps(b);

							if constexpr (UseGouraud)
							{
								rf = _mm256_mul_ps(rf, l);
								gf = _mm256_mul_ps(gf, l);
								bf = _mm256_mul_ps(bf, l);
							}

							if constexpr (UseFlatLight)
							{
								rf = _mm256_mul_ps(rf, flatLight);
								gf = _mm256_mul_ps(gf, flatLight);
								bf = _mm256_mul_ps(bf, flatLight);
							}

							if constexpr (UseAlpha)
							{
								// Extract color channels from old color
								__m256i oldr, oldg, oldb;

								stdColor_DecodeRGBSIMD_AVX2(
									oldPixels,
									&rdRaster_32bitMode,
									&oldr,
									&oldg,
									&oldb);

								// Convert to float
								__m256 oldrf = _mm256_cvtepi32_ps(oldr);
								__m256 oldgf = _mm256_cvtepi32_ps(oldg);
								__m256 oldbf = _mm256_cvtepi32_ps(oldb);

								rf = _mm256_fmadd_ps(rf, alpha_ps, _mm256_mul_ps(oldrf, one_minus_alpha));
								gf = _mm256_fmadd_ps(gf, alpha_ps, _mm256_mul_ps(oldgf, one_minus_alpha));
								bf = _mm256_fmadd_ps(bf, alpha_ps, _mm256_mul_ps(oldbf, one_minus_alpha));
							}

							// Back to int
							r = _mm256_cvtps_epi32(rf);
							g = _mm256_cvtps_epi32(gf);
							b = _mm256_cvtps_epi32(bf);

							// Pack it back
							index = stdColor_EncodeRGBASIMD_AVX2(
								&rdRaster_32bitMode,
								r,
								g,
								b,
								a);
						}

						// Mask pixels
						// blended = (srcMask & index) | (dstMask & oldPixels)
						__m256i masked = _mm256_or_si256(
							_mm256_and_si256(srcMask, index),
							_mm256_and_si256(dstMask, oldPixels));

						// Store the masked pixels back
						_mm256_storeu_si256(
							(__m256i*)(rdRaster_TileColorRGBA + offset),
							masked);
					}
				}

				// Column step
				w0 = _mm256_add_epi32(w0, w0_stride);
				w1 = _mm256_add_epi32(w1, w1_stride);
				w2 = _mm256_add_epi32(w2, w2_stride);

				z = _mm256_add_ps(z, z_dx);

				if constexpr (UseTexture)
				{
					u = _mm256_add_ps(u, u_dx);
					v = _mm256_add_ps(v, v_dx);
				}

				if constexpr (UseGouraud)
					l = _mm256_add_ps(l, l_dx);

				offset += 8;
			}

			// Row step
			w0_row = _mm256_add_epi32(w0_row, w0_dy_i);
			w1_row = _mm256_add_epi32(w1_row, w1_dy_i);
			w2_row = _mm256_add_epi32(w2_row, w2_dy_i);

			z_row = _mm256_add_ps(z_row, z_dy);

			if constexpr (UseTexture)
			{
				u_row = _mm256_add_ps(u_row, u_dy);
				v_row = _mm256_add_ps(v_row, v_dy);
			}

			if constexpr (UseGouraud)
				l_row = _mm256_add_ps(l_row, l_dy);
		}
	}

	_mm256_zeroupper();
}
#elif TARGET_SSE
// Templated using constexpr branches to avoid having to duplicate a crap ton of code
template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage, int8_t LightMode, bool UseDiscard, bool UseAlpha>
void rdRaster_DrawToTileSIMD(/*rdTilePrimitive* prim,*/rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	// Setup a few compile time options
	static constexpr bool ReadZ = ZMethod == RD_ZBUFFER_READ_NOWRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool WriteZ = ZMethod == RD_ZBUFFER_NOREAD_WRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool UseFlatLight = (LightMode == RD_LIGHTMODE_NOTLIT) || (LightMode == RD_LIGHTMODE_DIFFUSE);
	static constexpr bool UseGouraud = LightMode == RD_LIGHTMODE_GOURAUD;
	static constexpr bool UseSolidColor = GeoMode == RD_GEOMODE_SOLIDCOLOR;
	static constexpr bool UseTexture = GeoMode == RD_GEOMODE_TEXTURED && TextureMode >= 0;
	static constexpr bool Perspective = TextureMode == RD_TEXTUREMODE_PERSPECTIVE;
	static constexpr bool IsIndexedColor = std::is_same_v<TextureStorage, uint8_t>;
	static constexpr bool IsRGBAOutput = (ColorMode != STDCOLOR_PAL);

	//uint16_t iz_min = (uint16_t)(int)(entry->z_min * 65536.0f + 0.5f);
	//if(iz_min >= rdRaster_TileHiZ)
	//	return;
	int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
	int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;
	int tileMaxX = tileMinX + RDCACHE_FINE_TILE_SIZE - 1;
	int tileMaxY = tileMinY + RDCACHE_FINE_TILE_SIZE - 1;

	rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
	stdVBuffer* pVBuffer = pCanvas->vbuffer;

	uint_maybe<IsRGBAOutput> canvas_r_mask, canvas_g_mask, canvas_b_mask;
	if constexpr (IsRGBAOutput)
	{
		canvas_r_mask = (int8_t)((1u << pVBuffer->format.format.r_bits) - 1);
		canvas_g_mask = (int8_t)((1u << pVBuffer->format.format.g_bits) - 1);
		canvas_b_mask = (int8_t)((1u << pVBuffer->format.format.b_bits) - 1);
	}

	// todo: I assume this gets compiled out for permutations that don't use them?
	rdColor24* palette = pCommand->pHeader->pColormap->colors;
	uint8_t* transparency = ((uint8_t*)pCommand->pHeader->pColormap->transparency);
	uint8_t* lightLevels = ((uint8_t*)pCommand->pHeader->pColormap->lightlevel);
	uint8_t* searchTable = pCommand->pHeader->pColormap->searchTable;

	rdRaster_PrimitiveEncoderDecoder* pDecoder = pCommand->pDecoder;

	// SIMD constants
	const __m128 stride = _mm_set1_ps(4.0f);
	const __m128 indices = _mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f);
	const __m128 uvFixedScale =_mm_set1_ps(rdRaster_fixedScale);
	const __m128i uvCenter = _mm_set1_epi32(0x8000);
	const float alpha = 90.0f / 255.0f;
	const __m128 alpha_ps = _mm_set1_ps(alpha);
	const __m128 one_minus_alpha = _mm_set1_ps(1.0f - alpha);

	maybe<!UseTexture, __m128i> solidColor;
	if constexpr (!UseTexture)
	{
		if constexpr (IsRGBAOutput)
		{
			rdColor24 rgb24 = palette[pCommand->solidColor & 0xFF];
			solidColor = _mm_set1_epi32(stdColor_EncodeRGBA(&rdRaster_32bitMode, rgb24.r, rgb24.g, rgb24.b, pCommand->solidColor > 0 ? 255 : 0));
		}
		else
		{
			solidColor = _mm_cvtsi32_si128(0x01010101u * (pCommand->solidColor & 0xFF)); // store 8 bit color using a little binary trick
		}
	}

	uint_maybe<UseTexture> texRowShift;
	maybe<UseTexture, TextureStorage*> pixels;
	maybe<UseTexture, __m128i> uWrap, vWrap;
	if constexpr (UseTexture)
	{
		texRowShift = pCommand->pTexData->texRowShift;
		pixels = (TextureStorage*)pCommand->pTexData->pixels;
		uWrap = _mm_set1_epi32(pCommand->pTexData->uWrap);
		vWrap = _mm_set1_epi32(pCommand->pTexData->vWrap);
	}

	maybe<!IsIndexedColor, __m128i> r_mask, g_mask, b_mask, a_mask;
	uint_maybe<!IsIndexedColor> r_shift, g_shift, b_shift, a_shift;
	uint_maybe<!IsIndexedColor> r_loss, g_loss, b_loss, a_loss;
	if constexpr (!IsIndexedColor)
	{
		r_mask = _mm_set1_epi32(pCommand->pFormat->r_mask);
		g_mask = _mm_set1_epi32(pCommand->pFormat->g_mask);
		b_mask = _mm_set1_epi32(pCommand->pFormat->b_mask);
		a_mask = _mm_set1_epi32(pCommand->pFormat->a_mask);

		r_shift = pCommand->pFormat->r_shift;
		g_shift = pCommand->pFormat->g_shift;
		b_shift = pCommand->pFormat->b_shift;
		a_shift = pCommand->pFormat->a_shift;

		r_loss = pCommand->pFormat->r_loss;
		g_loss = pCommand->pFormat->g_loss;
		b_loss = pCommand->pFormat->b_loss;
		a_loss = pCommand->pFormat->a_loss;
	}

	maybe<UseFlatLight, __m128> flatLight;
	if constexpr (UseFlatLight)
		flatLight = _mm_set1_ps(pCommand->flatLight / 255.0f);

	// Pre-decode all triangles from the stream into a contiguous local array so that
	// the hot rasterization loop operates on cache-friendly sequential data, rather than
	// pointer-chasing through the encoded byte stream
	struct rdPreDecodedTri {
		rdTileTriangleHeader hdr;
		maybe<UseTexture, rdTileTriangleUVs>    uvs;
		maybe<UseGouraud, rdTileTriangleLights> lights;
	};
	constexpr int kMaxTris = 30;
	rdPreDecodedTri decoded[kMaxTris];
	int numDecoded = 0;
	{
		uint32_t triIdx = *pDecoder->Advance<const uint32_t>();
		while ((triIdx != 0x80007FFF) && (triIdx < pCommand->pHeader->numTris) && (numDecoded < kMaxTris))
		{
			auto& t = decoded[numDecoded++];
			t.hdr = *pDecoder->Advance<const rdTileTriangleHeader>();
			if constexpr (UseTexture)
				t.uvs = *pDecoder->Advance<const rdTileTriangleUVs>();
			if constexpr (UseGouraud)
				t.lights = *pDecoder->Advance<const rdTileTriangleLights>();
			triIdx = *pDecoder->Advance<const uint32_t>();
		}
	}

	// Rasterize using the pre-decoded, cache-friendly triangle array
	for (int triNum = 0; triNum < numDecoded; triNum++)
	{
		const rdTileTriangleHeader* pTriHeader = &decoded[triNum].hdr;

		int minX = std::min(std::max(pTriHeader->minX, tileMinX), tileMaxX);
		int maxX = std::min(std::max(pTriHeader->maxX, tileMinX), tileMaxX);
		int minY = std::min(std::max(pTriHeader->minY, tileMinY), tileMaxY);
		int maxY = std::min(std::max(pTriHeader->maxY, tileMinY), tileMaxY);

		int tileLocalMinX = minX - tileMinX;
		int tileLocalMaxX = maxX - tileMinX;

		// Align for 4 pixel processing
		int simdStartX = tileMinX + (tileLocalMinX & ~0x3);
		int simdEndX = tileMinX + (tileLocalMaxX & ~0x3);
		simdEndX = std::min(simdEndX, tileMaxX - 3);

		int tileOffsetX = simdStartX - tileMinX;
		int tileOffset = tileOffsetX - tileMinY * RDCACHE_FINE_TILE_SIZE;
		
		const __m128 x_offsets = _mm_add_ps(_mm_set1_ps((flex_t)simdStartX), indices);
		const __m128 y_offsets = _mm_set1_ps((flex_t)minY);

		// Edge function stepping
		const __m128i lane_offsets_i = _mm_setr_epi32(0, 1, 2, 3);

		const __m128i w0_dy_i = _mm_set1_epi32(pTriHeader->w0_dy);
		const __m128i w1_dy_i = _mm_set1_epi32(pTriHeader->w1_dy);
		const __m128i w2_dy_i = _mm_set1_epi32(pTriHeader->w2_dy);

		// Row start per lane: base + dx * lane
		__m128i w0_row = _mm_add_epi32(
			_mm_set1_epi32(pTriHeader->w0_dx * simdStartX + pTriHeader->w0_dy * minY + pTriHeader->w0_offset),
			_mm_mullo_epi32(_mm_set1_epi32(pTriHeader->w0_dx), lane_offsets_i));
		__m128i w1_row = _mm_add_epi32(
			_mm_set1_epi32(pTriHeader->w1_dx * simdStartX + pTriHeader->w1_dy * minY + pTriHeader->w1_offset),
			_mm_mullo_epi32(_mm_set1_epi32(pTriHeader->w1_dx), lane_offsets_i));
		__m128i w2_row = _mm_add_epi32(
			_mm_set1_epi32(pTriHeader->w2_dx * simdStartX + pTriHeader->w2_dy * minY + pTriHeader->w2_offset),
			_mm_mullo_epi32(_mm_set1_epi32(pTriHeader->w2_dx), lane_offsets_i));

		// Stride step: dx * 4 (one step per 4-wide SIMD group)
		const __m128i w0_stride = _mm_set1_epi32(pTriHeader->w0_dx * 4);
		const __m128i w1_stride = _mm_set1_epi32(pTriHeader->w1_dx * 4);
		const __m128i w2_stride = _mm_set1_epi32(pTriHeader->w2_dx * 4);

		// Z at corner and delta
		__m128 z_dx = _mm_set1_ps(pTriHeader->z_dx);
		__m128 z_dy = _mm_set1_ps(pTriHeader->z_dy);
		const __m128 z_offset = _mm_set1_ps(pTriHeader->z_offset);
		__m128 z_row = _mm_fmadd_ps(z_dx, x_offsets, _mm_fmadd_ps(z_dy, y_offsets, z_offset));
		z_dx = _mm_mul_ps(z_dx, stride);

		// UV at corner and delta
		maybe<UseTexture, __m128> u_dx, u_dy, u_row, v_dx, v_dy, v_row;
		if constexpr (UseTexture)
		{
			const rdTileTriangleUVs* pUVS = &decoded[triNum].uvs;

			u_dx = _mm_set1_ps(pUVS->u_dx);
			u_dy = _mm_set1_ps(pUVS->u_dy);
			const __m128 u_offset = _mm_set1_ps(pUVS->u_offset);
			u_row = _mm_fmadd_ps(u_dx, x_offsets, _mm_fmadd_ps(u_dy, y_offsets, u_offset));
			u_dx = _mm_mul_ps(u_dx, stride);

			v_dx = _mm_set1_ps(pUVS->v_dx);
			v_dy = _mm_set1_ps(pUVS->v_dy);
			const __m128 v_offset = _mm_set1_ps(pUVS->v_offset);
			v_row = _mm_fmadd_ps(v_dx, x_offsets, _mm_fmadd_ps(v_dy, y_offsets, v_offset));
			v_dx = _mm_mul_ps(v_dx, stride);
		}

		// Light at corner and delta
		maybe<UseGouraud, __m128> l_dx, l_dy, l_row;
		if constexpr (UseGouraud)
		{
			const rdTileTriangleLights* pLights = &decoded[triNum].lights;
			l_dx = _mm_set1_ps(pLights->l_dx);
			l_dy = _mm_set1_ps(pLights->l_dy);
			const __m128 l_offset = _mm_set1_ps(pLights->l_offset);
			l_row = _mm_fmadd_ps(l_dx, x_offsets, _mm_fmadd_ps(l_dy, y_offsets, l_offset));
			l_dx = _mm_mul_ps(l_dx, stride);
		}

		for (int y = minY; y <= maxY; y++)
		{
			// Current barycentric weights
			__m128i w0 = w0_row;
			__m128i w1 = w1_row;
			__m128i w2 = w2_row;

			// Z at row start
			__m128 z = z_row;

			// UV at row start
			maybe<UseTexture, __m128> u, v;
			if constexpr (UseTexture)
			{
				u = u_row;
				v = v_row;
			}

			// Light at row start
			maybe<UseGouraud, __m128> l;
			if constexpr (UseGouraud)
				l = l_row;

			// Tile coordinate at row start
			int offset = y * RDCACHE_FINE_TILE_SIZE + tileOffset;
			for (int x = simdStartX; x <= simdEndX; x += 4)
			{
				// Barycentric test: sign bit clear on all three weights means inside
				// OR the three values per lane, if any sign bit is set, that lane is outside
				const __m128i sign_or = _mm_or_si128(_mm_or_si128(w0, w1), w2);

				// Broadcast sign bit to all 32 bits: 0xFFFFFFFF if negative (outside), 0 if inside
				const __m128i coverageMask = _mm_andnot_si128(_mm_srai_epi32(sign_or, 31), _mm_set1_epi32(-1));
				if (_mm_movemask_ps(_mm_castsi128_ps(coverageMask)))
				{
					int dither = 0;//rdRaster_GetDither(x, y);
					//rdVector2 texDither0 = rdRaster_GetTexDither(x, y);
					//rdVector2 texDither1 = rdRaster_GetTexDither(x+1, y);
					//rdVector2 texDither2 = rdRaster_GetTexDither(x+2, y);
					//rdVector2 texDither3 = rdRaster_GetTexDither(x+3, y);
					//__m128 texDitherX = _mm_set_ps(texDither3.x, texDither2.x, texDither1.x, texDither0.x);
					//__m128 texDitherY = _mm_set_ps(texDither3.y, texDither2.y, texDither1.y, texDither0.y);

					// Rcp division approx
					__m128 iz = _mm_rcp_ps(z);
					iz = _mm_mul_ps(iz, _mm_sub_ps(_mm_set1_ps(2.0f), _mm_mul_ps(z, iz))); // 1 NR iteration
					const __m128 zif = _mm_fmadd_ps(iz, _mm_set1_ps(65535.0f), _mm_set1_ps(0.5f));
					const __m128i zi = _mm_cvtps_epi32(zif); // float->int

					// Read depth buffer values if needed
					__m128i depth_vals = _mm_setzero_si128();
					__m128i depthMask = _mm_set1_epi32(0xFFFFFFFF);
					if constexpr (ReadZ || WriteZ)
					{
						__m128i depth_u16 = _mm_loadl_epi64((__m128i*)&rdRaster_TileDepth[offset]);
						depth_vals = _mm_unpacklo_epi16(depth_u16, _mm_setzero_si128());
					}

					// Early Z (try to avoid texture accesses when mask fails, not sure how helpful it is)
					if constexpr (!UseDiscard && ReadZ)
						depthMask = _mm_andnot_si128(_mm_cmpgt_epi32(zi, depth_vals), _mm_set1_epi32(-1)); // don't have a lessequal, use greater and flip

					// Texture/Color
					__m128i index;
					if constexpr (UseSolidColor)
					{
						index = solidColor;
					}
					else if constexpr (UseTexture)
					{
						__m128 uz = u;
						__m128 vz = v;
						if constexpr (Perspective)
						{
							uz = _mm_mul_ps(uz, iz);
							vz = _mm_mul_ps(vz, iz);
						}
						//uz = _mm_add_ps(uz, texDitherX);
						//vz = _mm_add_ps(vz, texDitherY);

						// Fixed point (int)(u * fixedScale)
						const __m128 scaled_uz = _mm_mul_ps(uz, uvFixedScale);
						const __m128 scaled_vz = _mm_mul_ps(vz, uvFixedScale);
						__m128i x_fixed = _mm_cvtps_epi32(scaled_uz);
						__m128i y_fixed = _mm_cvtps_epi32(scaled_vz);

						// Center at 0x8000 for wrapping
						x_fixed = _mm_add_epi32(x_fixed, uvCenter);
						y_fixed = _mm_add_epi32(y_fixed, uvCenter);

						// Coordinate wrapping
						// x_wrapped = (x_fixed & uWrap) >> 16
						// y_wrapped = (y_fixed >> (16 - texRowShift)) & vWrap
						const __m128i x_wrapped = _mm_srli_epi32(_mm_and_si128(x_fixed, uWrap), 16);
						const __m128i y_wrapped = _mm_and_si128(_mm_srli_epi32(y_fixed, 16 - texRowShift), vWrap);
						const __m128i texcoords = _mm_add_epi32(x_wrapped, y_wrapped);

						// extract the depth and texcoord for each lane
						uint32_t depthMasks[4];
						_mm_storeu_si128((__m128i*)depthMasks, depthMask);

						uint32_t tcs[4];
						_mm_storeu_si128((__m128i*)tcs, texcoords);

						// Read the texture for each texcoord
						alignas(16) TextureStorage texels[4];
						for (int lane = 0; lane < 4; lane++)
							texels[lane] = depthMasks[lane] ? pixels[tcs[lane]] : 0;

						if constexpr (IsIndexedColor) // Direct index lookup
						{
							if constexpr (!IsRGBAOutput)
							{
								index = _mm_loadu_si128((__m128i*)texels);
							}
							else
							{
								uint32_t rgbs[4];
								for (int lane = 0; lane < 4; ++lane)
								{
									rdColor24 rgb = palette[texels[lane]];
									rgbs[lane] = stdColor_EncodeRGBA(&rdRaster_32bitMode, rgb.r, rgb.g, rgb.b, texels[lane] > 0 ? 255 : 0);
								}
								index = _mm_set_epi32(rgbs[3], rgbs[2], rgbs[1], rgbs[0]);
							}
						}
						else // RGB->index conversion
						{
							// SIMD texels so we can convert them all at the same time
							__m128i pix = _mm_set_epi32(texels[3], texels[2], texels[1], texels[0]);

							// Extract channels
							__m128i r = _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(pix, r_shift), r_mask), r_loss);
							__m128i g = _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(pix, g_shift), g_mask), g_loss);
							__m128i b = _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(pix, b_shift), b_mask), b_loss);
							__m128i a = _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(pix, a_shift), a_mask), a_loss);

							if constexpr (!IsRGBAOutput)
							{
								// Convert to 6 bit (intermediate 8 bit) for the search LUT
								r = _mm_srli_epi32(r, 2);
								g = _mm_srli_epi32(g, 2);
								b = _mm_srli_epi32(b, 2);

								// offset = (b << 12) | (g << 6) | r
								__m128i offset = _mm_or_si128(_mm_or_si128(_mm_slli_epi32(b, 12), _mm_slli_epi32(g, 6)), r);

								// Extract the offsets and read the lUT
								alignas(16) uint32_t offsets[4];
								_mm_store_si128((__m128i*)offsets, offset);

								alignas(16) uint8_t indices_arr[4];
								for (int lane = 0; lane < 4; ++lane)
									indices_arr[lane] = depthMasks[lane] ? searchTable[offsets[lane]] : 0;

								// Set back to SIMD for some testing (discard)
								index = _mm_set_epi8(
									0, 0, 0, 0, 0, 0, 0, 0,
									0, 0, 0, 0,
									indices_arr[3], indices_arr[2], indices_arr[1], indices_arr[0]
								);
							}
							else
							{
								index = stdColor_EncodeRGBASIMD(&rdRaster_32bitMode, r, g, b, a);
							}
						}
					}

					// Alpha test
					__m128i discardMask = _mm_setzero_si128();
					if constexpr (UseDiscard)
					{
						if constexpr (!IsRGBAOutput)
						{
							// discard where index == 0
							// todo: actual transparency index
							__m128i discardMask8 = _mm_cmpeq_epi8(index, _mm_setzero_si128());
							discardMask = _mm_cvtepi8_epi32(discardMask8);
						}
						else
						{
							discardMask = _mm_cmpeq_epi32(index, _mm_setzero_si128());
						}
					}

					// Late Z
					if constexpr (UseDiscard && ReadZ)
						depthMask = _mm_andnot_si128(_mm_cmpgt_epi32(zi, depth_vals), _mm_set1_epi32(-1)); // don't have a lessequal, use greater and flip

					// Merge the masks and determine what we keep from src and from dst
					__m128i srcMask = _mm_andnot_si128(discardMask, _mm_and_si128(coverageMask, depthMask));
					__m128i dstMask = _mm_xor_si128(srcMask, _mm_set1_epi32(-1));

					// Depth write
					if constexpr (WriteZ)
					{
						// Blend new and old Z values
						__m128i new_depth = _mm_or_si128(
							_mm_and_si128(srcMask, zi),
							_mm_and_si128(dstMask, depth_vals)
						);

						// Pack down to 16-bit and write
						__m128i packed_depth = _mm_packus_epi32(new_depth, new_depth);
						_mm_storel_epi64((__m128i*)&rdRaster_TileDepth[offset], packed_depth);
					}

					if constexpr (!IsRGBAOutput)
					{					
						// Read the previous index/color
						alignas(16) uint8_t oldIndex_arr[4];
						memcpy(oldIndex_arr, &rdRaster_TileColor[offset], 4);

						// Extract the indices and masks so we can do light/transparency lookups
					#ifndef FOG // todo: UseFog
						if constexpr (UseGouraud || UseFlatLight || UseAlpha)
					#endif
						{
							alignas(16) uint8_t index_arr[4];
							_mm_storeu_si32((__m128i*)index_arr, index);

							alignas(16) uint32_t laneMasks[4];
							_mm_storeu_si128((__m128i*)laneMasks, srcMask);

							flex_maybe<UseGouraud> l_arr[4];
							if constexpr (UseGouraud)
								_mm_storeu_ps(l_arr, l);

						#ifdef FOG
							__m128 fog = _mm_fmadd_ps(iz, _mm_set_ps1(rdRaster_fogScale), _mm_set_ps1(rdRaster_fogOffset));
							__m128i fogi = _mm_cvtps_epi32(fog);

							alignas(16) uint32_t fog_arr[4];
							_mm_storeu_si128((__m128i*)fog_arr, fogi);
						#endif

							for (int lane = 0; lane < 4; lane++)
							{
								if constexpr (UseGouraud)
									index_arr[lane] = laneMasks[lane] ? lightLevels[(stdMath_ClampInt((uint32_t)l_arr[lane] + dither, 0, 63) << 8) + index_arr[lane]] : 0;
						
								if constexpr (UseFlatLight)
									index_arr[lane] = laneMasks[lane] ? lightLevels[(stdMath_ClampInt(pCommand->flatLight + dither, 0, 63) << 8) + index_arr[lane]] : 0;
						
							#ifdef FOG
								// tiletodo: optimize me
								if (rdroid_curFogEnabled)
									index_arr[lane] = laneMasks[lane] ? rdroid_fogTable[(stdMath_ClampInt(fog_arr[lane] + dither, 0, 63) << 8) + index_arr[lane]] : index_arr[lane];
							#endif


								if constexpr (UseAlpha)
									index_arr[lane] = laneMasks[lane] ? transparency[(oldIndex_arr[lane] << 8) + index_arr[lane]] : 0;
							}

							// Pack back into SIMD
							index = _mm_loadl_epi64((__m128i*)index_arr);
						}

						// Load oldIndex as SIMD for masked write
						__m128i oldIndexVec = _mm_cvtepu8_epi32(_mm_loadl_epi64((__m128i*)oldIndex_arr));

						// Merge using masks
						__m128i masked = _mm_or_si128(
							_mm_and_si128(srcMask, _mm_cvtepu8_epi32(index)),
							_mm_and_si128(dstMask, oldIndexVec)
						);

						// Pack blended back to 8-bit and store
						__m128i packed16 = _mm_packus_epi32(masked, _mm_setzero_si128());
						__m128i packed8 = _mm_packus_epi16(packed16, _mm_setzero_si128());
						_mm_storeu_si32((__m128i*)&rdRaster_TileColor[offset], packed8);
					}
					else
					{
						// Load 4 pixels starting at offset
						__m128i oldPixels = _mm_loadu_si128((__m128i*)(rdRaster_TileColorRGBA + offset));

						// Lighting and blending
						if constexpr (UseGouraud || UseFlatLight || UseAlpha)
						{
							// Extract color channels
							__m128i r, g, b, a;
							stdColor_DecodeRGBASIMD(index, &rdRaster_32bitMode, &r, &g, &b, &a);

							// Convert to float
							__m128 rf = _mm_cvtepi32_ps(r);
							__m128 gf = _mm_cvtepi32_ps(g);
							__m128 bf = _mm_cvtepi32_ps(b);

							if constexpr (UseGouraud)
							{
								rf = _mm_mul_ps(rf, l);
								gf = _mm_mul_ps(gf, l);
								bf = _mm_mul_ps(bf, l);
							}
							
							if constexpr (UseFlatLight)
							{
								rf = _mm_mul_ps(rf, flatLight);
								gf = _mm_mul_ps(gf, flatLight);
								bf = _mm_mul_ps(bf, flatLight);
							}

							if constexpr (UseAlpha)
							{
								// Extract color channels from old color
								__m128i oldr, oldg, oldb;
								stdColor_DecodeRGBSIMD(oldPixels, &rdRaster_32bitMode, &oldr, &oldg, &oldb);

								// Convert to float
								__m128 oldrf = _mm_cvtepi32_ps(oldr);
								__m128 oldgf = _mm_cvtepi32_ps(oldg);
								__m128 oldbf = _mm_cvtepi32_ps(oldb);

								rf = _mm_fmadd_ps(rf, alpha_ps, _mm_mul_ps(oldrf, one_minus_alpha));
								gf = _mm_fmadd_ps(gf, alpha_ps, _mm_mul_ps(oldgf, one_minus_alpha));
								bf = _mm_fmadd_ps(bf, alpha_ps, _mm_mul_ps(oldbf, one_minus_alpha));
							}

							// Back to int
							r = _mm_cvtps_epi32(rf);
							g = _mm_cvtps_epi32(gf);
							b = _mm_cvtps_epi32(bf);

							// Pack it back
							index = stdColor_EncodeRGBASIMD(&rdRaster_32bitMode, r, g, b, a);
						}

						// Mask pixels
						// blended = (srcMask & index) | (dstMask & oldPixels)
						__m128i masked = _mm_or_si128(
							_mm_and_si128(srcMask, index),
							_mm_and_si128(dstMask, oldPixels)
						);

						// Store the masked pixels back
						_mm_storeu_si128((__m128i*)(rdRaster_TileColorRGBA + offset), masked);

					}
				}

				// Column step
				w0 = _mm_add_epi32(w0, w0_stride);
				w1 = _mm_add_epi32(w1, w1_stride);
				w2 = _mm_add_epi32(w2, w2_stride);

				z = _mm_add_ps(z, z_dx);

				if constexpr (UseTexture)
				{
					u = _mm_add_ps(u, u_dx);
					v = _mm_add_ps(v, v_dx);
				}

				if constexpr (UseGouraud)
					l = _mm_add_ps(l, l_dx);

				offset += 4;
			}

			// Row step
			w0_row = _mm_add_epi32(w0_row, w0_dy_i);
			w1_row = _mm_add_epi32(w1_row, w1_dy_i);
			w2_row = _mm_add_epi32(w2_row, w2_dy_i);

			z_row = _mm_add_ps(z_row, z_dy);

			if constexpr (UseTexture)
			{
				u_row = _mm_add_ps(u_row, u_dy);
				v_row = _mm_add_ps(v_row, v_dy);
			}

		if constexpr (UseGouraud)
			l_row = _mm_add_ps(l_row, l_dy);
		}
	}
}
#endif

// Templated using constexpr branches to avoid having to duplicate a crap ton of code
template <int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage, int8_t LightMode, bool UseDiscard, bool UseAlpha>
void rdRaster_DrawToTile(/*rdTilePrimitive* prim,*/rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	// Setup a few compile time options
	static constexpr bool ReadZ = ZMethod == RD_ZBUFFER_READ_NOWRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool WriteZ = ZMethod == RD_ZBUFFER_NOREAD_WRITE || ZMethod == RD_ZBUFFER_READ_WRITE;
	static constexpr bool UseFlatLight = (LightMode == RD_LIGHTMODE_NOTLIT) || (LightMode == RD_LIGHTMODE_DIFFUSE);
	static constexpr bool UseGouraud = LightMode == RD_LIGHTMODE_GOURAUD;
	static constexpr bool UseSolidColor = GeoMode == RD_GEOMODE_SOLIDCOLOR;
	static constexpr bool UseTexture = GeoMode == RD_GEOMODE_TEXTURED && TextureMode >= 0;
	static constexpr bool Perspective = TextureMode == RD_TEXTUREMODE_PERSPECTIVE;
	static constexpr bool IsIndexedColor = std::is_same_v<TextureStorage, uint8_t>;

	//uint16_t iz_min = (uint16_t)(int)(entry->z_min * 65536.0f + 0.5f);
	//if(iz_min >= rdRaster_TileHiZ)
	//	return;

	int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
	int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;
	int tileMaxX = tileMinX + RDCACHE_FINE_TILE_SIZE - 1;
	int tileMaxY = tileMinY + RDCACHE_FINE_TILE_SIZE - 1;

	// todo: I assume this gets compiled out for permutations that don't use them?
	uint8_t* transparency = ((uint8_t*)pCommand->pHeader->pColormap->transparency);
	uint8_t* lightLevels = ((uint8_t*)pCommand->pHeader->pColormap->lightlevel);
	
	rdRaster_PrimitiveEncoderDecoder* pDecoder = pCommand->pDecoder;

	uint32_t triIdx = *pDecoder->Advance<const uint32_t>();
	while((triIdx != 0x80007FFF) && (triIdx < pCommand->pHeader->numTris))
	{
		const rdTileTriangleHeader* pTriHeader = pDecoder->Advance<const rdTileTriangleHeader>();

		int minX = std::min(std::max(pTriHeader->minX, tileMinX), tileMaxX);
		int maxX = std::min(std::max(pTriHeader->maxX, tileMinX), tileMaxX);
		int minY = std::min(std::max(pTriHeader->minY, tileMinY), tileMaxY);
		int maxY = std::min(std::max(pTriHeader->maxY, tileMinY), tileMaxY);

		// Tile offset along X (for tile cache access)
		int tileOffsetX = (minX - tileMinX);

		// Initial edge values at top-left (pure integer arithmetic, no fixed_t overhead)
		int32_t w0_row = pTriHeader->w0_dx * minX + (pTriHeader->w0_dy * minY + pTriHeader->w0_offset);
		int32_t w1_row = pTriHeader->w1_dx * minX + (pTriHeader->w1_dy * minY + pTriHeader->w1_offset);
		int32_t w2_row = pTriHeader->w2_dx * minX + (pTriHeader->w2_dy * minY + pTriHeader->w2_offset);

		// Initial z values at top-left
		flex_t z_row = pTriHeader->z_dx * minX + (pTriHeader->z_dy * minY + pTriHeader->z_offset);

		// UV step per pixel
		maybe<UseTexture, rdTileTriangleUVs*> pUVS;
		flex_maybe<UseTexture> u_row, v_row;
		if constexpr (UseTexture)
		{
			pUVS = pDecoder->Advance<const rdTileTriangleUVs>();
			u_row = pUVS->u_dx * minX + (pUVS->u_dy * minY + pUVS->u_offset);
			v_row = pUVS->v_dx * minX + (pUVS->v_dy * minY + pUVS->v_offset);
		}

		// Light step per pixel
		maybe<UseGouraud, rdTileTriangleLights*> pLights;
		flex_maybe<UseGouraud> l_row;
		if constexpr (UseGouraud)
		{
			pLights = pDecoder->Advance<const rdTileTriangleLights>();
			l_row = pLights->l_dx * minX + (pLights->l_dy * minY + pLights->l_offset);
		}

		for (int y = minY; y <= maxY; y++)
		{
			// Current barycentric weights
			int32_t w0 = w0_row;
			int32_t w1 = w1_row;
			int32_t w2 = w2_row;

			// Z at row start
			flex_t z = z_row;

			// UV at row start
			flex_maybe<UseTexture> u, v;
			if constexpr (UseTexture)
			{
				u = u_row;
				v = v_row;
			}

			// Light at row start
			flex_maybe<UseGouraud> l;
			if constexpr (UseGouraud)
				l = l_row;

			// Tile coordinate at row start
			int offset = (y - tileMinY) * RDCACHE_FINE_TILE_SIZE + tileOffsetX;
			for (int x = minX; x <= maxX; x++)
			{
				// Barycentric test: all edge values must be non-negative (top-left rule already applied)
				if ((w0 | w1 | w2) >= 0)
				{
					flex_t iz = stdMath_Rcp(z);

					// Integer depth for depth testing
					uint16_t zi = (uint16_t)(int)(fma(iz, 65535.0f, 0.5f));
					uint16_t depthMask = 0xFFFF;

					// Early Z
					if constexpr (!UseDiscard && ReadZ)
						depthMask = (zi <= rdRaster_TileDepth[offset]) ? 0xFFFF : 0;

					// Texture/Color
					uint8_t index = 0;
					if constexpr (UseSolidColor)
						index = (uint8_t)(pCommand->solidColor & 0xFF);

					if constexpr (UseTexture)
					{
						flex_t uz = u;
						flex_t vz = v;
						if constexpr (Perspective)
						{
							uz *= iz;
							vz *= iz;
						}

						// Moved to fixed point centered at 0x8000 for wrapping
						// todo: would be nice to use fixed point up front but it doesn't work atm...
						int32_t x_fixed = (int32_t)(rdRaster_fixedScale * uz) + 0x8000;
						int32_t y_fixed = (int32_t)(rdRaster_fixedScale * vz) + 0x8000;

						// Coordinate wrapping
						int32_t x_wrapped = (x_fixed & pCommand->pTexData->uWrap) >> 16;
						int32_t y_wrapped = (y_fixed >> (16 - pCommand->pTexData->texRowShift)) & pCommand->pTexData->vWrap;

						// Read the texture
						// todo: texcache?
						if constexpr (IsIndexedColor) // direct index lookup
						{
							index = depthMask ? ((TextureStorage*)pCommand->pTexData->pixels)[x_wrapped + y_wrapped] : 0;
						}
						else // RGB->index conversion
						{
							TextureStorage pix = depthMask ? ((TextureStorage*)pCommand->pTexData->pixels)[x_wrapped + y_wrapped] : 0;

							uint32_t r = ((((pix >> pCommand->pFormat->r_shift) & pCommand->pFormat->r_mask) << pCommand->pFormat->r_loss) >> 2);// + dither;
							uint32_t g = ((((pix >> pCommand->pFormat->g_shift) & pCommand->pFormat->g_mask) << pCommand->pFormat->g_loss) >> 2);// + dither;
							uint32_t b = ((((pix >> pCommand->pFormat->b_shift) & pCommand->pFormat->b_mask) << pCommand->pFormat->b_loss) >> 2);// + dither;
							uint32_t offset = (b << 12) | (g << 6) | r;
							index = depthMask ? pCommand->pHeader->pColormap->searchTable[offset] : 0;
						}
					}

					// Alpha test
					uint16_t discardMask = 0;
					if constexpr (UseDiscard)
						discardMask = (index == 0) ? 0xFFFF : 0;

					// Late Z
					if constexpr (UseDiscard && ReadZ)
						depthMask = (zi <= rdRaster_TileDepth[offset]) ? 0xFFFF : 0;

					const uint16_t write_mask = depthMask & ~discardMask;
					const uint16_t read_mask = ~write_mask;

					if constexpr (WriteZ)
					{
						uint16_t oldZ = rdRaster_TileDepth[offset];
						rdRaster_TileDepth[offset] = (write_mask & zi) | (read_mask & oldZ);
						//rdRaster_TileHiZ = zi < rdRaster_TileHiZ ? zi : rdRaster_TileHiZ;
					}

					#ifdef FOG
					// tiletodo: optimize me
					if constexpr (IsIndexedColor)
					{
						int fogIndex = (int)(stdMath_Saturate((iz - rdroid_curFogStartDepth) * rdroid_curFogRange) * 64.0f);
						index = write_mask ? rdroid_fogTable[index * 64 + fogIndex] : index;
					}
					#endif

					// Lighting
					if constexpr (UseGouraud)
						index = write_mask ? lightLevels[stdMath_ClampInt(((uint32_t)l + dither, 0, 63) << 8) + index] : 0;

					if constexpr (UseFlatLight)
						index = write_mask ? lightLevels[(stdMath_ClampInt(pCommand->flatLight + dither, 0, 63) << 8) + index] : 0;

					// Blending
					uint8_t oldIndex = rdRaster_TileColor[offset];
					if constexpr (UseAlpha)
						index = write_mask ? transparency[(oldIndex << 8) + index] : 0;

					rdRaster_TileColor[offset] = (write_mask & index) | (read_mask & oldIndex);
				}

				// column step
				w0 += pTriHeader->w0_dx;
				w1 += pTriHeader->w1_dx;
				w2 += pTriHeader->w2_dx;

				z += pTriHeader->z_dx;

				if constexpr (UseTexture)
				{
					u += pUVS->u_dx;
					v += pUVS->v_dx;
				}

				if constexpr (UseGouraud)
					l += pLights->l_dx;

				++offset;
			}

			// row step
			w0_row += pTriHeader->w0_dy;
			w1_row += pTriHeader->w1_dy;
			w2_row += pTriHeader->w2_dy;

			z_row += pTriHeader->z_dy;

			if constexpr (UseTexture)
			{
				u_row += pUVS->u_dy;
				v_row += pUVS->v_dy;
			}

			if constexpr (UseGouraud)
				l_row += pLights->l_dy;
		}

		triIdx = *pDecoder->Advance<const uint32_t>();
	}
}

// Nested dispatches to choose a compile time permutation

template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage, int8_t LightMode, bool UseDiscard>
void rdRaster_DrawToTileByAlpha(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
#ifdef TARGET_AVX2
	bool alpha = pCommand->pHeader->blend;//(face->type & 2) != 0;
	if (alpha)
		rdRaster_DrawToTileSIMD_AVX2<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, UseDiscard, true>(pCommand, ppStream, tileX, tileY);
	else
		rdRaster_DrawToTileSIMD_AVX2<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, UseDiscard, false>(pCommand, ppStream, tileX, tileY);
#else
	bool alpha = pCommand->pHeader->blend;//(face->type & 2) != 0;
	if (alpha)
		rdRaster_DrawToTileSIMD<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, UseDiscard, true>(pCommand, ppStream, tileX, tileY);
	else
		rdRaster_DrawToTileSIMD<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, UseDiscard, false>(pCommand, ppStream, tileX, tileY);
#endif
}

template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage, int8_t LightMode>
void rdRaster_DrawToTileByDiscard(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	if (TextureMode < 0)
	{
		rdRaster_DrawToTileByAlpha<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, false>(pCommand, ppStream, tileX, tileY);
	}
	else
	{
		bool discard = pCommand->pHeader->discard;//(face->texinfo->texture_ptr->alpha_en & 1) != 0;
		if (discard)
			rdRaster_DrawToTileByAlpha<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, true>(pCommand, ppStream, tileX, tileY);
		else
			rdRaster_DrawToTileByAlpha<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, LightMode, false>(pCommand, ppStream, tileX, tileY);
	}
}

template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode, typename TextureStorage>
void rdRaster_DrawToTileByLightMode(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	switch (pCommand->pHeader->lightMode)
	{
	case RD_LIGHTMODE_FULLYLIT:
		rdRaster_DrawToTileByDiscard<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, RD_LIGHTMODE_FULLYLIT>(pCommand, ppStream, tileX, tileY);
		break;
	case RD_LIGHTMODE_NOTLIT:
		//stdPlatform_Printf("Fetching flat light\n");
		pCommand->flatLight = *pCommand->pDecoder->Advance<const uint32_t>();
		rdRaster_DrawToTileByDiscard<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, RD_LIGHTMODE_NOTLIT>(pCommand, ppStream, tileX, tileY);
		break;
	case RD_LIGHTMODE_DIFFUSE:
		pCommand->flatLight = *pCommand->pDecoder->Advance<const uint32_t>();
		rdRaster_DrawToTileByDiscard<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, RD_LIGHTMODE_DIFFUSE>(pCommand, ppStream, tileX, tileY);
		break;
	case RD_LIGHTMODE_GOURAUD:
		rdRaster_DrawToTileByDiscard<ColorMode, ZMethod, GeoMode, TextureMode, TextureStorage, RD_LIGHTMODE_GOURAUD>(pCommand, ppStream, tileX, tileY);
		break;
	default:
		break;
	}
}

template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode, int8_t TextureMode>
void rdRaster_DrawToTileByTextureBPP(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	int bpp = pCommand->pHeader->colorDepth;// face->bpp;
	switch (bpp)
	{
	case 1:
		rdRaster_DrawToTileByLightMode<ColorMode, ZMethod, GeoMode, TextureMode, uint8_t>(pCommand, ppStream, tileX, tileY);
		break;
	case 2:
		pCommand->pFormat = pCommand->pDecoder->Advance<const rdTilePrimitiveFmt>();
		rdRaster_DrawToTileByLightMode<ColorMode, ZMethod, GeoMode, TextureMode, uint16_t>(pCommand, ppStream, tileX, tileY);
		break;
	case 3:
		pCommand->pFormat = pCommand->pDecoder->Advance<const rdTilePrimitiveFmt>();
		rdRaster_DrawToTileByLightMode<ColorMode, ZMethod, GeoMode, TextureMode, uint32_t>(pCommand, ppStream, tileX, tileY);
		break;
	default:
		break;
	}
}
template <int8_t ColorMode, int8_t ZMethod, int8_t GeoMode>
void rdRaster_DrawToTileByTextureMode(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	int textureMode = pCommand->pHeader->texMode;
	switch (textureMode)
	{
	case RD_TEXTUREMODE_AFFINE:
		rdRaster_DrawToTileByTextureBPP<ColorMode, ZMethod, GeoMode, RD_TEXTUREMODE_AFFINE>(pCommand, ppStream, tileX, tileY);
		break;
	case RD_TEXTUREMODE_PERSPECTIVE:
		rdRaster_DrawToTileByTextureBPP<ColorMode, ZMethod, GeoMode, RD_TEXTUREMODE_PERSPECTIVE>(pCommand, ppStream, tileX, tileY);
		break;
	default:
		break;
	}
}

template <int8_t ColorMode, int8_t ZMethod>
void rdRaster_DrawToTileByGeoMode(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	int geometryMode = pCommand->pHeader->geoMode;
	switch (geometryMode)
	{
	case RD_GEOMODE_SOLIDCOLOR:
		pCommand->solidColor = *pCommand->pDecoder->Advance<const uint32_t>();
		rdRaster_DrawToTileByLightMode<ColorMode, ZMethod, RD_GEOMODE_SOLIDCOLOR, -1, uint8_t>(pCommand, ppStream, tileX, tileY);
		break;
	case RD_GEOMODE_TEXTURED:
		pCommand->pTexData = pCommand->pDecoder->Advance<const rdTilePrimitiveTex>();
		rdRaster_DrawToTileByTextureMode<ColorMode, ZMethod, RD_GEOMODE_TEXTURED>(pCommand, ppStream, tileX, tileY);
		break;
	default:
		break;
	}
}

template <int8_t ColorMode>
void rdRaster_DrawToTileByZMethod(rdTileDrawCommand* pCommand, uint8_t** ppStream, int tileX, int tileY)
{
	if (rdroid_curOcclusionMethod) // jk edge table method, just do full z
	{
		rdRaster_DrawToTileByGeoMode<ColorMode, RD_ZBUFFER_READ_WRITE>(pCommand, ppStream, tileX, tileY);
	}
	else
	{
		switch (rdroid_curZBufferMethod)
		{
		case RD_ZBUFFER_NOREAD_NOWRITE:
			rdRaster_DrawToTileByGeoMode<ColorMode, RD_ZBUFFER_NOREAD_NOWRITE>(pCommand, ppStream, tileX, tileY);
			break;
		case RD_ZBUFFER_NOREAD_WRITE:
			rdRaster_DrawToTileByGeoMode<ColorMode, RD_ZBUFFER_NOREAD_WRITE>(pCommand, ppStream, tileX, tileY);
			break;
		case RD_ZBUFFER_READ_WRITE:
			rdRaster_DrawToTileByGeoMode<ColorMode, RD_ZBUFFER_READ_WRITE>(pCommand, ppStream, tileX, tileY);
			break;
		case RD_ZBUFFER_READ_NOWRITE:
			rdRaster_DrawToTileByGeoMode<ColorMode, RD_ZBUFFER_READ_NOWRITE>(pCommand, ppStream, tileX, tileY);
			break;
		default:
			break;
		}
	}
}

extern "C" {

	void rdRaster_ClearBins()
	{
		rdRaster_numTilePrimitives = 0;


		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		memset(rdCamera_pCurCamera->canvas->coarseTileBits, 0, sizeof(uint32_t) * pCanvas->coarseTileCount * RDCACHE_TILE_BINNING_STRIDE);
		memset(rdCamera_pCurCamera->canvas->tileBits, 0, sizeof(uint32_t) * pCanvas->tileCount * RDCACHE_TILE_BINNING_STRIDE);
	}

	void rdRaster_StartBinning()
	{
		rdRaster_ClearBins();

		if (rdroid_curAcceleration)
			rdRaster_pTileStreamStart = std3D_LockRenderList();
		else
			rdRaster_pTileStreamStart = &rdRaster_aTilePrimitiveStream[0];

		rdRaster_pTileStream = rdRaster_pTileStreamStart;
		rdRaster_ppTileStream = &rdRaster_pTileStream;
	}

	void rdRaster_EndBinning()
	{
		if (rdroid_curAcceleration)
			std3D_UnlockRenderList();
	}

	// Coarse tile binning
	// TILETODO: consider sorting?
	void rdRaster_BinFaceCoarse(rdProcEntry* face)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		uint32_t* coarseTileBits = pCanvas->coarseTileBits;
		int coarseTileWidth = pCanvas->coarseTileWidth;

		if (!face || face->geometryMode == 0 || !face->material)
			return;
		
		int coarseMinX = stdMath_ClampInt((int32_t)(face->x_min / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileWidth - 1);
		int coarseMaxX = stdMath_ClampInt((int32_t)(face->x_max / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileWidth - 1);
		int coarseMinY = stdMath_ClampInt((int32_t)(face->y_min / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileHeight - 1);
		int coarseMaxY = stdMath_ClampInt((int32_t)(face->y_max / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileHeight - 1);

		int index = rdRaster_numTilePrimitives;
		int numPrims = rdRaster_SetupTiled(face, rdRaster_ppTileStream);
		if (!numPrims)
			return;

		for (int cy = coarseMinY; cy <= coarseMaxY; cy++)
		{
			for (int cx = coarseMinX; cx <= coarseMaxX; cx++)
			{
				const int linear_tile = cy * coarseTileWidth + cx;
				const int binned_bitmask_offset = (RDCACHE_TILE_BINNING_STRIDE * linear_tile);

				const uint32_t bin_index = index / 32;
				const uint32_t bin_place = index % 32;

				coarseTileBits[binned_bitmask_offset + bin_index] |= (1u << bin_place);
			}
		}
	}

#ifdef JOB_SYSTEM
	// written once before dispatch, then read-only during jobs
	static rdProcEntry* rdRaster_pCoarseBinFaces = NULL;
	static int         rdRaster_numCoarseBinFaces = 0;

	// Coarse tile binning job, one job per face
	//
	// Thread-safety notes:
	//   - Each job owns an exclusive stream region (pTileStreamStart + jobIndex * RD_FACE_STREAM_SLOT_SIZE)
	//     so stream writes never alias
	//   - rdRaster_aTilePrimitiveOffsets[jobIndex] is unique per job, no aliasing
	//   - coarseTileBits words are shared when faces in the same 32-face group overlap the same
			//     coarse tile, so we use atomic OR to set bits atomically
	void rdRaster_BinFaceCoarseJob(uint32_t jobIndex, uint32_t groupIndex)
	{
		if (jobIndex >= (uint32_t)rdRaster_numCoarseBinFaces)
			return;

		rdProcEntry* face = &rdRaster_pCoarseBinFaces[jobIndex];
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;

		if (!face || face->geometryMode == 0 || !face->material)
			return;

		// Point at this face's exclusive stream slot
		uint8_t* pStream = rdRaster_pTileStreamStart + jobIndex * RD_FACE_STREAM_SLOT_SIZE;

		const int coarseMinX = stdMath_ClampInt((int32_t)(face->x_min / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileWidth  - 1);
		const int coarseMaxX = stdMath_ClampInt((int32_t)(face->x_max / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileWidth  - 1);
		const int coarseMinY = stdMath_ClampInt((int32_t)(face->y_min / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileHeight - 1);
		const int coarseMaxY = stdMath_ClampInt((int32_t)(face->y_max / RDCACHE_COARSE_TILE_SIZE), 0, pCanvas->coarseTileHeight - 1);

		// Setup into the exclusive slot; explicitSlot bypasses the global counter
		const int numPrims = rdRaster_SetupTiled(face, &pStream, (int)jobIndex);
		if (!numPrims)
			return;

		const uint32_t bin_index = (uint32_t)jobIndex / 32;
		const long     bin_bit   = (long)(1u << ((uint32_t)jobIndex % 32));

		for (int cy = coarseMinY; cy <= coarseMaxY; cy++)
		{
			for (int cx = coarseMinX; cx <= coarseMaxX; cx++)
			{
				const int linear_tile = cy * pCanvas->coarseTileWidth + cx;
				volatile long* pWord = (volatile long*)&pCanvas->coarseTileBits[RDCACHE_TILE_BINNING_STRIDE * linear_tile + bin_index];
				
				// Atomic OR: multiple faces can share the same 32-face bitmask word
#ifdef _WIN32
				_InterlockedOr(pWord, bin_bit);
#else
				__sync_or_and_fetch(pWord, bin_bit);
#endif
			}
		}
	}
#endif

	void rdRaster_BinFacesCoarse(rdProcEntry* faces, int numFaces)
	{
#ifdef JOB_SYSTEM
		rdRaster_pCoarseBinFaces   = faces;
		rdRaster_numCoarseBinFaces = numFaces;

		// Pre-set the counter so rdRaster_BinFaces / rdRaster_FlushBins see the right total
		// even though individual jobs write their own slots without touching it
		rdRaster_numTilePrimitives = numFaces;
		stdJob_Dispatch(numFaces, 1, rdRaster_BinFaceCoarseJob);
		stdJob_Wait();
#else
		for (int i = 0; i < numFaces; ++i)
			rdRaster_BinFaceCoarse(&faces[i]);
#endif
	}

	// Coarse tile (cx,cy) exclusively owns fine tiles at
	// (cx * FINE_PER_COARSE + fx, cy * FINE_PER_COARSE + fy), so tileBits writes
	// never alias across jobs and no synchronization is required
#ifdef JOB_SYSTEM
	void rdRaster_BinFacesJob(uint32_t jobIndex, uint32_t groupIndex)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		if (jobIndex >= (uint32_t)pCanvas->coarseTileCount)
			return;

		uint32_t* coarseMask = &pCanvas->coarseTileBits[jobIndex * RDCACHE_TILE_BINNING_STRIDE];
		const int coarseTileX = (int)jobIndex % pCanvas->coarseTileWidth;
		const int coarseTileY = (int)jobIndex / pCanvas->coarseTileWidth;

		const int coarseX0 = coarseTileX * RDCACHE_COARSE_TILE_SIZE;
		const int coarseY0 = coarseTileY * RDCACHE_COARSE_TILE_SIZE;
		const int coarseX1 = coarseX0 + RDCACHE_COARSE_TILE_SIZE - 1;
		const int coarseY1 = coarseY0 + RDCACHE_COARSE_TILE_SIZE - 1;

		for (int triWord = 0; triWord < RDCACHE_TILE_BINNING_STRIDE; triWord++)
		{
			uint32_t maskWord = coarseMask[triWord];
			while (maskWord != 0)
			{
				const int bitPos   = stdMath_FindLSB(maskWord);
				const int triIndex = triWord * 32 + bitPos;
				maskWord ^= (1u << bitPos);

				rdTilePrimitiveHeader* tri = (rdTilePrimitiveHeader*)(rdRaster_pTileStreamStart + rdRaster_aTilePrimitiveOffsets[triIndex]);

				// Clip primitive bounds to coarse tile
				const int triMinX = tri->minX > coarseX0 ? tri->minX : coarseX0;
				const int triMaxX = tri->maxX < coarseX1 ? tri->maxX : coarseX1;
				const int triMinY = tri->minY > coarseY0 ? tri->minY : coarseY0;
				const int triMaxY = tri->maxY < coarseY1 ? tri->maxY : coarseY1;

				const int fineMinX = (triMinX - coarseX0) / RDCACHE_FINE_TILE_SIZE;
				const int fineMaxX = (triMaxX - coarseX0) / RDCACHE_FINE_TILE_SIZE;
				const int fineMinY = (triMinY - coarseY0) / RDCACHE_FINE_TILE_SIZE;
				const int fineMaxY = (triMaxY - coarseY0) / RDCACHE_FINE_TILE_SIZE;

				const uint32_t bin_index = (uint32_t)triIndex / 32;
				const uint32_t bin_place = (uint32_t)triIndex % 32;
				const uint32_t bin_bit   = 1u << bin_place;

				for (int fy = fineMinY; fy <= fineMaxY; fy++)
				{
					for (int fx = fineMinX; fx <= fineMaxX; fx++)
					{
						int fineTileX = stdMath_ClampInt(coarseTileX * RDCACHE_FINE_PER_COARSE + fx, 0, pCanvas->tileWidth  - 1);
						int fineTileY = stdMath_ClampInt(coarseTileY * RDCACHE_FINE_PER_COARSE + fy, 0, pCanvas->tileHeight - 1);
						int fineTileIdx = fineTileY * pCanvas->tileWidth + fineTileX;
						pCanvas->tileBits[RDCACHE_TILE_BINNING_STRIDE * fineTileIdx + bin_index] |= bin_bit;
					}
				}
			}
		}
	}
#endif

	// Fine tile binning
	void rdRaster_BinFaces()
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
#ifdef JOB_SYSTEM
		stdJob_Dispatch(pCanvas->coarseTileCount, 1, rdRaster_BinFacesJob);
		stdJob_Wait();
#else
		for (int coarseTileIdx = 0; coarseTileIdx < pCanvas->coarseTileCount; ++coarseTileIdx)
		{
			uint32_t* coarseMask = &pCanvas->coarseTileBits[coarseTileIdx * RDCACHE_TILE_BINNING_STRIDE];
			int coarseTileX = coarseTileIdx % pCanvas->coarseTileWidth;
			int coarseTileY = coarseTileIdx / pCanvas->coarseTileWidth;

			int coarseX0 = coarseTileX * RDCACHE_COARSE_TILE_SIZE;
			int coarseY0 = coarseTileY * RDCACHE_COARSE_TILE_SIZE;
			int coarseX1 = coarseX0 + RDCACHE_COARSE_TILE_SIZE - 1;
			int coarseY1 = coarseY0 + RDCACHE_COARSE_TILE_SIZE - 1;

			for (int triWord = 0; triWord < RDCACHE_TILE_BINNING_STRIDE; triWord++)
			{
				uint32_t maskWord = coarseMask[triWord];
				while (maskWord != 0)
				{
					int bitPos = stdMath_FindLSB(maskWord);
					int triIndex = triWord * 32 + bitPos;
					maskWord ^= (1u << bitPos);

					// Read the header (we don't need any more information so we don't need to decode the whole stream)
					rdTilePrimitiveHeader* tri = (rdTilePrimitiveHeader*)(rdRaster_pTileStreamStart + rdRaster_aTilePrimitiveOffsets[triIndex]);

					// Clip triangle bounds to coarse tile bounds
					int triMinX = tri->minX > coarseX0 ? tri->minX : coarseX0;
					int triMaxX = tri->maxX < coarseX1 ? tri->maxX : coarseX1;
					int triMinY = tri->minY > coarseY0 ? tri->minY : coarseY0;
					int triMaxY = tri->maxY < coarseY1 ? tri->maxY : coarseY1;

					// Convert to fine tile local coordinates
					int fineMinX = (triMinX - coarseX0) / RDCACHE_FINE_TILE_SIZE;
					int fineMaxX = (triMaxX - coarseX0) / RDCACHE_FINE_TILE_SIZE;
					int fineMinY = (triMinY - coarseY0) / RDCACHE_FINE_TILE_SIZE;
					int fineMaxY = (triMaxY - coarseY0) / RDCACHE_FINE_TILE_SIZE;

					// Bin triangle into fine tiles
					for (int fy = fineMinY; fy <= fineMaxY; fy++)
					{
						for (int fx = fineMinX; fx <= fineMaxX; fx++)
						{
							int fineTileX = coarseTileX * RDCACHE_FINE_PER_COARSE + fx;
							int fineTileY = coarseTileY * RDCACHE_FINE_PER_COARSE + fy;
							fineTileX = stdMath_ClampInt(fineTileX, 0, pCanvas->tileWidth - 1);
							fineTileY = stdMath_ClampInt(fineTileY, 0, pCanvas->tileHeight - 1);
							int fineTileIdx = fineTileY * pCanvas->tileWidth + fineTileX;
							int binned_bitmask_offset = (RDCACHE_TILE_BINNING_STRIDE * fineTileIdx);

							const uint32_t bin_index = triIndex / 32;
							const uint32_t bin_place = triIndex % 32;

							pCanvas->tileBits[binned_bitmask_offset + bin_index] |= (1u << bin_place);

						}
					}
				}
			}
		}
#endif
	}

#ifdef TARGET_SSE
	// Copy framebuffer data to thread-local cache (improves cache access)
	void rdRaster_CopyToTileDepth(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;
		
		//rdRaster_TileHiZ = 0xFFFF;

		uint16_t* zBuffer = (uint16_t*)pCanvas->d3d_vbuf->surface_lock_alloc;

		// If the clear flag is set, just clear the tile depth outright
		if (pCanvas->bIdk & 4)
		{
			memset(rdRaster_TileDepth, 0xFFFF, sizeof(rdRaster_TileDepth));
		}
		else // Otherwise copy the depth buffer to the tile
		{
			// Copy depth buffer row-wise
			for (int row = 0; row < tileHeight; ++row)
			{
				const uint16_t* src = zBuffer + (tileMinY + row) * width + tileMinX;
				uint16_t* dst = (uint16_t*)rdRaster_TileDepth + row * maxW;

				memcpy(dst, src, tileWidth * sizeof(uint16_t));

				// Clear remaining columns if needed
				if (tileWidth < maxW)
					memset(dst + tileWidth, 0, (maxW - tileWidth) * sizeof(uint16_t));
			}

			// Clear remaining rows if needed
			for (int row = tileHeight; row < maxH; ++row)
				memset(rdRaster_TileDepth + row * maxW, 0, maxW * sizeof(uint16_t));
		}
	}


	void rdRaster_CopyToTileColor(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		uint8_t* colorBuffer = (uint8_t*)pCanvas->vbuffer->surface_lock_alloc;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		// Copy color buffer row-wise
		for (int row = 0; row < tileHeight; ++row)
		{
			const uint8_t* src = colorBuffer + (tileMinY + row) * width + tileMinX;
			uint8_t* dst = rdRaster_TileColor + row * maxW;

			memcpy(dst, src, tileWidth);

			if (tileWidth < maxW)
				memset(dst + tileWidth, 0, maxW - tileWidth);
		}

		// Clear remaining rows if needed
		for (int row = tileHeight; row < maxH; ++row)
			memset(rdRaster_TileColor + row * maxW, 0, maxW);
	}

	void rdRaster_CopyToTileColorRGBA(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		if (pCanvas->vbuffer->format.format.bpp == 16)
		{
			uint16_t* colorBuffer = (uint16_t*)pCanvas->vbuffer->surface_lock_alloc;
			rdTexformat* srcFormat = &pCanvas->vbuffer->format.format;
			rdTexformat* dstFormat = &rdRaster_32bitMode;

			for (int row = 0; row < tileHeight; ++row)
			{
				uint16_t* src = colorBuffer + (tileMinY + row) * width + tileMinX;
				uint32_t* dst = rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE;

				int col = 0;
				for (; col <= tileWidth - 8; col += 8)
				{
					// Load 8 uint16_t pixels (128 bits)
					__m128i src16 = _mm_loadu_si128((__m128i*)(src + col));

					// Unpack lower 4 pixels to 32-bit lanes
					__m128i srcLo = _mm_unpacklo_epi16(src16, _mm_setzero_si128());

					// Unpack upper 4 pixels to 32-bit lanes
					__m128i srcHi = _mm_unpackhi_epi16(src16, _mm_setzero_si128());

					// Process lower 4 pixels
					__m128i recodedLo = stdColor_RecodeSIMD(srcLo, srcFormat, dstFormat);

					// Process upper 4 pixels
					__m128i recodedHi = stdColor_RecodeSIMD(srcHi, srcFormat, dstFormat);

					// Store 8 pixels back
					_mm_storeu_si128((__m128i*)(dst + col), recodedLo);
					_mm_storeu_si128((__m128i*)(dst + col + 4), recodedHi);
				}

				// Scalar fallback for remaining pixels
				for (; col < tileWidth; col++)
				{
					dst[col] = stdColor_Recode(src[col], srcFormat, dstFormat);
				}

				// Clear tail if needed
				if (tileWidth < RDCACHE_FINE_TILE_SIZE)
				{
					memset(dst + tileWidth, 0, (RDCACHE_FINE_TILE_SIZE - tileWidth) * sizeof(uint32_t));
				}
			}
		}

		// Clear remaining rows if needed
		for (int row = tileHeight; row < RDCACHE_FINE_TILE_SIZE; ++row)
		{
			memset(rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE, 0, sizeof(uint32_t) * RDCACHE_FINE_TILE_SIZE);
		}
	}

	// Copies the tile cache back to the framebuffer
	void rdRaster_CopyFromTileDepth(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;
	
		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		uint16_t* zBuffer = (uint16_t*)pCanvas->d3d_vbuf->surface_lock_alloc;

		// Copy row by row for zbuffer
		for (int row = 0; row < tileHeight; ++row)
		{
			uint16_t* dst = zBuffer + (tileMinY + row) * width + tileMinX;
			const uint16_t* src = (uint16_t*)rdRaster_TileDepth + row * maxW;

			memcpy(dst, src, tileWidth * sizeof(uint16_t));
		}
	}

	void rdRaster_CopyFromTileColor(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;
	
		uint8_t* colorBuffer = (uint8_t*)pCanvas->vbuffer->surface_lock_alloc;

		// Copy row by row for color buffer
		for (int row = 0; row < tileHeight; ++row)
		{
			uint8_t* dst = colorBuffer + (tileMinY + row) * width + tileMinX;
			const uint8_t* src = rdRaster_TileColor + row * maxW;
			memcpy(dst, src, tileWidth);
		}
	}

	void rdRaster_CopyFromTileColorRGBA(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		if (pCanvas->vbuffer->format.format.bpp == 16)
		{
			uint16_t* colorBuffer = (uint16_t*)pCanvas->vbuffer->surface_lock_alloc;
			rdTexformat* srcFormat = &rdRaster_32bitMode;
			rdTexformat* dstFormat = &pCanvas->vbuffer->format.format;

			for (int row = 0; row < tileHeight; ++row)
			{
				uint16_t* dst = colorBuffer + (tileMinY + row) * width + tileMinX;
				uint32_t* src = rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE;

				int col = 0;
				for (; col <= tileWidth - 4; col += 4)
				{
					//__m128i dither = _mm_setr_epi32(rdRaster_GetDither(row, col), rdRaster_GetDither(row, col+1), rdRaster_GetDither(row, col+2), rdRaster_GetDither(row, col+3));

					__m128i srcPixels = _mm_loadu_si128((__m128i*)(src + col));
					//srcPixels = _mm_add_epi32(srcPixels, dither);
					
					__m128i recoded = stdColor_RecodeSIMD(srcPixels, srcFormat, dstFormat);

					// Pack 4x32-bit unsigned to 4x16-bit unsigned with saturation (SSE4.1)
					__m128i low16 = _mm_packus_epi32(recoded, _mm_setzero_si128());

					// Store lower 64 bits (4 pixels x 16-bit)
					_mm_storel_epi64((__m128i*)(dst + col), low16);
				}

				// Scalar fallback for any remaining pixels
				for (; col < tileWidth; col++)
				{
					dst[col] = (uint16_t)stdColor_Recode(src[col], srcFormat, dstFormat);
				}
			}
		}
	}


#else

	void rdRaster_CopyToTileMem(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width_in_pixels = pCanvas->vbuffer->format.width;
		int height_in_pixels = pCanvas->vbuffer->format.height;
		uint8_t* colorBuffer = (uint8_t*)pCanvas->vbuffer->surface_lock_alloc;
		uint16_t* zBuffer = (uint16_t*)(pCanvas->d3d_vbuf->surface_lock_alloc);

		int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		// Check if tile start is outside framebuffer bounds
		if (tileMinX >= width_in_pixels || tileMinY >= height_in_pixels)
			return;

		int tileWidth = RDCACHE_FINE_TILE_SIZE;
		int tileHeight = RDCACHE_FINE_TILE_SIZE;

		// Clamp tile width and height to avoid reading outside
		tileWidth = width_in_pixels - tileMinX;
		if (tileWidth > RDCACHE_FINE_TILE_SIZE)
			tileWidth = RDCACHE_FINE_TILE_SIZE;

		tileHeight = height_in_pixels - tileMinY;
		if (tileHeight > RDCACHE_FINE_TILE_SIZE)
			tileHeight = RDCACHE_FINE_TILE_SIZE;

		if (tileWidth <= 0 || tileHeight <= 0)
			return;

		//rdRaster_TileHiZ = 0xFFFF;

		// Copy color buffer row-wise
		for (int row = 0; row < tileHeight; ++row)
		{
			uint8_t* src = colorBuffer + (tileMinY + row) * width_in_pixels + tileMinX;
			uint8_t* dst = rdRaster_TileColor + row * RDCACHE_FINE_TILE_SIZE;
			memcpy(dst, src, tileWidth);

			if (tileWidth < RDCACHE_FINE_TILE_SIZE)
				memset(dst + tileWidth, 0, RDCACHE_FINE_TILE_SIZE - tileWidth);
		}

		// Clear remaining rows if needed
		for (int row = tileHeight; row < RDCACHE_FINE_TILE_SIZE; ++row)
			memset(rdRaster_TileColor + row * RDCACHE_FINE_TILE_SIZE, 0, RDCACHE_FINE_TILE_SIZE);

		// If the clear flag is set, just clear the tile depth outright
		if (pCanvas->bIdk & 4)
		{
			memset(rdRaster_TileDepth, 0xFFFF, sizeof(rdRaster_TileDepth));
		}
		else
		{
			// Copy depth buffer row-wise
			for (int row = 0; row < tileHeight; ++row)
			{
				uint16_t* src = zBuffer + (tileMinY + row) * width_in_pixels + tileMinX;
				uint16_t* dst = (uint16_t*)(rdRaster_TileDepth)+row * RDCACHE_FINE_TILE_SIZE;
				memcpy(dst, src, tileWidth * sizeof(uint16_t));

				//for (int x = 0; x < tileWidth; ++x)
					//rdRaster_TileHiZ = src[x] < rdRaster_TileHiZ ? src[x] : rdRaster_TileHiZ;

				if (tileWidth < RDCACHE_FINE_TILE_SIZE)
					memset(dst + tileWidth, 0, (RDCACHE_FINE_TILE_SIZE - tileWidth) * sizeof(uint16_t));
			}


			// Clear remaining rows if needed
			for (int row = tileHeight; row < RDCACHE_FINE_TILE_SIZE; ++row)
				memset(rdRaster_TileDepth + row * RDCACHE_FINE_TILE_SIZE, 0, RDCACHE_FINE_TILE_SIZE * sizeof(uint16_t));
		}
	}

	void rdRaster_CopyFromTileMem(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width_in_pixels = pCanvas->vbuffer->format.width;
		int height_in_pixels = pCanvas->vbuffer->format.height;
		
		uint8_t* colorBuffer = (uint8_t*)pCanvas->vbuffer->surface_lock_alloc;
		uint16_t* zBuffer = (uint16_t*)(pCanvas->d3d_vbuf->surface_lock_alloc);

		int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		// Return early if tile start is outside the framebuffer
		if (tileMinX >= width_in_pixels || tileMinY >= height_in_pixels)
			return;

		int tileWidth = RDCACHE_FINE_TILE_SIZE;
		int tileHeight = RDCACHE_FINE_TILE_SIZE;

		tileWidth = width_in_pixels - tileMinX;
		if (tileWidth > RDCACHE_FINE_TILE_SIZE)
			tileWidth = RDCACHE_FINE_TILE_SIZE;
		else if (tileWidth < 0)
			tileWidth = 0;

		tileHeight = height_in_pixels - tileMinY;
		if (tileHeight > RDCACHE_FINE_TILE_SIZE)
			tileHeight = RDCACHE_FINE_TILE_SIZE;
		else if (tileHeight < 0)
			tileHeight = 0;

		// Copy row by row for color buffer
		for (int row = 0; row < tileHeight; ++row)
		{
			uint8_t* dst = colorBuffer + (tileMinY + row) * width_in_pixels + tileMinX;
			uint8_t* src = rdRaster_TileColor + row * RDCACHE_FINE_TILE_SIZE;
			memcpy(dst, src, tileWidth);
		}

		// Copy row by row for zbuffer
		for (int row = 0; row < tileHeight; ++row)
		{
			uint16_t* dst = zBuffer + (tileMinY + row) * width_in_pixels + tileMinX;
			uint16_t* src = (uint16_t*)rdRaster_TileDepth + row * RDCACHE_FINE_TILE_SIZE;
			memcpy(dst, src, tileWidth * sizeof(uint16_t));
		}
	}
	void rdRaster_CopyToTileColorRGBA(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		if (pCanvas->vbuffer->format.format.bpp == 16)
		{
			uint16_t* colorBuffer = (uint16_t*)pCanvas->vbuffer->surface_lock_alloc;

			// Copy color buffer row-wise
			for (int row = 0; row < tileHeight; ++row)
			{
				uint16_t* src = colorBuffer + (tileMinY + row) * width + tileMinX;
				uint32_t* dst = rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE;

				for (int col = 0; col < tileWidth; ++col)
					dst[col] = stdColor_Recode(src[col], &pCanvas->vbuffer->format.format, &rdRaster_32bitMode);

				if (tileWidth < RDCACHE_FINE_TILE_SIZE)
					memset(dst + tileWidth, 0, RDCACHE_FINE_TILE_SIZE - tileWidth);
			}
		}

		// Clear remaining rows if needed
		for (int row = tileHeight; row < RDCACHE_FINE_TILE_SIZE; ++row)
			memset(rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE, 0, sizeof(uint32_t) * RDCACHE_FINE_TILE_SIZE);
	}
	void rdRaster_CopyFromTileColorRGBA(int tileX, int tileY)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
		int width = pCanvas->vbuffer->format.width;
		int height = pCanvas->vbuffer->format.height;

		const int tileMinX = tileX * RDCACHE_FINE_TILE_SIZE;
		const int tileMinY = tileY * RDCACHE_FINE_TILE_SIZE;

		if (tileMinX >= width || tileMinY >= height)
			return;

		const int maxW = RDCACHE_FINE_TILE_SIZE;
		const int maxH = RDCACHE_FINE_TILE_SIZE;

		const int tileWidth = (tileMinX + maxW > width) ? (width - tileMinX) : maxW;
		const int tileHeight = (tileMinY + maxH > height) ? (height - tileMinY) : maxH;

		if (pCanvas->vbuffer->format.format.bpp == 16)
		{
			uint16_t* colorBuffer = (uint16_t*)pCanvas->vbuffer->surface_lock_alloc;
			for (int row = 0; row < tileHeight; ++row)
			{
				uint16_t* dst = colorBuffer + (tileMinY + row) * width + tileMinX;
				uint32_t* src = rdRaster_TileColorRGBA + row * RDCACHE_FINE_TILE_SIZE;

				for (int col = 0; col < tileWidth; ++col)
					dst[col] = stdColor_Recode(src[col], &rdRaster_32bitMode, &pCanvas->vbuffer->format.format);
			}
		}
	}
#endif

	void rdCache_DrawFaceTiled(uint8_t** ppStream, int tileX, int tileY)
	{
		uint32_t numBytes = 0;

		rdRaster_PrimitiveEncoderDecoder decoder(ppStream, &numBytes);

		// Decode the header
		const rdTilePrimitiveHeader* pHeader = decoder.Advance<const rdTilePrimitiveHeader>();

		// Tracking everything across all of the nesting can be tedious so pack it all into a command struct
		rdTileDrawCommand command;
		memset(&command, 0, sizeof(rdTileDrawCommand));
		command.pDecoder = &decoder;
		command.pHeader = pHeader;

		if (rdCamera_pCurCamera->canvas->vbuffer->format.format.colorMode == 0)
			rdRaster_DrawToTileByZMethod<STDCOLOR_PAL>(&command, ppStream, tileX, tileY);
		else
			rdRaster_DrawToTileByZMethod<STDCOLOR_RGB>(&command, ppStream, tileX, tileY);

		if (pHeader->numBytes != numBytes)
			stdPlatform_Printf("Error decoding command stream\n");
	}

#ifdef JOB_SYSTEM
#define USE_JOBS
#endif

#ifdef USE_JOBS
	// Processes a tile
	void rdRaster_FlushBinJob(uint32_t jobIndex, uint32_t groupIndex)
	{
		rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;

		if(jobIndex >= pCanvas->tileCount)
			return;

		int fineX = jobIndex % pCanvas->tileWidth;
		int fineY = jobIndex / pCanvas->tileWidth;

		rdRaster_CopyToTileDepth(fineX, fineY);

		if (pCanvas->vbuffer->format.format.colorMode == 0)
			rdRaster_CopyToTileColor(fineX, fineY);
		else
			rdRaster_CopyToTileColorRGBA(fineX, fineY);

		uint32_t* fineMask = &pCanvas->tileBits[jobIndex * RDCACHE_TILE_BINNING_STRIDE];
		for (int word = 0; word < RDCACHE_TILE_BINNING_STRIDE; word++)
		{
			uint32_t bits = fineMask[word];
			while (bits != 0)
			{
				int bit = stdMath_FindLSB(bits);
				int triIndex = word * 32 + bit;
				bits ^= (1ull << bit);

				// grab the command stream at this primitive offset and draw the primitive
				uint8_t* pStream = rdRaster_pTileStreamStart + rdRaster_aTilePrimitiveOffsets[triIndex];
				rdCache_DrawFaceTiled(&pStream, fineX, fineY);
			}
		}

		rdRaster_CopyFromTileDepth(fineX, fineY);
		if (pCanvas->vbuffer->format.format.colorMode == 0)
			rdRaster_CopyFromTileColor(fineX, fineY);
		else
			rdRaster_CopyFromTileColorRGBA(fineX, fineY);
}
#endif

void rdRaster_FlushBins()
{
	rdRaster_fogScale = rdroid_curFogRange * rdCamera_pCurCamera->pClipFrustum->zFar * 63.0f;
	rdRaster_fogOffset = -rdroid_curFogStartDepth * rdroid_curFogRange * 63.0f;

	rdCanvas* pCanvas = rdCamera_pCurCamera->canvas;
	stdDisplay_VBufferLock(pCanvas->vbuffer);
	stdDisplay_VBufferLock(pCanvas->d3d_vbuf);

	int width_in_pixels = pCanvas->vbuffer->format.width;
	int height_in_pixels = pCanvas->vbuffer->format.height;
	uint8_t* colorBuffer = (uint8_t*)pCanvas->vbuffer->surface_lock_alloc;
	uint16_t* zBuffer = (uint16_t*)(pCanvas->d3d_vbuf->surface_lock_alloc);

#ifdef USE_JOBS
	stdJob_Dispatch(pCanvas->tileCount, 8, rdRaster_FlushBinJob);
	stdJob_Wait();
#else
	// traversing coarse bins helps reduce the number of tiles we need to process
	for (int coarseIdx = 0; coarseIdx < pCanvas->coarseTileCount; coarseIdx++)
	{
		uint32_t* coarseMask = &pCanvas->coarseTileBits[coarseIdx * RDCACHE_TILE_BINNING_STRIDE];

		int coarseX = coarseIdx % pCanvas->coarseTileWidth;
		int coarseY = coarseIdx / pCanvas->coarseTileWidth;

		for (int fy = 0; fy < RDCACHE_FINE_PER_COARSE; fy++)
		{
			for (int fx = 0; fx < RDCACHE_FINE_PER_COARSE; fx++)
			{
				int fineX = coarseX * RDCACHE_FINE_PER_COARSE + fx;
				int fineY = coarseY * RDCACHE_FINE_PER_COARSE + fy;
				int fineIdx = fineY * pCanvas->tileWidth + fineX;
				if (fineIdx >= pCanvas->tileCount)
				{
					//stdPlatform_Printf("the fuck?\n");
					break;
				}

				rdRaster_CopyToTileMem(fineX, fineY);

				uint32_t* fineMask = &pCanvas->tileBits[fineIdx * RDCACHE_TILE_BINNING_STRIDE];
				for (int word = 0; word < RDCACHE_TILE_BINNING_STRIDE; word++)
				{
					uint32_t bits = fineMask[word];
					while (bits != 0)
					{
						int bit = stdMath_FindLSB(bits);
						int triIndex = word * 32 + bit;
						bits ^= (1ull << bit);

						if (triIndex >= rdRaster_numTilePrimitives)
						{
							//stdPlatform_Printf("Somehow triIndex is higher than the number of active primitives\n");
							break;
						}
						//stdPlatform_Printf("Drawing primitive %d\n", triIndex);

						uint8_t* ppStream = rdRaster_pTileStreamStart + rdRaster_aTilePrimitiveOffsets[triIndex];
						rdCache_DrawFaceTiled(&ppStream, fineX, fineY);

						//rdCache_DrawFaceTiled(&rdRaster_aTilePrimitives[triIndex], fineX, fineY);
					}
				}

				rdRaster_CopyFromTileMem(fineX, fineY);
			}
		}
	}
#endif
	pCanvas->bIdk &= ~4;

	stdDisplay_VBufferUnlock(pCanvas->vbuffer);
	stdDisplay_VBufferUnlock(pCanvas->d3d_vbuf);
}

}
#endif
