#define VRAM_REGISTER u0
#define DESCRIPTOR_REGISTER t0

#include "VRAM.h"
#include "Descriptors.h"

cbuffer CopyInfo : register( b0 )
{
	int4 SrcRect;
	int4 DstRect;
	
	int  Flags;
	uint TransparentColor;
	uint SrcHandle, DstHandle;
};

static const uint Flag_Transparency = 1 << 0;

[numthreads(256, 1, 1)]
void main(int3 dispatchThreadID : SV_DispatchThreadID)
{
	int2 coord = dispatchThreadID.xy;	
	if (any(coord.xy >= DstRect.zw))
		return;

	const Descriptor srcDesc = Descriptors[SrcHandle];
	const Descriptor dstDesc = Descriptors[DstHandle];
				
	const int2 srcCoord = coord.xy + SrcRect.xy;
	const int2 dstCoord = coord.xy + DstRect.xy;
	if (any(srcCoord.xy >= srcDesc.resolution.xy || dstCoord.xy >= dstDesc.resolution.xy))
		return;
	
	uint bpp = GetDescriptorBits(dstDesc);
	bool transparency = bool(Flags & Flag_Transparency);
	
	uint2 srcAddressAndStride = uint2(srcDesc.offset, srcDesc.rowStride);
	uint2 dstAddressAndStride = uint2(dstDesc.offset, dstDesc.rowStride);

	switch (bpp)
	{
	case 8:
	{
		uint pixel = Load8(srcCoord, srcAddressAndStride);
		if (transparency && pixel == (TransparentColor & 0xFF))
			return;	
		Store8(pixel, dstCoord, dstAddressAndStride);
		return;
	}

	case 16:
	{
		uint pixel = Load16(srcCoord, srcAddressAndStride);
		if (transparency && pixel == (TransparentColor & 0xFFFF)) 
			return;	
		Store16(pixel, dstCoord, dstAddressAndStride);
		return;
	}

	case 24:
		// not supported here, should remove from CPU side too
		return;

	case 32:
	{
		uint pixel = Load32(srcCoord, srcAddressAndStride);
		if (transparency && pixel == TransparentColor)
			return;	
		Store32(pixel, dstCoord, dstAddressAndStride);
		return;
	}

	default:
		// Unsupported bpp
		return;
	}
}
