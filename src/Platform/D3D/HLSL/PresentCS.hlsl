#define VRAM_READ_ONLY
#define VRAM_REGISTER t0
#define DESCRIPTOR_REGISTER t1

#include "VRAM.h"
#include "Descriptors.h"

cbuffer PresentInfo : register( b0 )
{
	int2  SrcAddressAndStride;
	int2  SrcSize;
	int2  DstSize;
    int   SrcHandle;
	int   Padding;
	uint4 DstRect;
};

RWTexture2D<float4> BackBuffer : register(u0);

Buffer<float4> Palette : register(t2);

[numthreads(256, 1, 1)]
void main(int3 dispatchThreadID : SV_DispatchThreadID)
{
	uint2 coord = dispatchThreadID.xy;
    if (any(coord.xy >= DstRect.zw))
        return;
    
    const Descriptor srcDesc = Descriptors[SrcHandle];
   
    uint2 srcCoord = (coord.xy * srcDesc.resolution.xy) / DstRect.zw;//(coord.xy * SrcSize.xy) / DstRect.zw;
    uint2 srcAddressAndStride = uint2(srcDesc.offset, srcDesc.rowStride);
  
    uint pixel = Load8(srcCoord.xy, srcAddressAndStride);//SrcAddressAndStride);
	BackBuffer[coord.xy + DstRect.xy] = Palette[pixel];
}
