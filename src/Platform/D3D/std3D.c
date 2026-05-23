#include "Platform/std3D.h"
#include "engine_config.h"
#include "stdPlatform.h"
#include "Win95/stdDisplay.h"
#include "General/stdString.h"
#include "Platform/Common/stdEmbeddedRes.h"

#if (!defined(SDL2_RENDER) && !defined(TARGET_TWL)) || defined(TILE_SW_RASTER)

#ifdef TILE_SW_RASTER

#ifdef _WIN32

#define COBJMACROS
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "dxguid.lib" )

#include "SDL.h"
#include <SDL_syswm.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <d3d11_3.h>
#include <dxgi.h>
#include <d3dcompiler.h>
//#include <wrl.h>

#undef max
#undef min

#pragma comment( lib, "dxguid" )

void std3D_VerifySwapchain();

typedef struct int4
{
	int32_t r, g, b, a;
} int4;

int std3D_bReinitHudElements = 0;

// d3d device list
typedef struct std3DDeviceDriver
{
	const char* name;
	const char* desc;
} std3DDeviceDriver;

static const std3DDeviceDriver kDriverTypes[] =
{
	{ "Direct3D 11", "Microsoft Direct3D Hardware Renderer" }
};

d3d_device std3D_d3dDevices[16];
int std3D_d3dDeviceCount = 0;

// ID3D11Buffer wrapper
typedef struct std3DGpuBuffer
{
	ID3D11Buffer*              pBuffer;
	ID3D11ShaderResourceView*  pShaderView;
	ID3D11UnorderedAccessView* pUnorderedView;
	D3D11_MAPPED_SUBRESOURCE   mapped;
} std3DGpuBuffer;

// Surface blitting
// Todo: use descriptors
typedef struct std3DBlitInfo
{
	int32_t SrcRectX, SrcRectY, SrcRectW, SrcRectH;
	int32_t DstRectX, DstRectY, DstRectW, DstRectH;

	uint32_t Flags;
	uint32_t TransparentColor;
	uint32_t  SrcHandle, DstHandle;
} std3DBlitInfo;

static ID3D11ComputeShader* std3D_pBlitShader = NULL;
static ID3D11Buffer* std3D_pBlitConstants = NULL;

// Surface filling
typedef struct std3DFillInfo
{
	int32_t SrcRectX, SrcRectY, SrcRectW, SrcRectH;
	uint32_t Fill;
	uint32_t SrcHandle;
	int Pad1, Pad2;
} std3DFillInfo;

static ID3D11ComputeShader* std3D_pFillShader = NULL;
static ID3D11Buffer* std3D_pFillConstants = NULL;

// Surface present (draw to swap chain)
// Todo: use descriptors
typedef struct std3DPresentInfo
{
	int32_t SrcAddress, SrcStride;
	int32_t SrcWidth, SrcHeight;
	int32_t DstWidth, DstHeight;
	int32_t SrcHandle, Padding1;
	int32_t DstRectX, DstRectY, DstRectW, DstRectH;
} std3DPresentInfo;

static ID3D11ComputeShader* std3D_pPresentShader = NULL;
static ID3D11Buffer* std3D_pPresentConstants = NULL;

// Raster flush
typedef struct std3DRasterInfo
{
	int32_t ColorAddress, ColorStride;
	int32_t DepthAddress, DepthStride;
	int32_t Width, Height;
	int32_t Padding0, Padding1;
} std3DRasterInfo;

static ID3D11ComputeShader* std3D_pRasterShader = NULL;
static ID3D11Buffer* std3D_pRasterConstants = NULL;

// Active device state
static ID3D11Device1* std3D_device = NULL;
static ID3D11DeviceContext1* std3D_deviceContext = NULL;
static stdHwnd std3D_swapWindow = NULL;
static IDXGISwapChain* std3D_swapChain = NULL;
static ID3D11UnorderedAccessView* std3D_backBufferUAV = NULL;

// Virtual Video Memory
typedef struct std3DBlock std3DBlock;

// Allocation block (points into vram buffer)
typedef struct std3DBlock
{
	uint32_t    offset;
	uint32_t    size;
	std3DBlock* pNext;
} std3DBlock;

static std3DBlock* std3D_vramBlocks = NULL;
static int         std3D_vramMapped = 0; // vram map status flag, increments on every map, decrements on every unmap, only does map/unmap once

static std3DGpuBuffer std3D_vram;

// Packed form of rdTexformat for GPU access
typedef struct std3DDescriptorFormat
{
	// low 32
	uint32_t colorMode  : 2;
	uint32_t bpp        : 6;
	uint32_t r_bits     : 4;
	uint32_t r_shift    : 6;
	uint32_t r_bitdiff  : 4;
	uint32_t g_bits     : 4;
	uint32_t g_shift    : 6;

	// high 32
	uint32_t g_bitdiff  : 4;
	uint32_t b_bits     : 4;
	uint32_t b_shift    : 6;
	uint32_t b_bitdiff  : 4;
	uint32_t a_bits     : 4;
	uint32_t a_shift    : 6;
	uint32_t a_bitdiff  : 4;
} std3DDescriptorFormat;
static_assert(sizeof(std3DDescriptorFormat) == sizeof(uint64_t), "std3DDescriptorFormat must be 64 bits in size");

void std3D_PackFormat(std3DDescriptorFormat* pSmallFormat, const rdTexformat* pFormat)
{
	pSmallFormat->colorMode = pFormat->colorMode;
	pSmallFormat->bpp = pFormat->bpp;
	pSmallFormat->r_bits = pFormat->r_bits;
	pSmallFormat->r_shift = pFormat->r_shift;
	pSmallFormat->r_bitdiff = pFormat->r_bitdiff;
	pSmallFormat->g_bits = pFormat->g_bits;
	pSmallFormat->g_shift = pFormat->g_shift;
	pSmallFormat->g_bitdiff = pFormat->g_bitdiff;
	pSmallFormat->b_bits = pFormat->b_bits;
	pSmallFormat->b_shift = pFormat->b_shift;
	pSmallFormat->b_bitdiff = pFormat->b_bitdiff;
	pSmallFormat->a_bits = pFormat->unk_40;
	pSmallFormat->a_shift = pFormat->unk_44;
	pSmallFormat->a_bitdiff = pFormat->unk_48;
}

// Texture descriptor, describes a texture living in the vram buffer
typedef struct std3DDescriptor
{
	uint32_t              width, height;
	uint32_t              freeInt;
	uint32_t              totalSize;
	uint32_t              offset; // offset into vram
	uint32_t              rowStride;
	std3DDescriptorFormat format;
} std3DDescriptor;
static_assert(sizeof(std3DDescriptor) == sizeof(uint32_t) * 8, "std3DDescriptor must be 32 bytes in size");

#define STD3D_MAX_DESCRIPTORS 65535 // todo: what's a good max? we could maybe batch upload textures every frame (cache) to get under 128, then we can bind them as normal textures...
static std3DGpuBuffer std3D_descriptors;
static std3DDescriptor std3D_descriptorData[STD3D_MAX_DESCRIPTORS];
static uint32_t std3D_numDescriptors = 0;
static int std3D_freeDescriptors[STD3D_MAX_DESCRIPTORS];
static uint32_t std3D_numFreeDescriptors = 0;

void std3D_UpdateDescriptor(uint32_t index, std3DDescriptor* pData)
{
	UINT structSize = sizeof(std3DDescriptor);

	D3D11_BOX box;
	memset(&box, 0, sizeof(D3D11_BOX));
	box.left = index * structSize;
	box.right = box.left + structSize;
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;

	std3D_descriptorData[index] = *pData; // CPU side buffer
	ID3D11DeviceContext_UpdateSubresource(std3D_deviceContext, std3D_descriptors.pBuffer, 0, &box, pData, 0, 0);
}

int std3D_CreateDescriptor(std3DDescriptor* pDescriptor)
{
	int idx;
	if (std3D_numFreeDescriptors)
	{
		idx = std3D_freeDescriptors[std3D_numFreeDescriptors--];
	}
	else
	{
		idx = std3D_numDescriptors++;
		if (idx >= STD3D_MAX_DESCRIPTORS)
		{
			stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error: max descriptor count reached\n");
			return -1;
		}
	}

	std3D_UpdateDescriptor(idx, pDescriptor);

	return idx;
}

void std3D_ReleaseDescriptor(int idx)
{
	std3D_freeDescriptors[std3D_numFreeDescriptors++] = idx;
}

// Display palette (8 bit palette)
// todo: display cube?
static std3DGpuBuffer std3D_palette;

// Vertex streams
enum std3DVertexBufferIdx
{
	STD3D_VERTEX_ARRAY,
	STD3D_TEXVERTEX_ARRAY,
	STD3D_INTENSITY_ARRAY,
#ifdef JKM_LIGHTING
	STD3D_RED_INTENSITY_ARRAY,
	STD3D_GREEN_INTENSITY_ARRAY,
	STD3D_BLUE_INTENSITY_ARRAY
#endif
};

typedef struct std3DVertexStreamFormat
{
	DXGI_FORMAT deviceFormat;
	int stride;
} std3DVertexStreamFormat;

static const std3DVertexStreamFormat kVertexStreams[] =
{
	{ DXGI_FORMAT_R32G32B32_FLOAT, sizeof(rdVector3) },
	{ DXGI_FORMAT_R32G32_FLOAT, sizeof(rdVector2) },
	{ DXGI_FORMAT_R32_FLOAT, sizeof(float) },
#ifdef JKM_LIGHTING
	{ DXGI_FORMAT_R32_FLOAT, sizeof(float) },
	{ DXGI_FORMAT_R32_FLOAT, sizeof(float) },
	{ DXGI_FORMAT_R32_FLOAT, sizeof(float) },
#endif
};

static std3DGpuBuffer std3D_primitiveStream;
static std3DGpuBuffer std3D_primitiveOffsets;
static std3DGpuBuffer std3D_vertexStreams[ARRAY_SIZE(kVertexStreams)];

// Shader interface
int std3D_CreateComputeShader(ID3D11ComputeShader** pShader, const char* filePath)
{
	size_t len;
	char* pShaderByteCode = stdEmbeddedRes_Load(filePath, &len);
	if (!pShaderByteCode)
		return 0;
	// todo: error checking
	HRESULT res = ID3D11Device_CreateComputeShader(std3D_device, (void*)pShaderByteCode, len - 1, NULL, pShader);
	return res == S_OK;
}

void std3D_ReleaseComputeShader(ID3D11ComputeShader** pShader)
{
	if (*pShader)
	{
		ID3D11ComputeShader_Release(*pShader);
	}
	*pShader = NULL;
}

// Constant buffer interface
int std3D_CreateConstantBuffer(ID3D11Buffer** pConstantBuffer, int byteWidth)
{
	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = byteWidth;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;
	HRESULT res = ID3D11Device_CreateBuffer(std3D_device, &desc, NULL, pConstantBuffer);
	return res == S_OK;
}

void std3D_ReleaseConstantBuffer(ID3D11Buffer** pConstantBuffer)
{
	if (*pConstantBuffer)
		ID3D11Buffer_Release(*pConstantBuffer);
	*pConstantBuffer = 0;
}


// GPU Buffer interface
void std3D_CreateGpuBuffer(std3DGpuBuffer* pGpuBuffer, int numElements, int stride, DXGI_FORMAT format, int writeable, D3D11_RESOURCE_MISC_FLAG miscFlags)
{
	memset(pGpuBuffer, 0, sizeof(std3DGpuBuffer));

	D3D11_BUFFER_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = D3D11_USAGE_DEFAULT;//(writeable ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC);
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = miscFlags;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writeable ? D3D11_BIND_UNORDERED_ACCESS : 0);
	desc.ByteWidth = numElements * stride;
	if (miscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
		desc.StructureByteStride = stride;
	ID3D11Device_CreateBuffer(std3D_device, &desc, NULL, &pGpuBuffer->pBuffer);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	//srvDesc.Buffer.FirstElement = 0;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	srvDesc.Format = format;
	if (miscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
	{
		srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		srvDesc.BufferEx.NumElements = desc.ByteWidth / 4;
	}
	else
	{
		srvDesc.BufferEx.FirstElement = 0;
		srvDesc.BufferEx.NumElements = numElements;
	}
	ID3D11Device_CreateShaderResourceView(std3D_device, pGpuBuffer->pBuffer, &srvDesc, &pGpuBuffer->pShaderView);

	if (writeable)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		ZeroMemory(&uavDesc, sizeof(uavDesc));
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		if (miscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
		{
			uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			uavDesc.Buffer.NumElements = desc.ByteWidth / 4;
		}
		else
		{
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = numElements;
		}
		ID3D11Device_CreateUnorderedAccessView(std3D_device, pGpuBuffer->pBuffer, &uavDesc, &pGpuBuffer->pUnorderedView);
	}
}

void std3D_ReleaseGpuBuffer(std3DGpuBuffer* pGpuBuffer)
{
	if (pGpuBuffer->pBuffer)
		ID3D11Buffer_Release(pGpuBuffer->pBuffer);
	pGpuBuffer->pBuffer = NULL;

	if (pGpuBuffer->pShaderView)
		ID3D11ShaderResourceView_Release(pGpuBuffer->pShaderView);
	pGpuBuffer->pShaderView = NULL;

	if (pGpuBuffer->pUnorderedView)
		ID3D11UnorderedAccessView_Release(pGpuBuffer->pUnorderedView);
	pGpuBuffer->pUnorderedView = NULL;
}

void* std3D_MapGpuBuffer(std3DGpuBuffer* pGpuBuffer)
{
	ID3D11DeviceContext_Map(std3D_deviceContext, pGpuBuffer->pBuffer, 0, D3D11_MAP_WRITE, 0, &pGpuBuffer->mapped);
	return pGpuBuffer->mapped.pData;
}

void std3D_UnmapGpuBuffer(std3DGpuBuffer* pGpuBuffer)
{
	if(pGpuBuffer->mapped.pData)
		ID3D11DeviceContext_Unmap(std3D_deviceContext, pGpuBuffer->pBuffer, 0);
	pGpuBuffer->mapped.pData = 0;
}

// Vertex stream interface
void std3D_CreateVertexStreams()
{
	for (int i = 0; i < ARRAY_SIZE(kVertexStreams); ++i)
		std3D_CreateGpuBuffer(&std3D_vertexStreams[i], RDCACHE_MAX_VERTICES, kVertexStreams[i].stride, kVertexStreams[i].deviceFormat, 0, 0);

	std3D_CreateGpuBuffer(&std3D_primitiveStream, RDCACHE_MAX_TILE_TRIS * 1024 / 4, sizeof(uint32_t), DXGI_FORMAT_R32_TYPELESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
	std3D_CreateGpuBuffer(&std3D_primitiveOffsets, RDCACHE_MAX_TILE_TRIS, sizeof(uint32_t), DXGI_FORMAT_R32_UINT, 0, 0);
}

void std3D_ReleaseVertexStreams()
{
	std3D_ReleaseGpuBuffer(&std3D_primitiveOffsets);
	std3D_ReleaseGpuBuffer(&std3D_primitiveStream);

	for (int i = 0; i < ARRAY_SIZE(kVertexStreams); ++i)
		std3D_ReleaseGpuBuffer(&std3D_vertexStreams[i]);
}

void* std3D_LockVertexStream(int idx)
{
	return std3D_MapGpuBuffer(&std3D_vertexStreams[idx]);
}

void std3D_UnlockVertexStream(int idx)
{
	std3D_UnmapGpuBuffer(&std3D_vertexStreams[idx]);
}

uint8_t* std3D_LockRenderList()
{
	return std3D_MapGpuBuffer(&std3D_primitiveStream);
}

void std3D_UnlockRenderList()
{
	std3D_UnmapGpuBuffer(&std3D_primitiveStream);
}

// VRAM interface
int std3D_CreateVRAM()
{
	std3D_CreateGpuBuffer(&std3D_vram, 64 * 1024 * 1024, sizeof(uint8_t), DXGI_FORMAT_R8_UINT, 1, 0);
	std3D_CreateGpuBuffer(&std3D_palette, 256, sizeof(uint32_t), DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0);

	std3D_vramBlocks = (std3DBlock*)std_pHS->alloc(sizeof(std3DBlock));
	if (!std3D_vramBlocks)
		return 0;

	std3D_vramBlocks->offset = 0;
	std3D_vramBlocks->size = 64 * 1024 * 1024;
	std3D_vramBlocks->pNext = NULL;
	return 1;
}

void std3D_ReleaseVRAM()
{
	std3DBlock* curr = std3D_vramBlocks;
	while (curr)
	{
		std3DBlock* pNext = curr->pNext;
		std_pHS->free(curr);
		curr = pNext;
	}
	std3D_vramBlocks = NULL;

	std3D_ReleaseGpuBuffer(&std3D_vram);
	std3D_ReleaseGpuBuffer(&std3D_palette);
}

// Surface interface
#if 1

uint64_t std3D_CreateSurface(stdVBufferTexFmt* pTexFormat)
{
	int size = pTexFormat->texture_size_in_bytes;

	std3DBlock** curr = &std3D_vramBlocks;
	while (*curr)
	{
		std3DBlock* block = *curr;
		int offset = block->offset;
		if (block->size >= size)
		{
			if (size == block->size)
			{
				// Exact fit, remove block
				*curr = block->pNext;
				std_pHS->free(block);
			}
			else
			{
				// Partial use of block
				block->offset = offset + size;
				block->size -= size;
			}

			std3DDescriptor desc;
			std3D_PackFormat(&desc.format, &pTexFormat->format);
			desc.width = pTexFormat->width;
			desc.height = pTexFormat->height;
			desc.offset = offset;
			desc.rowStride = pTexFormat->width_in_bytes;
			desc.totalSize = size;

			int descriptor = std3D_CreateDescriptor(&desc);

			return ((uint64_t)(descriptor & 0x3FF) << 54) | ((uint64_t)(size & 0x3FFFFF) << 32) | ((uint64_t)offset & 0xFFFFFFFF);
		}
		curr = &block->pNext;
	}
	return 0;
}

void std3D_ReleaseSurface(uint64_t handle)
{
	std3DBlock* new_block = (std3DBlock*)std_pHS->alloc(sizeof(std3DBlock));
	if (!new_block)
		return;

	uint32_t offset = (uint32_t)(handle & 0xFFFFFFFF);
	uint32_t size = (uint32_t)(handle >> 32) & 0x3FFFFF;
	int descriptor = (int)(handle >> 54);

	std3D_ReleaseDescriptor(descriptor);

	new_block->offset = offset;
	new_block->size = size;
	new_block->pNext = NULL;

	std3DBlock** curr = &std3D_vramBlocks;
	while (*curr && (*curr)->offset < offset)
		curr = &(*curr)->pNext;

	new_block->pNext = *curr;
	*curr = new_block;

	// Coalesce adjacent blocks
	std3DBlock* prev = NULL;
	std3DBlock* iter = std3D_vramBlocks;

	while (iter && iter->pNext)
	{
		if ((iter->offset + iter->size) == iter->pNext->offset)
		{
			std3DBlock* to_merge = iter->pNext;
			iter->size += to_merge->size;
			iter->pNext = to_merge->pNext;
			std_pHS->free(to_merge);
		}
		else
		{
			iter = iter->pNext;
		}
	}
}

void* std3D_LockSurface(uint64_t handle)
{
	if (!std3D_device)
		return 0;

	uint32_t offset = (uint32_t)(handle & 0xFFFFFFFF);
	if (!std3D_vramMapped)
		std3D_MapGpuBuffer(&std3D_vram);
	++std3D_vramMapped;
	return (uint8_t*)std3D_vram.mapped.pData + offset;
}

void std3D_UnlockSurface(uint64_t handle)
{
	if (!std3D_device)
		return;
	if (std3D_vramMapped == 0)
	{
		stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Tried to unlock a surface when it wasn't locked.\n");
		return;
	}
	std3D_vramMapped--;
	if (std3D_vramMapped == 0)
		std3D_UnmapGpuBuffer(&std3D_vram);
	return;
}

void std3D_BlitSurface(uint64_t dst, const rdRect* dstRect, uint64_t src, const rdRect* srcRect, uint32_t transparentColor, int flags)
{
	if (!std3D_deviceContext || !std3D_pBlitConstants)
		return;

	// make sure vram isn't mapped
	if (std3D_vram.mapped.pData)
	{
		std3D_UnmapGpuBuffer(&std3D_vram);
		std3D_vramMapped = 0;
	}

	std3DBlitInfo copyInfo;
	copyInfo.SrcRectX = srcRect->x;
	copyInfo.SrcRectY = srcRect->y;
	copyInfo.SrcRectW = srcRect->width;
	copyInfo.SrcRectH = srcRect->height;

	copyInfo.DstRectX = dstRect->x;
	copyInfo.DstRectY = dstRect->y;
	copyInfo.DstRectW = dstRect->width;
	copyInfo.DstRectH = dstRect->height;

	copyInfo.Flags = flags;
	copyInfo.TransparentColor = transparentColor;
	copyInfo.SrcHandle = (uint32_t)(src >> 54);
	copyInfo.DstHandle = (uint32_t)(dst >> 54);
	
	ID3D11DeviceContext_UpdateSubresource(std3D_deviceContext, std3D_pBlitConstants, 0, NULL, &copyInfo, 0, 0);

	ID3D11DeviceContext_CSSetShader(std3D_deviceContext, std3D_pBlitShader, NULL, 0);
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &std3D_pBlitConstants);
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &std3D_descriptors.pShaderView);
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &std3D_vram.pUnorderedView, 0);
	ID3D11DeviceContext_Dispatch(std3D_deviceContext, (dstRect->width + 255) / 256, dstRect->height, 1);

	ID3D11Buffer* nullBuf[] = { NULL };
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &nullBuf);

	ID3D11UnorderedAccessView* nullUAV[] = { NULL };
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &nullUAV, 0);

	ID3D11ShaderResourceView* nullSRV[] = { NULL };
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &nullSRV);
}

void std3D_FillSurface(uint64_t dst, uint32_t fill, int dstWidth, int dstHeight, int dstStride, const rdRect* rect)
{
	if (!std3D_deviceContext || !std3D_pBlitConstants)
		return;

	// make sure vram isn't mapped
	if (std3D_vram.mapped.pData)
	{
		std3D_UnmapGpuBuffer(&std3D_vram);
		std3D_vramMapped = 0;
	}

	std3DFillInfo fillInfo;
	fillInfo.SrcRectX = rect->x;
	fillInfo.SrcRectY = rect->y;
	fillInfo.SrcRectW = rect->width;
	fillInfo.SrcRectH = rect->height;

	fillInfo.SrcHandle = (uint32_t)(dst >> 54);
	fillInfo.Fill = fill;

	ID3D11DeviceContext_UpdateSubresource(std3D_deviceContext, std3D_pFillConstants, 0, NULL, &fillInfo, 0, 0);

	ID3D11DeviceContext_CSSetShader(std3D_deviceContext, std3D_pFillShader, NULL, 0);
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &std3D_pFillConstants);
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &std3D_descriptors.pShaderView);
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &std3D_vram.pUnorderedView, 0);
	ID3D11DeviceContext_Dispatch(std3D_deviceContext, (rect->width + 255) / 256, rect->height, 1);

	ID3D11Buffer* nullBuf[] = { NULL };
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &nullBuf);

	ID3D11UnorderedAccessView* nullUAV[] = { NULL };
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &nullUAV, 0);

	ID3D11ShaderResourceView* nullSRV[] = { NULL };
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &nullSRV);
}

void std3D_Present(uint64_t src, int srcWidth, int srcHeight, int srcStride, const rdRect* dstRect)
{
	if (!std3D_deviceContext || !std3D_swapChain)
		return;

	std3D_VerifySwapchain();

	DXGI_SWAP_CHAIN_DESC sd;
	IDXGISwapChain_GetDesc(std3D_swapChain, &sd);

	UINT width = sd.BufferDesc.Width;
	UINT height = sd.BufferDesc.Height;

	std3DPresentInfo presentInfo;
	presentInfo.SrcAddress = src & 0xFFFFFFFF;
	presentInfo.SrcStride = srcWidth * srcStride;
	presentInfo.SrcWidth = srcWidth;
	presentInfo.SrcHeight = srcHeight;

	presentInfo.DstWidth = width;
	presentInfo.DstHeight = height;

	presentInfo.DstRectX = dstRect->x;
	presentInfo.DstRectY = dstRect->y;
	presentInfo.DstRectW = dstRect->width;
	presentInfo.DstRectH = dstRect->height;

	presentInfo.SrcHandle = (uint32_t)(src >> 54);

	// todo: move this to another function and call it from stdDisplay_setMasterPalette
	rdColor24* pal_master = (rdColor24*)stdDisplay_masterPalette;
	uint32_t* pal_write = std3D_MapGpuBuffer(&std3D_palette);
	for (int i = 0; i < 256; ++i)
	{
		rdColor24 rgb = pal_master[i];
		pal_write[i] = (rgb.r << 0) | (rgb.g << 8) | (rgb.b << 16) | (255 << 24);
	}
	std3D_UnmapGpuBuffer(&std3D_palette);

	ID3D11DeviceContext_UpdateSubresource(std3D_deviceContext, std3D_pPresentConstants, 0, NULL, &presentInfo, 0, 0);

	ID3D11ShaderResourceView* srvs[] = { std3D_vram.pShaderView, std3D_descriptors.pShaderView, std3D_palette.pShaderView };

	ID3D11DeviceContext_CSSetShader(std3D_deviceContext, std3D_pPresentShader, NULL, 0);
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &std3D_pPresentConstants);
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &std3D_backBufferUAV, 0);
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 3, srvs);
	ID3D11DeviceContext_Dispatch(std3D_deviceContext, (dstRect->width + 255) / 256, dstRect->height, 1);

	ID3D11Buffer* nullBuf[] = { NULL };
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &nullBuf);

	ID3D11UnorderedAccessView* nullUAV[] = { NULL };
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &nullUAV, 0);

	ID3D11ShaderResourceView* nullSRV[] = { NULL, NULL, NULL };
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 3, &nullSRV);

	std3D_Flip();
}

void std3D_FlushStreams(uint64_t colorDst, uint64_t depthDst, int dstWidth, int dstHeight, int colorDstStride, int depthDstStride, const rdRect* rect)
{
	if (!std3D_deviceContext || !std3D_pBlitConstants)
		return;

	// make sure vram isn't mapped
	if (std3D_vram.mapped.pData)
	{
		std3D_UnmapGpuBuffer(&std3D_vram);
		std3D_vramMapped = 0;
	}

	std3DRasterInfo rasterInfo;
	rasterInfo.ColorAddress = colorDst & 0xFFFFFFFF;
	rasterInfo.ColorStride = dstWidth * colorDstStride;
	rasterInfo.DepthAddress = depthDst & 0xFFFFFFFF;
	rasterInfo.DepthStride = dstWidth * depthDstStride;
	rasterInfo.Width = dstWidth;
	rasterInfo.Height = dstHeight;

	ID3D11DeviceContext_UpdateSubresource(std3D_deviceContext, std3D_pRasterConstants, 0, NULL, &rasterInfo, 0, 0);

	ID3D11DeviceContext_CSSetShader(std3D_deviceContext, std3D_pRasterShader, NULL, 0);
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &std3D_pRasterConstants);
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &std3D_descriptors.pShaderView);
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &std3D_vram.pUnorderedView, 0);
	if (rect)
		ID3D11DeviceContext_Dispatch(std3D_deviceContext, (rect->width + 255) / 256, rect->height, 1);
	else
		ID3D11DeviceContext_Dispatch(std3D_deviceContext, (dstWidth + 255) / 256, dstHeight, 1);

	ID3D11Buffer* nullBuf[] = { NULL };
	ID3D11DeviceContext_CSSetConstantBuffers(std3D_deviceContext, 0, 1, &nullBuf);

	ID3D11UnorderedAccessView* nullUAV[] = { NULL };
	ID3D11DeviceContext_CSSetUnorderedAccessViews(std3D_deviceContext, 0, 1, &nullUAV, 0);

	ID3D11ShaderResourceView* nullSRV[] = { NULL };
	ID3D11DeviceContext_CSSetShaderResources(std3D_deviceContext, 0, 1, &nullSRV);
}

#endif

// Palette
int std3D_SetCurrentPalette(rdColor24* a1, int a2)
{
	uint32_t* pal_write = std3D_MapGpuBuffer(&std3D_palette);
	for (int i = 0; i < 256; ++i)
	{
		rdColor24 rgb = a1[i];
		pal_write[i] = (rgb.r << 0) | (rgb.g << 8) | (rgb.b << 16) | (255 << 24);
	}
	std3D_UnmapGpuBuffer(&std3D_palette);
	return 1;
}

// Setup
int std3D_Startup()
{
	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_1 };
	int feature_level = 0;

	std3D_d3dDeviceCount = 0;
	for (int i = 0; i < ARRAY_SIZE(kDriverTypes); i++)
	{
		ID3D11Device* pDevice = NULL;
		ID3D11DeviceContext* pContext = NULL;
		HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &pDevice, (D3D_FEATURE_LEVEL*)&feature_level, &pContext);
		if (SUCCEEDED(hr))
		{
			d3d_device* dev = &std3D_d3dDevices[std3D_d3dDeviceCount++];
			memset(dev, 0, sizeof(*dev));
			strncpy_s(dev->deviceName, 128, kDriverTypes[i].name, 128);
			strncpy_s(dev->deviceDescription, 128, kDriverTypes[i].desc, 128);

			// todo: we should completely remove the old struct tbh
			dev->hasColorModel = 1;
			dev->dpcTri_hasperspectivecorrectttexturing = 1;
			dev->hasZBuffer = 1;
			dev->supportsColorKeyedTransparency = 1;
			dev->hasAlpha = 1;
			dev->hasAlphaFlatStippled = 0;
			dev->hasModulateAlpha = 1;
			dev->hasOnlySquareTexs = 0;

			dev->dcmColorModel = D3DCOLOR_RGB;
			dev->availableBitDepths = DDBD_8 | DDBD_16 | DDBD_24 | DDBD_32;
			dev->zCaps = D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL |
				D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_GREATER | D3DPCMPCAPS_NOTEQUAL |
				D3DPCMPCAPS_GREATEREQUAL;

			D3DDeviceDesc* dd = &dev->device_desc;
			dd->dwSize = sizeof(*dd);
			dd->dwFlags = D3DDD_COLORMODEL | D3DDD_DEVCAPS | D3DDD_TRANSFORMCAPS |
				D3DDD_CLIPPING | D3DDD_LIGHTINGCAPS | D3DDD_LINECAPS |
				D3DDD_TRICAPS | D3DDD_DEVICERENDERBITDEPTH |
				D3DDD_DEVICEZBUFFERBITDEPTH | D3DDD_MAXBUFFERSIZE |
				D3DDD_MAXVERTEXCOUNT;

			dd->dcmColorModel = D3DCOLOR_RGB;
			dd->dwDevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY |
				D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
				D3DDEVCAPS_TEXTUREVIDEOMEMORY |
				D3DDEVCAPS_DRAWPRIMITIVES2 |
				D3DDEVCAPS_CANRENDERAFTERFLIP |
				D3DDEVCAPS_HWRASTERIZATION |
				D3DDEVCAPS_HWTRANSFORMANDLIGHT;

			dd->dtcTransformCaps.dwSize = sizeof(D3DTRANSFORMCAPS);
			dd->dtcTransformCaps.dwCaps = 0; // Per doc, driver must set to 0

			dd->bClipping = TRUE;

			dd->dlcLightingCaps.dwSize = sizeof(D3DLIGHTINGCAPS);
			dd->dlcLightingCaps.dwCaps = D3DLIGHTCAPS_DIRECTIONAL |
				D3DLIGHTCAPS_POINT |
				D3DLIGHTCAPS_SPOT;
			dd->dlcLightingCaps.dwLightingModel = D3DLIGHTINGMODEL_RGB;
			dd->dlcLightingCaps.dwNumLights = 8;

			// Helper macro for prim caps
#define INIT_PRIMCAPS(caps) do { \
  caps.dwSize = sizeof(D3DPrimCaps); \
  caps.dwMiscCaps = D3DPMISCCAPS_MASKZ; \
  caps.dwRasterCaps = D3DPRASTERCAPS_DITHER; \
  caps.dwZCmpCaps = D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL | \
                    D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_GREATER | \
                    D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL; \
  caps.dwSrcBlendCaps = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE | \
                        D3DPBLENDCAPS_SRCCOLOR | D3DPBLENDCAPS_INVSRCCOLOR | \
                        D3DPBLENDCAPS_SRCALPHA; \
  caps.dwDestBlendCaps = caps.dwSrcBlendCaps; \
  caps.dwAlphaCmpCaps = D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL | \
                        D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_GREATER | \
                        D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL; \
  caps.dwShadeCaps = D3DPSHADECAPS_ALPHAFLATBLEND; \
  caps.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE; \
  caps.dwTextureFilterCaps = D3DPTFILTERCAPS_LINEAR; \
  caps.dwTextureBlendCaps = D3DPTBLENDCAPS_MODULATE; \
  caps.dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP; \
  caps.dwStippleWidth = 32; \
  caps.dwStippleHeight = 32; \
} while(0)

			INIT_PRIMCAPS(dd->dpcLineCaps);
			INIT_PRIMCAPS(dd->dpcTriCaps);

			dd->dwDeviceRenderBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
			dd->dwDeviceZBufferBitDepth = dd->dwDeviceRenderBitDepth;
			dd->dwMaxBufferSize = 0x400000;
			dd->dwMaxVertexCount = 65536;

			if (!d3d_device_ptr)
				d3d_device_ptr = dev;

			ID3D11Device_Release(pDevice);
			ID3D11DeviceContext_Release(pContext);
		}
	}
	return 1;
}

void std3D_Shutdown()
{
	std3D_ReleaseVertexStreams();
	std3D_ReleaseVRAM();
	std3D_FreeSwapChain();
	
	if (std3D_deviceContext)
		ID3D11DeviceContext_Release(std3D_deviceContext);
	std3D_deviceContext = NULL;

	if(std3D_device)
		ID3D11Device_Release(std3D_device);
	std3D_device = NULL;
}

void std3D_EnumerateDevices()
{
	IDXGIFactory* pFactory = NULL;
	if (FAILED(CreateDXGIFactory(&IID_IDXGIFactory, (void**)&pFactory)))
		return 0;

	IDXGIAdapter* pAdapter;
	for (UINT i = 0; IDXGIFactory_EnumAdapters(pFactory, i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		if (!pAdapter || stdDisplay_numDevices >= 16)
			break;

		stdVideoDevice* device3D = &stdDisplay_aDevices[stdDisplay_numDevices++];
		memset(device3D, 0, sizeof(stdVideoDevice));
		//typedef struct DXGI_ADAPTER_DESC {
		//	WCHAR  Description[128];
		//	UINT   VendorId;
		//	UINT   DeviceId;
		//	UINT   SubSysId;
		//	UINT   Revision;
		//	SIZE_T DedicatedVideoMemory;
		//	SIZE_T DedicatedSystemMemory;
		//	SIZE_T SharedSystemMemory;
		//	LUID   AdapterLuid;
		//} DXGI_ADAPTER_DESC;
		DXGI_ADAPTER_DESC desc;
		IDXGIAdapter_GetDesc(pAdapter, &desc);
		stdString_WcharToChar(device3D->driverDesc, desc.Description, 128);
		stdString_WcharToChar(device3D->driverName, desc.Description, 128);
		device3D->video_device[0].device_active = 1;
		device3D->video_device[0].hasGUID = 1;
		device3D->video_device[0].has3DAccel = 1;
		device3D->video_device[0].hasNoGuid = 0;
		device3D->video_device[0].dwVidMemTotal = desc.DedicatedVideoMemory;
		device3D->video_device[0].dwVidMemFree = desc.DedicatedSystemMemory;
		device3D->video_device[0].windowedMaybe = 0;
		device3D->guid.Data1 = desc.AdapterLuid.HighPart;
		device3D->guid.Data2 = desc.AdapterLuid.LowPart;
		device3D->adapter = pAdapter;
	}

	IDXGIFactory_Release(pFactory);
}

void std3D_EnumerateVideoModes(stdVideoDevice* device)
{
	if (!device->adapter)
		return;

	IDXGIOutput* pOutput = NULL;
	HRESULT hr = IDXGIAdapter_EnumOutputs((IDXGIAdapter*)device->adapter, 0, &pOutput);
	if (hr)
		return;

	UINT numModes = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	hr = IDXGIOutput_GetDisplayModeList(pOutput, format, DXGI_ENUM_MODES_SCALING, &numModes, NULL);

	DXGI_MODE_DESC* displayModes = std_pHS->alloc(sizeof(DXGI_MODE_DESC) * numModes);
	hr = IDXGIOutput_GetDisplayModeList(pOutput, format, 0, &numModes, displayModes);
	for (int i = 0; i < numModes; ++i)
	{
		if (stdDisplay_numVideoModes >= STD3D_MAX_VIDEO_MODES)
			break;

		// d3dtodo how do we want to handle this?
		if (displayModes[i].RefreshRate.Numerator != 60)
			continue;

		// since we're only checking for 32bit values and we're not actually planning on using the display modes as actual full screen modes
		// we can include a 16 bit mode here so that we can pick between 32 or 16 bit for the internal backbuffer
		int bpp = 8;
		int r_bits[3] = { 0,5,8 };
		int g_bits[3] = { 0,6,8 };
		int b_bits[3] = { 0,5,8 };
		int r_shift[3] = { 0, 11, 24 };
		int g_shift[3] = { 0, 5, 16 };
		int b_shift[3] = { 0, 0, 8 };
		for (int j = 0; j < 3; ++j)
		{
			stdVideoMode* mode = &Video_renderSurface[stdDisplay_numVideoModes++];
			mode->format.width = displayModes[i].Width;
			mode->format.width_in_pixels = displayModes[i].Width;
			mode->format.width_in_bytes = displayModes[i].Width * (bpp >> 3);
			mode->format.height = displayModes[i].Height;
			mode->format.texture_size_in_bytes = mode->format.width_in_bytes * mode->format.height;
			mode->format.format.colorMode = bpp == 8 ? STDCOLOR_PAL : STDCOLOR_RGBA;
			mode->format.format.bpp = bpp;
			mode->format.format.r_bits = r_bits[j];
			mode->format.format.g_bits = g_bits[j];
			mode->format.format.b_bits = b_bits[j];
			mode->format.format.r_shift = r_shift[j];
			mode->format.format.g_shift = g_shift[j];
			mode->format.format.b_shift = b_shift[j];
			mode->format.format.r_bitdiff = 0;
			mode->format.format.g_bitdiff = 0;
			mode->format.format.b_bitdiff = 0;
			mode->format.format.unk_40 = j == 2 ? 8 : 0;
			mode->format.format.unk_44 = 0;
			mode->format.format.unk_48 = 0;
			mode->aspectRatio = (flex_t)displayModes[i].Width / displayModes[i].Height;
			mode->field_0 = 1; // is this an "active" flag or a "3d" flag
			mode->refreshRate = (flex_t)displayModes[i].RefreshRate.Numerator / displayModes[i].RefreshRate.Denominator;

			bpp <<= 1;
		}
	}

	std_pHS->free(displayModes);
}

// Device context
int std3D_CreateDeviceContext()
{
	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_1 };
	int feature_level = 0;
	
	ID3D11Device* d3d11Device;
	ID3D11DeviceContext* d3d11DeviceContext;

	HRESULT res;
	if ((res = D3D11CreateDevice(/*(IDXGIAdapter*)stdDisplay_pCurDevice->adapter*/0, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &d3d11Device, (D3D_FEATURE_LEVEL*)&feature_level, &d3d11DeviceContext)) != S_OK)
	{
		const char* reason = "";
		if (res == E_FAIL)
			reason = "failed to create a device with the debug layer enabled and the layer is not installed";
		else if (res == E_INVALIDARG)
			reason = "an invalid parameter was passed to the returning function";
		stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error: Failed to create d3d device, reason: %s\n", reason);
		return 0;
	}

	ID3D11Device_QueryInterface(d3d11Device, &IID_ID3D11Device1, (void**)&std3D_device);
	ID3D11Device_Release(d3d11Device);
		
	ID3D11DeviceContext_QueryInterface(d3d11DeviceContext, &IID_ID3D11DeviceContext1, (void**)&std3D_deviceContext);
	ID3D11DeviceContext_Release(d3d11DeviceContext);

	if (!std3D_device || !std3D_deviceContext)
		return 0;

	if(!std3D_CreateConstantBuffer(&std3D_pBlitConstants, sizeof(std3DBlitInfo)))
	   return 0;
	if(!std3D_CreateComputeShader(&std3D_pBlitShader, "hlsl/BlitCS.cso"))
		return 0;
	if (!std3D_CreateConstantBuffer(&std3D_pFillConstants, sizeof(std3DFillInfo)))
		return 0;
	if (!std3D_CreateComputeShader(&std3D_pFillShader, "hlsl/FillCS.cso"))
		return 0;
	if (!std3D_CreateConstantBuffer(&std3D_pPresentConstants, sizeof(std3DPresentInfo)))
		return 0;
	if (!std3D_CreateComputeShader(&std3D_pPresentShader, "hlsl/PresentCS.cso"))
		return 0;
	if (!std3D_CreateConstantBuffer(&std3D_pRasterConstants, sizeof(std3DRasterInfo)))
		return 0;
	if (!std3D_CreateComputeShader(&std3D_pRasterShader, "hlsl/RasterCS.cso"))
		return 0;

	std3D_CreateVertexStreams();

	std3D_CreateGpuBuffer(&std3D_descriptors, STD3D_MAX_DESCRIPTORS, sizeof(std3DDescriptor), DXGI_FORMAT_UNKNOWN, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED);

	return std3D_CreateVRAM();
}


void std3D_DestroyDeviceContext()
{
	std3D_ReleaseConstantBuffer(&std3D_pBlitConstants);
	std3D_ReleaseComputeShader(&std3D_pBlitShader);
	std3D_ReleaseConstantBuffer(&std3D_pFillConstants);
	std3D_ReleaseComputeShader(&std3D_pFillShader);
	std3D_ReleaseConstantBuffer(&std3D_pPresentConstants);
	std3D_ReleaseComputeShader(&std3D_pPresentShader);
	std3D_ReleaseConstantBuffer(&std3D_pRasterConstants);
	std3D_ReleaseComputeShader(&std3D_pRasterShader);

	std3D_ReleaseGpuBuffer(&std3D_descriptors);

	std3D_ReleaseVertexStreams();
	std3D_ReleaseVRAM();

	stdPlatform_Printf("Destroying swap chain\n");

	std3D_FreeSwapChain();

	if (std3D_deviceContext)
		ID3D11DeviceContext_Release(std3D_deviceContext);
	std3D_deviceContext = NULL;

	if (std3D_device)
		ID3D11Device_Release(std3D_device);
	std3D_device = NULL;
}

// Swap chain
int std3D_CreateSwapChain()
{
	if (!std3D_device)
		return 0;

	if (std3D_swapChain)
		return 1;

	IDXGIFactory* pFactory = NULL;
	if (FAILED(CreateDXGIFactory(&IID_IDXGIFactory, (void**)&pFactory)))
		return 0;

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo((SDL_Window*)stdGdi_GetHwnd(), &wmInfo);
	HWND hwnd = wmInfo.info.win.window;

	std3D_swapWindow = stdGdi_GetHwnd();

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;// DXGI_SWAP_EFFECT_DISCARD; // not using flip model because we need GDI for software render (otherwise we get a black screen)

	HRESULT res;
	if ((res = IDXGIFactory_CreateSwapChain(pFactory, std3D_device, &sd, &std3D_swapChain)) != S_OK)
	{
		stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error: Failed to create swap chain\n");

		IDXGIFactory_Release(pFactory);
		return 0;
	}
	IDXGIFactory_Release(pFactory);

	ID3D11Texture2D* pBackBuffer;
	IDXGISwapChain_GetBuffer(std3D_swapChain, 0, &IID_ID3D11Texture2D, &pBackBuffer);
	ID3D11Device_CreateUnorderedAccessView(std3D_device, pBackBuffer, NULL, &std3D_backBufferUAV);
	ID3D11Texture2D_Release(pBackBuffer);

	stdPlatform_Printf("Created swap chain\n");
	return 1;
}

void std3D_FreeSwapChain()
{
	stdPlatform_Printf("Destroying swap chain\n");

	if (std3D_backBufferUAV)
		ID3D11UnorderedAccessView_Release(std3D_backBufferUAV);
	std3D_backBufferUAV = NULL;

	if (std3D_swapChain)
		IDXGISwapChain_Release(std3D_swapChain);
	std3D_swapChain = NULL;
}


void std3D_VerifySwapchain()
{
	if(std3D_swapWindow != stdGdi_GetHwnd())
	{
		std3D_FreeSwapChain();
		std3D_CreateSwapChain();
	}
}

void std3D_ResizeViewport(int w, int h)
{
	if (!std3D_swapChain)
		return;

	std3D_VerifySwapchain();
	
	if (std3D_backBufferUAV)
		ID3D11UnorderedAccessView_Release(std3D_backBufferUAV);
	std3D_backBufferUAV = NULL;

	stdPlatform_Printf("Resizing swap chain\n");

	HRESULT res = IDXGISwapChain_ResizeBuffers(std3D_swapChain, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
	if (res != S_OK)
	{
		stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error: Failed to resize swap chain\n");
		return;
	}
	ID3D11Texture2D* pBackBuffer;
	IDXGISwapChain_GetBuffer(std3D_swapChain, 0, &IID_ID3D11Texture2D, &pBackBuffer);
	ID3D11Device_CreateUnorderedAccessView(std3D_device, pBackBuffer, NULL, &std3D_backBufferUAV);
	ID3D11Texture2D_Release(pBackBuffer);
}

void std3D_Flip()
{
	std3D_VerifySwapchain();
	//stdPlatform_Printf("Flipping swap chain\n");
	if(std3D_swapChain)
		IDXGISwapChain_Present(std3D_swapChain, 0, DXGI_PRESENT_ALLOW_TEARING);
}


int std3D_StartScene(){}
int std3D_EndScene() {}

int std3D_IsReady() { return std3D_device != NULL; }

int std3D_HasAlpha()
{
	return 1;
}

int std3D_HasModulateAlpha()
{
	return 1;
}

int std3D_HasAlphaFlatStippled()
{
	return 1;
}

void std3D_InitializeViewport(rdRect* viewRect)
{
	// d3dtodo do we want to store a viewport value here? what does this normally do?
}



int std3D_DrawOverlay() { return 0; }

void std3D_Screenshot(const char* pFpath) {}
void std3D_UpdateSettings() {}

// d3dtodo the render lists would be the precomputed triangle setup from the tiled rasterizer?
void std3D_ResetRenderList() {}


int std3D_GetValidDimensions(int a1, int a2, int a3, int a4)
{
	int result; // eax

	std3D_gpuMaxTexSizeMaybe = a1;
	result = a4;
	std3D_dword_53D66C = a2;
	std3D_dword_53D670 = a3;
	std3D_dword_53D674 = a4;
	return result;
}
int std3D_FindClosestDevice(uint32_t index, int a2)
{
	if (index >= std3D_d3dDeviceCount || !std3D_device)
		return 0;
	
	d3d_device_ptr = &std3D_d3dDevices[index];
	std3D_CreateSwapChain();

	return 1;
}

int std3D_SetRenderList(intptr_t a1)
{
	std3D_renderList = a1;
	return 1;
}

intptr_t std3D_GetRenderList()
{
	return std3D_renderList;
}


void std3D_FreeResources()
{
}

#else

#endif

#else

// Added helpers
int std3D_HasAlpha()
{
    return d3d_device_ptr->hasAlpha;
}

int std3D_HasModulateAlpha()
{
    return d3d_device_ptr->hasModulateAlpha;
}

int std3D_HasAlphaFlatStippled()
{
    return d3d_device_ptr->hasAlphaFlatStippled;
}

void std3D_InitializeViewport(rdRect *viewRect)
{
    signed int v1; // ebx
    signed int height; // ebp

    flex_t viewXMax_2; // [esp+14h] [ebp+4h]
    flex_t viewRectYMax; // [esp+14h] [ebp+4h]

    std3D_rectViewIdk.x = viewRect->x;
    v1 = viewRect->width;
    std3D_rectViewIdk.y = viewRect->y;
    std3D_rectViewIdk.width = v1;
    height = viewRect->height;
    memset(std3D_aViewIdk, 0, sizeof(std3D_aViewIdk));
    std3D_aViewIdk[0] = (flex_t)std3D_rectViewIdk.x; // FLEXTODO
    std3D_aViewIdk[1] = (flex_t)std3D_rectViewIdk.y; // FLEXTODO
    std3D_rectViewIdk.height = height;
    std3D_aViewTris[0].v1 = 0;
    std3D_aViewTris[0].v2 = 1;
    viewXMax_2 = (flex_t)(v1 + std3D_rectViewIdk.x); // FLEXTODO
    std3D_aViewIdk[8] = viewXMax_2;
    std3D_aViewIdk[9] = std3D_aViewIdk[1];
    std3D_aViewIdk[16] = viewXMax_2;
    viewRectYMax = (flex_t)(height + std3D_rectViewIdk.y); // FLEXTODO
    std3D_aViewTris[0].texture = 0;
    std3D_aViewIdk[17] = viewRectYMax;
    std3D_aViewIdk[25] = viewRectYMax;
    std3D_aViewIdk[24] = std3D_aViewIdk[0];
    std3D_aViewTris[0].v3 = 2;
    std3D_aViewTris[0].flags = 0x8200;
    std3D_aViewTris[1].v1 = 0;
    std3D_aViewTris[1].v2 = 2;
    std3D_aViewTris[1].v3 = 3;
    std3D_aViewTris[1].texture = 0;
    std3D_aViewTris[1].flags = 0x8200;
}

int std3D_GetValidDimensions(int a1, int a2, int a3, int a4)
{
    int result; // eax

    std3D_gpuMaxTexSizeMaybe = a1;
    result = a4;
    std3D_dword_53D66C = a2;
    std3D_dword_53D670 = a3;
    std3D_dword_53D674 = a4;
    return result;
}

int std3D_SetRenderList(intptr_t a1)
{
    std3D_renderList = a1;
    return std3D_CreateExecuteBuffer();
}

intptr_t std3D_GetRenderList()
{
    return std3D_renderList;
}

#endif // TILE_SW_RASTER

#endif // !SDL2_RENDER