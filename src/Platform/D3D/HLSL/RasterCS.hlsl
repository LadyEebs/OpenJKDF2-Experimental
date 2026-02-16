#define VRAM_REGISTER u0
#define STREAM_REGISTER t7
#define DESCRIPTOR_REGISTER t9

#include "VRAM.h"
#include "Stream.h"
#include "Descriptors.h"

// todo: get from engine
#define RDCACHE_MAX_TRIS (0x1000)
#define RDCACHE_FINE_TILE_SIZE 16
#define RDCACHE_TILE_BINNING_STRIDE  ((RDCACHE_MAX_TRIS+31) / 32)

static const uint StateFlag_WriteZ = 1 << 0;
static const uint StateFlag_ReadZ  = 1 << 1;
static const uint StateFlag_ClearZ = 1 << 2;

cbuffer RasterInfo : register( b0 )
{
    int2 ColorAddressAndStride;
    int2 DepthAddressAndStride;
    int2 CanvasSize;
    uint StateFlags;
    uint Padding;
};

Buffer<float3> aVertices : register(t0);
Buffer<float2> aTexVertices : register(t1);
Buffer<float> aIntensities : register(t2);

// todo
#ifdef JKM_LIGHTING
Buffer<float> aIntensities : register(t3);
Buffer<float> aIntensities : register(t4);
Buffer<float> aIntensities : register(t5);
#endif

Buffer<uint> aTileBits : register(t6);
Buffer<uint> aPrimitiveOffsets : register(t8);

float ToFloat(int fixed)
{
	return (float)fixed / 256.0;
}

[numthreads(RDCACHE_FINE_TILE_SIZE, RDCACHE_FINE_TILE_SIZE, 1)]
void main(int3 groupID : SV_GroupID, int groupIndex : SV_GroupIndex, int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
	int2 coord = int2(dispatchThreadID.xy);
	int2 coordFixed = coord << 8;
   
	//int tileMinX = groupID.x * RDCACHE_FINE_TILE_SIZE;
	//int tileMinY = groupID.y * RDCACHE_FINE_TILE_SIZE;
	//int tileMaxX = tileMinX + RDCACHE_FINE_TILE_SIZE - 1;
	//int tileMaxY = tileMinY + RDCACHE_FINE_TILE_SIZE - 1;

	uint currentDepth = (StateFlags & StateFlag_ClearZ) ? 0xFFFF : Load16(coord, DepthAddressAndStride);
	uint currentColor = Load8(coord, ColorAddressAndStride);
	
	// todo: should probably iterate the coarse bits
	uint binOffset = groupIndex * RDCACHE_TILE_BINNING_STRIDE;
	for (uint word = 0; word < RDCACHE_TILE_BINNING_STRIDE; word++)
	{
		uint bits = aTileBits[binOffset + word];
		while (bits != 0)
		{
			int bit = firstbitlow(bits);
			int triIndex = word * 32 + bit;
			bits ^= (1 << bit);
			
			uint byteOffset = aPrimitiveOffsets[triIndex];
			
			PrimitiveHeader header = ReadPrimitiveHeader(byteOffset);
			
			// read tex data
			PrimitiveTex tex;
			uint solidColor;
			if (header.geoMode == 3) // solid
				solidColor = ReadUint(byteOffset);
			else if (header.geoMode == 4) // textured
				tex = ReadPrimitiveTex(byteOffset);
			else
				continue;
				
			// read fmt
			PrimitiveFmt fmt;
			if (header.colorDepth > 1)
				fmt = ReadPrimitiveFmt(byteOffset);
			
			// read flat light
			uint flatLight;
			if (header.lightMode > 0 && header.lightMode < 3)
				flatLight = ReadUint(byteOffset);

			uint triIdx = ReadUint(byteOffset);
			while((triIdx != 0x80007FFF) && (triIdx < header.numTris))
			{
				TriangleHeader triHeader = ReadTriangleHeader(byteOffset);

				float u, v;
				if (header.geoMode == 4)
				{
					TriangleUVs uvs = ReadTriangleUVs(byteOffset);
					u = uvs.u_dy * coord.y + uvs.u_dx * coord.x + uvs.u_offset;							
					v = uvs.v_dy * coord.y + uvs.v_dx * coord.x + uvs.v_offset;
				}

				float l;
				if (header.lightMode == 3)
				{
					TriangleLights lights = ReadTriangleLights(byteOffset);
					l = lights.l_dy * coord.y + lights.l_dx * coord.x + lights.l_offset;
				}

				int w0 = ((triHeader.w0_dy * coordFixed.y)>>8) + ((triHeader.w0_dx * coordFixed.x) >> 8) + triHeader.w0_offset;
				int w1 = ((triHeader.w1_dy * coordFixed.y)>>8) + ((triHeader.w1_dx * coordFixed.x) >> 8) + triHeader.w1_offset;
				int w2 = ((triHeader.w2_dy * coordFixed.y)>>8) + ((triHeader.w2_dx * coordFixed.x) >> 8) + triHeader.w2_offset;
				
				// coverage test
				if ((w0 | w1 | w2) >= 0)
				{					
					float z = triHeader.z_dy * coord.y + triHeader.z_dx * coord.x + triHeader.z_offset;
					float iz = rcp(z);

					uint zi = (uint)(int)(iz * 65535.0f + 0.5f);
					uint depthMask = 0xFFFF;

					// Early Z
					if (!header.hasDiscard && (StateFlags & StateFlag_ReadZ))
						depthMask = (zi <= currentDepth) ? 0xFFFF : 0;

					// Texture/Color
					uint index = 0;
					if (header.geoMode == 3)
						index = solidColor & 0xFF;
					else if (header.geoMode == 4)
					{
						if (header.texMode == 1)
						{
							u *= iz;
							v *= iz;
						}

						int x_fixed = (int)(65536.0 * u) + 0x8000;
						int y_fixed = (int)(65536.0 * v) + 0x8000;

						// Coordinate wrapping
						int x_wrapped = (x_fixed & tex.uWrap) >> 16;
						int y_wrapped = (y_fixed >> (16 - tex.texRowShift)) & tex.vWrap;

						// TODO: RGBA
						Descriptor desc = Descriptors[tex.textureOffsetAndStride.x];
						index = depthMask ? SampleTexture(desc, int2(x_wrapped, y_wrapped)) : 0;
					}

					// Alpha test
					uint discardMask = 0;
					if (header.hasDiscard)
						discardMask = (index == 0) ? 0xFFFF : 0;

					// Late Z
                    if (header.hasDiscard && (StateFlags & StateFlag_ReadZ))
						depthMask = (zi <= currentDepth) ? 0xFFFF : 0;

					const uint write_mask = depthMask & ~discardMask;
					const uint read_mask = ~write_mask;

                    if (StateFlags & StateFlag_WriteZ)
					{
						uint oldZ = currentDepth;
						currentDepth = (write_mask & zi) | (read_mask & oldZ);
					}

					// Lighting
					if (header.lightMode == 3)
						index = 0;//write_mask ? lightLevels[stdMath_ClampInt((uint32_t)l + rdRaster_DitherLUT[(x & 3) + (y & 3) * 4], 0, 63) * 256 + index] : 0;

					if (header.lightMode == 1 || header.lightMode == 2)
						index = 0;//write_mask ? lightLevels[stdMath_ClampInt(pCommand->flatLight + rdRaster_DitherLUT[(x & 3) + (y & 3) * 4], 0, 63) * 256 + index] : 0;

					// Blending
					uint oldIndex = currentColor;
					if (header.blend)
						index = 0;//write_mask ? transparency[oldIndex * 256 + index] : 0;

					currentColor = (write_mask & index) | (read_mask & oldIndex);
				}
				
				triIdx = ReadUint(byteOffset);
			}
		}
	}	
		
	Store16(currentDepth, coord, DepthAddressAndStride);
	Store8(currentColor, coord, ColorAddressAndStride);
}
