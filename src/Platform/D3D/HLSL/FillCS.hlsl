#define VRAM_REGISTER u0
#define DESCRIPTOR_REGISTER t0

#include "VRAM.h"
#include "Descriptors.h"

cbuffer FillInfo : register( b0 )
{
	int4 SrcRect;
	int  Fill;
    int SrcHandle, Pad1, Pad2;
};

[numthreads(256, 1, 1)]
void main(int3 dispatchThreadID : SV_DispatchThreadID)
{
	if (any(dispatchThreadID.xy >= SrcRect.zw))
		return;
	
    const Descriptor srcDesc = Descriptors[SrcHandle];   
    uint2 srcAddressAndStride = uint2(srcDesc.offset, srcDesc.rowStride);
    
	int2 coord = dispatchThreadID.xy + SrcRect.xy;
    if (any(coord.xy >= srcDesc.resolution.xy))
		return;
		
	int bpp = 8;
	switch (bpp)
	{
	case 8:
        Store8(Fill & 0xFF, coord, srcAddressAndStride);
		return;

	case 16:
        Store16(Fill & 0xFFFF, coord, srcAddressAndStride);
		return;

	case 32:
        Store32(Fill, coord, srcAddressAndStride);
		return;

	default:
		return;
	}		
}
