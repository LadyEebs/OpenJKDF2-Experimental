#include "stdDisplay.h"

#include "std.h"
#include "stdPlatform.h"
#include "jk.h"
#include "Win95/Video.h"
#include "Win95/Window.h"
#include "General/stdColor.h"

#include "Platform/std3D.h"
#include "Modules/std/stdJob.h"
#include "Modules/std/stdProfiler.h"

#include "SDL.h"
#include <SDL_syswm.h>

#ifdef TILE_SW_RASTER
rdColor24 stdDisplay_paletteCube[STDPAL_CUBE_SIZE];
rdColor24 stdDisplay_masterPaletteCube[STDPAL_CUBE_SIZE];
#endif

void stdDisplay_SetGammaTable(int len, flex_d_t *table)
{
    stdDisplay_gammaTableLen = len;
    stdDisplay_paGammaTable = table;
}

uint8_t* stdDisplay_GetPalette()
{
    return (uint8_t*)stdDisplay_gammaPalette;
}

#ifdef TILE_SW_RASTER
uint8_t* stdDisplay_GetPaletteCube()
{
	return (uint8_t*)stdDisplay_paletteCube;
}

void stdDisplay_BuildIdentityPaletteCube()
{
	for (int b = 0; b < 16; ++b)
	{
		for (int g = 0; g < 16; ++g)
		{
			for (int r = 0; r < 16; ++r)
			{
				rdColor24 rgb = {r << 4, g << 4, b << 4};
				stdDisplay_paletteCube[(b << 8) | (g << 4) | r] = rgb;
			}
		}
	}
}
#endif

int stdDisplay_SortVideoModes(stdVideoMode* modeA, stdVideoMode* modeB)
{
	// Prioritize active modes
	if (modeA->field_0 == 0 && modeB->field_0 != 0)
	{
		return 1; // B comes before A
	}
	if (modeA->field_0 != 0 && modeB->field_0 == 0)
	{
		return -1; // A comes before B
	}

	// If both inactive or both active, compare by bit depth
	uint32_t bppA = modeA->format.format.bpp;
	uint32_t bppB = modeB->format.format.bpp;
	if (bppA != bppB)
	{
		return bppA - bppB;
	}

	// Then compare width
	int widthA = modeA->format.width;
	int widthB = modeB->format.width;
	if (widthA != widthB)
	{
		return widthA - widthB;
	}

	// Finally compare height
	return modeA->format.height - modeB->format.height;
}

// Added (from openjones)
static void stdDisplay_SetPixels16(uint16_t* pPixels16, uint16_t pixel, size_t size)
{
	int v3;
	uint16_t* pCurPixel;
	unsigned int count;
	int v6;

	pCurPixel = pPixels16;
	count = size;
	if ((size & 1) != 0)
	{
		while (count)
		{
			*pCurPixel++ = pixel;
			--count;
		}
	}
	else
	{
		// The size is multiple of 2 so we can copy 2 pixels (32 bit) at a time
		v3 = (uint16_t)(pixel & 0xFFFFU);
		v6 = v3 << 16;
		v6 |= (uint16_t)(pixel & 0xFFFFU);
		memset(pPixels16, v6, (size / 2) * sizeof(int)); // Note, divide by 2 because 2 copy 2 uint16_t at a time in form of 32 bit number 
	}
}

// Added (from openjones)
static void stdDisplay_SetPixels32(uint32_t* pPixels32, uint32_t pixel, size_t size)
{
	memset(pPixels32, pixel, size * sizeof(pixel));
}

#ifndef SDL2_RENDER

#else
#include "SDL2_helper.h"
#include <assert.h>

uint32_t Video_menuTexId = 0;
uint32_t Video_overlayTexId = 0;
rdColor24 stdDisplay_masterPalette[256];
int Video_bModeSet = 0;
int stdDisplay_numDevices;

static struct SDL_Color stdDisplay_paletteScratch[256] = { 0 };

int stdDisplay_lastDisplayIdx = 0;

int stdDisplay_Startup()
{
    stdDisplay_bStartup = 1;
	stdDisplay_numDevices = 1;
#ifdef TILE_SW_RASTER
	Video_dword_866D78 = 0;

	stdVideoDevice* device = &stdDisplay_aDevices[0];
	stdDisplay_numDevices = 0;

	// Hardcoded fallback GUID (no real GUID)
	//device->guid.Data1 = 0x92a69f00;
	//device->guid.Data2 = 0x11d113fa;
	//device->guid.Data3 = 0xa000c097;
	//device->guid.Data4 = 0x05302924;

	// Driver description and name
	_strncpy(device->driverDesc, "Window Display Device", 0x7F);
	device->driverDesc[0x7F] = '\0';

	_strncpy(device->driverName, "Windowed display", 0x7F); // DIBsection windowed display
	device->driverName[0x7F] = '\0';

	// Fallback "windowed" video device
	video_device* vdev = &device->video_device[0];
	vdev->device_active = 0;
	vdev->hasGUID = 0;
	vdev->has3DAccel = 0;
	vdev->hasNoGuid = 1;
	vdev->windowedMaybe = 0;
	vdev->dwVidMemTotal = 2000000;
	vdev->dwVidMemFree = 2000000;

	stdDisplay_numDevices = 1;
	std3D_EnumerateDevices();

	stdDisplay_BuildIdentityPaletteCube();

#endif
    return 1;
}

int stdDisplay_FindClosestDevice(stdDeviceParams* a)
{
	stdPlatform_Printf("Returning device %d\n", stdDisplay_lastDisplayIdx);

    //Video_dword_866D78 = 0;
	//stdDisplay_Open(Video_dword_866D78);
#ifdef TILE_SW_RASTER
	//std3D_FindClosestDevice(stdDisplay_lastDisplayIdx,0);
#endif
    return stdDisplay_lastDisplayIdx;
}

void stdDisplay_CreateDefaultModes()
{
	Video_renderSurface[2].format.width = 400;
	Video_renderSurface[2].format.height = 400;
	Video_renderSurface[2].format.width_in_bytes = 400;
	Video_renderSurface[2].format.width_in_pixels = 400;
	Video_renderSurface[3].format.width = 512;
	Video_renderSurface[3].format.width_in_bytes = 512;
	Video_renderSurface[3].format.width_in_pixels = 512;
	Video_renderSurface[0].field_0 = 0;
	Video_renderSurface[0].format.width = 320;
	Video_renderSurface[0].format.height = 200;
	Video_renderSurface[0].format.texture_size_in_bytes = 64000;
	Video_renderSurface[0].format.width_in_bytes = 320;
	Video_renderSurface[0].format.width_in_pixels = 320;
	Video_renderSurface[0].format.format.colorMode = 0;
	Video_renderSurface[0].format.format.bpp = 8;
	Video_renderSurface[0].format.format.r_bits = 0;
	Video_renderSurface[0].format.format.g_bits = 0;
	Video_renderSurface[0].format.format.b_bits = 0;
	Video_renderSurface[0].aspectRatio = 0.75;
	Video_renderSurface[1].field_0 = 0;
	Video_renderSurface[1].format.width = 320;
	Video_renderSurface[1].format.height = 240;
	Video_renderSurface[1].format.texture_size_in_bytes = 0x12c00;
	Video_renderSurface[1].format.width_in_bytes = 320;
	Video_renderSurface[1].format.width_in_pixels = 320;
	Video_renderSurface[1].format.format.colorMode = 0;
	Video_renderSurface[1].format.format.bpp = 8;
	Video_renderSurface[1].format.format.r_bits = 0;
	Video_renderSurface[1].format.format.g_bits = 0;
	Video_renderSurface[1].format.format.b_bits = 0;
	Video_renderSurface[1].aspectRatio = 1.0;
	Video_renderSurface[2].field_0 = 0;
	Video_renderSurface[2].format.texture_size_in_bytes = 160000;
	Video_renderSurface[2].format.format.colorMode = 0;
	Video_renderSurface[2].format.format.bpp = 8;
	Video_renderSurface[2].format.format.r_bits = 0;
	Video_renderSurface[2].format.format.g_bits = 0;
	Video_renderSurface[2].format.format.b_bits = 0;
	Video_renderSurface[2].aspectRatio = 1.0;
	Video_renderSurface[3].field_0 = 0;
	Video_renderSurface[3].format.height = 384;
	Video_renderSurface[3].format.texture_size_in_bytes = 0x30000;
	Video_renderSurface[3].format.format.colorMode = 0;
	Video_renderSurface[3].format.format.bpp = 8;
	Video_renderSurface[3].format.format.r_bits = 0;
	Video_renderSurface[3].format.format.g_bits = 0;
	Video_renderSurface[3].format.format.b_bits = 0;
	Video_renderSurface[3].aspectRatio = 1.0;
	Video_renderSurface[4].field_0 = 0;
	Video_renderSurface[4].format.width = 640;
	Video_renderSurface[4].format.height = 480;
	Video_renderSurface[4].format.texture_size_in_bytes = 0x4b000;
	Video_renderSurface[4].format.width_in_bytes = 640;
	Video_renderSurface[4].format.width_in_pixels = 640;
	Video_renderSurface[4].format.format.colorMode = 0;
	Video_renderSurface[4].format.format.bpp = 8;
	Video_renderSurface[4].format.format.r_bits = 0;
	Video_renderSurface[4].format.format.g_bits = 0;
	Video_renderSurface[4].format.format.b_bits = 0;
	Video_renderSurface[4].aspectRatio = 1.0;
	Video_renderSurface[5].field_0 = 0;
	Video_renderSurface[5].format.width = 1024;
	Video_renderSurface[5].format.height = 768;
	Video_renderSurface[5].format.texture_size_in_bytes = 0xc0000;
	Video_renderSurface[5].format.width_in_bytes = 1024;
	Video_renderSurface[5].format.width_in_pixels = 1024;
	Video_renderSurface[5].format.format.colorMode = 0;
	Video_renderSurface[5].format.format.bpp = 8;
	Video_renderSurface[5].format.format.r_bits = 0;
	Video_renderSurface[5].format.format.g_bits = 0;
	Video_renderSurface[5].format.format.b_bits = 0;
	Video_renderSurface[5].aspectRatio = 1.0;
	Video_renderSurface[6].field_0 = 0;
	Video_renderSurface[6].format.width = 1280;
	Video_renderSurface[6].format.height = 1024;
	Video_renderSurface[6].format.format.bpp = 8;
	Video_renderSurface[7].format.width = 320;
	Video_renderSurface[7].format.width_in_pixels = 320;
	Video_renderSurface[6].format.texture_size_in_bytes = 0x140000;
	Video_renderSurface[6].format.width_in_bytes = 1280;
	Video_renderSurface[6].format.width_in_pixels = 1280;
	Video_renderSurface[6].format.format.colorMode = 0;
	Video_renderSurface[6].format.format.r_bits = 0;
	Video_renderSurface[6].format.format.g_bits = 0;
	Video_renderSurface[6].format.format.b_bits = 0;
	Video_renderSurface[6].aspectRatio = 1.0;
	Video_renderSurface[7].field_0 = 0;
	Video_renderSurface[7].format.height = 240;
	Video_renderSurface[7].format.texture_size_in_bytes = 0x25800;
	Video_renderSurface[7].format.width_in_bytes = 640;
	Video_renderSurface[7].format.format.colorMode = 1;
	Video_renderSurface[7].format.format.bpp = 16;
	Video_renderSurface[7].format.format.r_bits = 5;
	Video_renderSurface[7].format.format.g_bits = 6;
	Video_renderSurface[7].format.format.b_bits = 5;
	Video_renderSurface[7].format.format.r_bitdiff = 3;
	Video_renderSurface[7].format.format.g_bitdiff = 2;
	Video_renderSurface[7].format.format.b_bitdiff = 3;
	Video_renderSurface[7].format.format.r_shift = 11;
	Video_renderSurface[7].format.format.g_shift = 5;
	Video_renderSurface[7].format.format.b_shift = 0;
	Video_renderSurface[7].aspectRatio = 1.0;
	Video_renderSurface[8].field_0 = 0;
	Video_renderSurface[8].format.width = 640;
	Video_renderSurface[8].format.height = 480;
	Video_renderSurface[8].format.texture_size_in_bytes = 0x96000;
	Video_renderSurface[8].format.width_in_bytes = 1280;
	Video_renderSurface[8].format.width_in_pixels = 640;
	Video_renderSurface[8].format.format.colorMode = 1;
	Video_renderSurface[8].format.format.bpp = 16;
	Video_renderSurface[8].format.format.r_bits = 5;
	Video_renderSurface[8].format.format.g_bits = 6;
	Video_renderSurface[8].format.format.b_bits = 5;
	Video_renderSurface[8].format.format.r_bitdiff = 3;
	Video_renderSurface[8].format.format.g_bitdiff = 2;
	Video_renderSurface[8].format.format.b_bitdiff = 3;
	Video_renderSurface[8].format.format.r_shift = 11;
	Video_renderSurface[8].format.format.g_shift = 5;
	Video_renderSurface[8].format.format.b_shift = 0;
	Video_renderSurface[8].aspectRatio = 1.0;
	// Added
	Video_renderSurface[9].field_0 = 0;
	Video_renderSurface[9].format.width = 1280;
	Video_renderSurface[9].format.height = 720;
	Video_renderSurface[9].format.texture_size_in_bytes = 1280 * 720;
	Video_renderSurface[9].format.width_in_bytes = 1280;
	Video_renderSurface[9].format.width_in_pixels = 1280;
	Video_renderSurface[9].format.format.colorMode = 0;
	Video_renderSurface[9].format.format.bpp = 8;
	Video_renderSurface[9].format.format.r_bits = 0;
	Video_renderSurface[9].format.format.g_bits = 0;
	Video_renderSurface[9].format.format.b_bits = 0;
	Video_renderSurface[9].format.format.r_bitdiff = 0;
	Video_renderSurface[9].format.format.g_bitdiff = 0;
	Video_renderSurface[9].format.format.b_bitdiff = 0;
	Video_renderSurface[9].format.format.r_shift = 0;
	Video_renderSurface[9].format.format.g_shift = 0;
	Video_renderSurface[9].format.format.b_shift = 0;
	Video_renderSurface[9].aspectRatio = 1.0;//1280.0/720.0;

	Video_renderSurface[10].field_0 = 0;
	Video_renderSurface[10].format.width = 1920;
	Video_renderSurface[10].format.height = 1080;
	Video_renderSurface[10].format.texture_size_in_bytes = 1920 * 1080;
	Video_renderSurface[10].format.width_in_bytes = 1920;
	Video_renderSurface[10].format.width_in_pixels = 1920;
	Video_renderSurface[10].format.format.colorMode = 0;
	Video_renderSurface[10].format.format.bpp = 8;
	Video_renderSurface[10].format.format.r_bits = 0;
	Video_renderSurface[10].format.format.g_bits = 0;
	Video_renderSurface[10].format.format.b_bits = 0;
	Video_renderSurface[10].format.format.r_bitdiff = 0;
	Video_renderSurface[10].format.format.g_bitdiff = 0;
	Video_renderSurface[10].format.format.b_bitdiff = 0;
	Video_renderSurface[10].format.format.r_shift = 0;
	Video_renderSurface[10].format.format.g_shift = 0;
	Video_renderSurface[10].format.format.b_shift = 0;
	Video_renderSurface[10].aspectRatio = 1.0;//1280.0/720.0;


	Video_renderSurface[11].field_0 = 0;
	Video_renderSurface[11].format.width = 1920;
	Video_renderSurface[11].format.height = 1080;
	Video_renderSurface[11].format.texture_size_in_bytes = 1920 * 1080;
	Video_renderSurface[11].format.width_in_bytes = 1920;//*2;
	Video_renderSurface[11].format.width_in_pixels = 1920;
	Video_renderSurface[11].format.format.colorMode = 0;
	Video_renderSurface[11].format.format.bpp = 8;//16;
	Video_renderSurface[11].format.format.r_bits = 5;
	Video_renderSurface[11].format.format.g_bits = 6;
	Video_renderSurface[11].format.format.b_bits = 5;
	Video_renderSurface[11].format.format.r_bitdiff = 3;
	Video_renderSurface[11].format.format.g_bitdiff = 2;
	Video_renderSurface[11].format.format.b_bitdiff = 3;
	Video_renderSurface[11].format.format.r_shift = 11;
	Video_renderSurface[11].format.format.g_shift = 5;
	Video_renderSurface[11].format.format.b_shift = 0;
	Video_renderSurface[11].aspectRatio = 1.0;//1280.0/720.0;

	stdDisplay_numVideoModes = 12;
}

int stdDisplay_Open(int index)
{
	if (stdDisplay_bOpen != 0)
		return 0;
	
	if (stdDisplay_numDevices <= index)
		return 0;
	
	stdPlatform_Printf("Opening display device %d\n", index);

#ifdef TILE_SW_RASTER
	HWND HVar1;
	int iVar2;
	
	stdDisplay_lastDisplayIdx = index;
	stdDisplay_pCurDevice = &stdDisplay_aDevices[index];
	iVar2 = stdDisplay_aDevices[index].video_device[0].device_active;
	
	stdDisplay_numVideoModes = 0;
	if (iVar2 == 0)
	{
		stdDisplay_CreateDefaultModes();
	}
	else if (iVar2 == 1)
	{
		// Originally JK init DDraw here?
		std3D_EnumerateVideoModes(stdDisplay_pCurDevice);
		if(!stdDisplay_numVideoModes)
			stdDisplay_CreateDefaultModes();

		if(!std3D_CreateDeviceContext())
			return 0;
	}
	qsort(Video_renderSurface, stdDisplay_numVideoModes, sizeof(stdVideoMode), stdDisplay_SortVideoModes);
	stdPlatform_Printf("Successfully opened display device %d\n", index);

	//stdDisplay_RestoreSurfaces();
#else
	stdDisplay_pCurDevice = &stdDisplay_aDevices[0];
	stdDisplay_bOpen = 1;
#endif
    return 1;
}

void stdDisplay_Close()
{
	if (!stdDisplay_bOpen)
		return;

	stdPlatform_Printf("Opening display device %d?\n", stdDisplay_lastDisplayIdx);

	//Video_dword_866D78 = 0;
#ifdef TILE_SW_RASTER
	std3D_DestroyDeviceContext();
	stdDisplay_FreeBackBuffers();
#endif
    stdDisplay_bOpen = 0;
}

int stdDisplay_FindClosestMode(render_pair *target, struct stdVideoMode *modes, unsigned int max_modes)
{
#ifdef TILE_SW_RASTER
	int bestIndex = 0;
	int bestScore = 0;
	
	if (stdDisplay_numVideoModes == 0 || max_modes == 0)
		return bestIndex;

	for (int i = 0; i < max_modes; i++)
	{
		stdVBufferTexFmt* fmt = &modes[i].format;
		int score = 0;

		// Check if is16bit matches (assuming colorMode encodes is16bit info)
		int modeIs16bit = (fmt->format.colorMode != STDCOLOR_PAL)? 1 : 0;
		if (modeIs16bit == target->render_8bpp.palBytes)
		{
			score = 1;

			if (fmt->format.bpp == target->render_rgb.bpp)
			{
				score = 2;

				if (fmt->width == target->render_8bpp.width)
				{
					score = 3;

					if (fmt->height == target->render_8bpp.height)
					{
						score = 4;

						if (target->render_8bpp.palBytes != 1)
						{
							// Perfect match for non-16bit
							return i;
						}

						// For 16-bit mode, check RGB bits too
						if (fmt->format.r_bits == target->render_rgb.rBpp &&
							fmt->format.g_bits == target->render_rgb.gBpp &&
							fmt->format.b_bits == target->render_rgb.bBpp)
						{
							// Perfect 16bit RGB match
							return i;
						}
					}
				}
			}
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}
	return bestIndex;
#else
	stdDisplay_bPaged = 1;
	stdDisplay_bModeSet = 1;
    Video_curMode = 0;
	return 0;
#endif
}

int stdDisplay_SetMode(unsigned int modeIdx, const void *palette, int paged)
{
#ifdef TILE_SW_RASTER
	if (modeIdx >= stdDisplay_numVideoModes)
		return 0;

	stdDisplay_FreeBackBuffers();

	stdDisplay_bPaged = paged;
	Video_curMode = modeIdx;
	stdDisplay_pCurVideoMode = &Video_renderSurface[modeIdx];

	//SDL_Surface* otherSurface = SDL_CreateRGBSurface(0, stdDisplay_pCurVideoMode->format.width, stdDisplay_pCurVideoMode->format.height, 8,
	//												 0,
	//												 0,
	//												 0,
	//												 0);
	//SDL_Surface* overlaySurface = SDL_CreateRGBSurface(0, stdDisplay_pCurVideoMode->format.width, stdDisplay_pCurVideoMode->format.height, 8, 0, 0, 0, 0);
	
	if (stdDisplay_pCurDevice->video_device[0].device_active)
	{
		uint64_t handle = std3D_CreateSurface(&stdDisplay_pCurVideoMode->format);
		Video_otherBuf.gpuHandle = handle;
		Video_menuBuffer.gpuHandle = handle;
		Video_menuBuffer.bSurfaceLocked = 1;
		Video_otherBuf.bSurfaceLocked = 1;
	}
	else
	{
		Video_menuBuffer.bSurfaceLocked = 0;
		Video_otherBuf.bSurfaceLocked = 0;
		Video_menuBuffer.surface_lock_alloc = _mm_malloc(stdDisplay_pCurVideoMode->format.width_in_bytes * stdDisplay_pCurVideoMode->format.height, 16);
		Video_otherBuf.surface_lock_alloc = Video_menuBuffer.surface_lock_alloc;
	}
	Video_otherBuf.lock_cnt = 0;
	Video_menuBuffer.lock_cnt = 0;

	if (palette)
	{
		memcpy(stdDisplay_tmpGammaPal, palette, 0x300);
		if (stdDisplay_paGammaTable != NULL && stdDisplay_gammaIdx != 0)
		{
			stdColor_GammaCorrect(&stdDisplay_gammaPalette[0].r, stdDisplay_tmpGammaPal, 256,
								  stdDisplay_paGammaTable[stdDisplay_gammaIdx - 1]);
		}
		else
		{
			memcpy(stdDisplay_gammaPalette, palette, 0x300);
		}

		//const rdColor24* pal24 = (const rdColor24*)palette;
		//SDL_Color* tmp = stdDisplay_paletteScratch;
		//for (int i = 0; i < 256; i++)
		//{
		//	tmp[i].r = pal24[i].r;
		//	tmp[i].g = pal24[i].g;
		//	tmp[i].b = pal24[i].b;
		//	tmp[i].a = 0xFF;
		//}
		//
		//SDL_SetPaletteColors(otherSurface->format->palette, tmp, 0, 256);
		//SDL_SetPaletteColors(overlaySurface->format->palette, tmp, 0, 256);
	}

	//Video_otherBuf.sdlSurface = otherSurface;
	//Video_menuBuffer.sdlSurface = otherSurface;

	_memcpy(&Video_otherBuf.format, &stdDisplay_pCurVideoMode->format, sizeof(Video_otherBuf.format));
	_memcpy(&Video_menuBuffer.format, &stdDisplay_pCurVideoMode->format, sizeof(Video_menuBuffer.format));

	Video_otherBuf.palette = palette;
	Video_menuBuffer.palette = palette;

	stdDisplay_VBufferFill(&Video_menuBuffer, 0, NULL);

	Video_pMenuBuffer = &Video_menuBuffer;
	Video_pOtherBuf = &Video_otherBuf;

	stdDisplay_bModeSet = 1;
	return 1;

#else

    uint32_t newW = Window_xSize;
    uint32_t newH = Window_ySize;

    //if (jkGame_isDDraw)
    {
        newW = (uint32_t)((flex_t)Window_xSize * ((480.0*2.0)/Window_ySize));
        newH = 480*2;
    }

    if (newW > Window_xSize)
    {
        newW = Window_xSize;
        newH = Window_ySize;
    }

    if (newW < 640)
        newW = 640;
    if (newH < 480)
        newH = 480;
	
#ifdef TILE_SW_RASTER
	// eebs: fixme
	newW = 640;
	newH = 480;
#endif

    stdDisplay_pCurVideoMode = &Video_renderSurface[modeIdx];
    
    stdDisplay_pCurVideoMode->format.format.bpp = 8;
    stdDisplay_pCurVideoMode->format.width_in_pixels = newW;
    stdDisplay_pCurVideoMode->format.width = newW;
    stdDisplay_pCurVideoMode->format.height = newH;
    
    _memcpy(&Video_otherBuf.format, &stdDisplay_pCurVideoMode->format, sizeof(Video_otherBuf.format));
    _memcpy(&Video_menuBuffer.format, &stdDisplay_pCurVideoMode->format, sizeof(Video_menuBuffer.format));
    
    _memcpy(&Video_overlayMapBuffer.format, &stdDisplay_pCurVideoMode->format, sizeof(Video_overlayMapBuffer.format));
    
    if (Video_bModeSet)
    {
        glDeleteTextures(1, &Video_menuTexId);
        glDeleteTextures(1, &Video_overlayTexId);
        if (Video_otherBuf.sdlSurface)
            SDL_FreeSurface(Video_otherBuf.sdlSurface);
        if (Video_menuBuffer.sdlSurface)
            SDL_FreeSurface(Video_menuBuffer.sdlSurface);
        if (Video_overlayMapBuffer.sdlSurface)
            SDL_FreeSurface(Video_overlayMapBuffer.sdlSurface);
        
        Video_otherBuf.sdlSurface = 0;
        Video_menuBuffer.sdlSurface = 0;
        Video_overlayMapBuffer.sdlSurface = 0;

#ifdef HW_VBUFFER
		std3D_FreeDrawSurface(&Video_otherBuf);
		std3D_FreeDrawSurface(&Video_menuBuffer);
		std3D_FreeDrawSurface(&Video_overlayMapBuffer);
#endif
    }

#ifdef HW_VBUFFER
//if(create_ddraw_surface)
	std3D_AllocDrawSurface(&Video_otherBuf, newW, newH);
	std3D_AllocDrawSurface(&Video_menuBuffer, newW, newH);
	std3D_AllocDrawSurface(&Video_overlayMapBuffer, newW, newH);
#endif

    SDL_Surface* otherSurface = SDL_CreateRGBSurface(0, newW, newH, 8,
                                        0,
                                        0,
                                        0,
                                        0);
    SDL_Surface* menuSurface = SDL_CreateRGBSurface(0, newW, newH, 8,
                                        0,
                                        0,
                                        0,
                                        0);
    SDL_Surface* overlaySurface = SDL_CreateRGBSurface(0, newW, newH, 8, 0, 0, 0, 0);
    
    if (palette)
    {
		if (stdDisplay_paGammaTable != NULL && stdDisplay_gammaIdx != 0)
		{
			memcpy(stdDisplay_tmpGammaPal, palette, 0x300);
			stdColor_GammaCorrect(&stdDisplay_gammaPalette[0].r, stdDisplay_tmpGammaPal, 256,
								  stdDisplay_paGammaTable[stdDisplay_gammaIdx - 1]);
		}
		else
		{
			memcpy(stdDisplay_gammaPalette, palette, 0x300);
		}

        const rdColor24* pal24 = (const rdColor24*)palette;
        SDL_Color* tmp = (SDL_Color*)malloc(sizeof(SDL_Color) * 256);
        for (int i = 0; i < 256; i++)
        {
            tmp[i].r = pal24[i].r;
            tmp[i].g = pal24[i].g;
            tmp[i].b = pal24[i].b;
            tmp[i].a = 0xFF;
        }
        
        SDL_SetPaletteColors(otherSurface->format->palette, tmp, 0, 256);
        SDL_SetPaletteColors(menuSurface->format->palette, tmp, 0, 256);
        SDL_SetPaletteColors(overlaySurface->format->palette, tmp, 0, 256);
        free(tmp);
    }
    
    //SDL_SetSurfacePalette(otherSurface, palette);
    //SDL_SetSurfacePalette(menuSurface, palette);
    
    Video_otherBuf.sdlSurface = otherSurface;
    Video_menuBuffer.sdlSurface = menuSurface;
    Video_overlayMapBuffer.sdlSurface = overlaySurface;
    
    Video_menuBuffer.format.width_in_bytes = menuSurface->pitch;
    Video_otherBuf.format.width_in_bytes = otherSurface->pitch;
    Video_overlayMapBuffer.format.width_in_bytes = overlaySurface->pitch;
    
    Video_menuBuffer.format.width_in_pixels = menuSurface->pitch;
    Video_otherBuf.format.width_in_pixels = otherSurface->pitch;
    Video_overlayMapBuffer.format.width_in_pixels = overlaySurface->pitch;
    Video_menuBuffer.format.width = newW;
    Video_otherBuf.format.width = newW;
    Video_overlayMapBuffer.format.width = newW;
    Video_menuBuffer.format.height = newH;
    Video_otherBuf.format.height = newH;
    Video_overlayMapBuffer.format.height = newH;
    
    Video_menuBuffer.format.format.bpp = 8;
    Video_otherBuf.format.format.bpp = 8;
    Video_overlayMapBuffer.format.format.bpp = 8;
    
    glGenTextures(1, &Video_menuTexId);
    glBindTexture(GL_TEXTURE_2D, Video_menuTexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, newW, newH, 0, GL_RED, GL_UNSIGNED_BYTE, Video_menuBuffer.sdlSurface->pixels);
    
    glGenTextures(1, &Video_overlayTexId);
    glBindTexture(GL_TEXTURE_2D, Video_overlayTexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, newW, newH, 0, GL_RED, GL_UNSIGNED_BYTE, Video_overlayMapBuffer.sdlSurface->pixels);
    

    Video_bModeSet = 1;
    
    return 1;
#endif
}

int stdDisplay_ClearRect(stdVBuffer *buf, int fillColor, rdRect *rect)
{
    return stdDisplay_VBufferFill(buf, fillColor, rect);
}



int stdDisplay_DDrawGdiSurfaceFlip()
{
#ifdef TILE_SW_RASTER
	extern void SwapWindow(SDL_Window * window);
	SwapWindow(stdGdi_GetHwnd());
#else
    Window_SdlUpdate();
#endif
    return 1;
}

int stdDisplay_ddraw_waitforvblank()
{
    Window_SdlVblank();
    return 1;
}

int stdDisplay_SetMasterPalette(uint8_t* pal)
{
    rdColor24* pal24 = (rdColor24*)pal;
    
    memcpy(stdDisplay_masterPalette, pal24, sizeof(stdDisplay_masterPalette));
    
    return 1;
}

#ifdef TILE_SW_RASTER
int stdDisplay_SetMasterPaletteCube(uint8_t* pal)
{
	rdColor24* pal24 = (rdColor24*)pal;
	memcpy(stdDisplay_masterPaletteCube, pal24, sizeof(stdDisplay_masterPaletteCube));
	return 1;
}
#endif

stdVBuffer* stdDisplay_VBufferNew(stdVBufferTexFmt *fmt, int create_ddraw_surface, int gpu_mem, const void* palette)
{
    stdVBuffer* out = (stdVBuffer*)std_pHS->alloc(sizeof(stdVBuffer));    
	if (!out)
	{
		stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error allocating vbuffer.\n");
		return NULL;
	}
	_memset(out, 0, sizeof(*out));
    _memcpy(&out->format, fmt, sizeof(out->format));

#ifdef TILE_SW_RASTER
	out->surface_lock_alloc = NULL;
	out->bSurfaceLocked = 0;
	out->gap8 = 0;

	unsigned int bbp = (unsigned int)out->format.format.bpp / 8;
	out->format.width_in_bytes = out->format.width * bbp;
	out->format.width_in_pixels = out->format.width;
	out->format.texture_size_in_bytes = out->format.height * out->format.width * bbp;

	if (stdDisplay_pCurDevice && stdDisplay_pCurDevice->video_device[0].device_active)
	{	
		out->bSurfaceLocked = 1;
		out->gpuHandle = std3D_CreateSurface(&out->format);
		if (out->gpuHandle == 0)
		{
			stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Error when creating a DirectDraw vbuffer surface.\n");
			return NULL;
		}
	}
	else
	{
		out->bSurfaceLocked = 0;
		out->surface_lock_alloc = _mm_malloc(out->format.texture_size_in_bytes, 16);
	}
#else
    
    // force 0 reads
    //out->format.width = 0;
    //out->format.width_in_bytes = 0;
    //out->surface_lock_alloc = std_pHS->alloc(texture_size_in_bytes);
    
#ifndef TILE_SW_RASTER
    //if (fmt->format.g_bits == 6) // RGB565
    {
		// eebs: why is this here??
        fmt->format.r_bits = 0;
        fmt->format.g_bits = 0;
        fmt->format.b_bits = 0;
        fmt->format.r_shift = 0;
        fmt->format.g_shift = 0;
        fmt->format.b_shift = 0;
    }
#endif

    uint32_t rbitmask = ((1 << fmt->format.r_bits) - 1) << fmt->format.r_shift;
    uint32_t gbitmask = ((1 << fmt->format.g_bits) - 1) << fmt->format.g_shift;
    uint32_t bbitmask = ((1 << fmt->format.b_bits) - 1) << fmt->format.b_shift;
    uint32_t abitmask = 0;//((1 << fmt->format.a_bits) - 1) << fmt->format.a_shift;
    if (fmt->format.bpp == 8)
    {
        rbitmask = 0;
        gbitmask = 0;
        bbitmask = 0;
        abitmask = 0;
    }

    SDL_Surface* surface = SDL_CreateRGBSurface(0, fmt->width, fmt->height, fmt->format.bpp, rbitmask, gbitmask, bbitmask, abitmask);
    
    if (surface)
    {
        static int num = 0;
        //printf("Allocated VBuffer %u, w %u h %u bpp %u %x %x %x\n", num++, fmt->width, fmt->height, fmt->format.bpp, rbitmask, gbitmask, bbitmask);
        out->format.width_in_bytes = surface->pitch;
        out->format.width_in_pixels = fmt->width;
        out->format.texture_size_in_bytes = surface->pitch * fmt->height;
    }
    else
    {
        //printf("asdf\n");
        stdPlatform_Printf("Failed to allocate VBuffer! %s, w %u h %u bpp %u, rmask %x gmask %x bmask %x amask %x, %x %x %x, %x %x %x\n", SDL_GetError(), fmt->width, fmt->height, fmt->format.bpp, rbitmask, gbitmask, bbitmask, abitmask, fmt->format.r_bits, fmt->format.g_bits, fmt->format.b_bits, fmt->format.r_shift, fmt->format.g_shift, fmt->format.b_shift);
        assert(0);
    }
    //printf("Failed to allocate VBuffer! %s, w %u h %u bpp %u, rmask %x gmask %x bmask %x amask %x, %x %x %x, %x %x %x\n", SDL_GetError(), fmt->width, fmt->height, fmt->format.bpp, rbitmask, gbitmask, bbitmask, abitmask, fmt->format.r_bits, fmt->format.g_bits, fmt->format.b_bits, fmt->format.r_shift, fmt->format.g_shift, fmt->format.b_shift);
    
    out->sdlSurface = surface;

#ifdef HW_VBUFFER
	//memcpy(out->palette, palette, );
	//if(create_ddraw_surface)
		std3D_AllocDrawSurface(out, fmt->width, fmt->height);
#endif

#if 0//def TILE_SW_RASTER
	out->gpuHandle = std3D_Alloc(out->format.texture_size_in_bytes);
#endif
#endif
    
    return out;
}

int stdDisplay_VBufferLock(stdVBuffer *buf)
{
    if (!buf) return 0;
#ifdef TILE_SW_RASTER
	if (buf->lock_cnt > 0)
		return 0;
	if (buf->bSurfaceLocked)
		buf->surface_lock_alloc = std3D_LockSurface(buf->gpuHandle);
	if(!buf->surface_lock_alloc)
		return 0;
	++buf->lock_cnt;
	return 1;
#else
    SDL_LockSurface(buf->sdlSurface);
    buf->surface_lock_alloc = (char*)buf->sdlSurface->pixels;
	return 1;
#endif
}

void stdDisplay_VBufferUnlock(stdVBuffer *buf)
{
    if (!buf) return;
#ifdef TILE_SW_RASTER
	if (buf->lock_cnt <= 0)
		return;
	buf->lock_cnt--;
	if(buf->lock_cnt == 0)
	{
		if(buf->bSurfaceLocked)
		{
			std3D_UnlockSurface(buf->gpuHandle);
			buf->surface_lock_alloc = 0;
		}
	}
#else

#ifdef HW_VBUFFER
	if (buf->device_surface && buf->surface_lock_alloc)
	{
		std3D_UploadDrawSurface(buf->device_surface, buf->format.width, buf->format.height, buf->surface_lock_alloc, buf->palette);
	}
   #endif
    buf->surface_lock_alloc = NULL;
    SDL_UnlockSurface(buf->sdlSurface);
#endif
}

int stdDisplay_VBufferCopy(stdVBuffer *vbuf, stdVBuffer *vbuf2, int blit_x, int blit_y, rdRect *rect, int alpha_maybe)
{
    if (!vbuf || !vbuf2) return 1;
	
	rdRect fallback = { 0,0,vbuf2->format.width, vbuf2->format.height };
	if (!rect)
	{
		rect = &fallback;
		//memcpy(vbuf->sdlSurface->pixels, vbuf2->sdlSurface->pixels, 640*480);
		//return;
	}

#ifdef TILE_SW_RASTER

	// todo: conversions?
	if (vbuf->format.format.bpp != vbuf2->format.format.bpp)
		return 0;

	stdVBuffer* dst = vbuf;
	stdVBuffer* src = vbuf2;

	// Initial bounds
	int srcX = rect->x;
	int srcY = rect->y;
	int width = rect->width;
	int height = rect->height;

	int dstX = blit_x;
	int dstY = blit_y;

	int x0 = srcX;
	int y0 = srcY;
	int x1 = srcX + width - 1;
	int y1 = srcY + height - 1;

	// Clamp dstX/Y to bounds
	if (dstX < 0)
	{
		// Skip pixels on left
		int skip = -dstX;
		srcX += skip;
		width -= skip;
		dstX = 0;
	}
	if (dstY < 0)
	{
		// Skip rows on top
		int skip = -dstY;
		srcY += skip;
		height -= skip;
		dstY = 0;
	}
	if (dstX + width > dst->format.width)
	{
		width = dst->format.width - dstX;
	}
	if (dstY + height > dst->format.height)
	{
		height = dst->format.height - dstY;
	}

	// Recheck after full clamp
	if (width <= 0 || height <= 0)
		return 0;

	// Reject fully clipped
	if (width <= 0 || height <= 0)
		return 0;

	int blitWidth = width;
	int blitHeight = height;

	if (vbuf->bSurfaceLocked && vbuf2->bSurfaceLocked) // should both be true when 1 is true
	{
		rdRect dstRect = { dstX, dstY, blitWidth, blitHeight };
		rdRect srcRect = { srcX, srcY, blitWidth, blitHeight };

		std3D_BlitSurface(vbuf->gpuHandle, &dstRect, vbuf2->gpuHandle, &srcRect, vbuf2->transparent_color, alpha_maybe);
		return 1;
	}

	uint8_t* srcPixels = (uint8_t*)vbuf2->surface_lock_alloc;
	uint8_t* dstPixels = (uint8_t*)vbuf->surface_lock_alloc;
	uint32_t srcStride = vbuf2->format.width_in_bytes;
	uint32_t dstStride = vbuf->format.width_in_bytes;

	int bpp = dst->format.format.bpp;
	int transparency = (alpha_maybe & 1);

	switch (bpp)
	{
	case 8:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		char* dstPtr = dst->surface_lock_alloc + dstX + dstY * dstPitch;
		char* srcPtr = src->surface_lock_alloc + srcX + srcY * srcPitch;

		if (!transparency)
		{
			for (int y = 0; y < blitHeight; y++)
			{
				memcpy(dstPtr, srcPtr, blitWidth);
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		}
		else
		{
		#ifdef TARGET_SSE
			uint8_t transi = (uint8_t)src->transparent_color;
			__m128i trans = _mm_set1_epi8(transi);

			for (int y = 0; y < blitHeight; y++)
			{
				int x = 0;
				for (; x <= blitWidth - 16; x += 16)
				{
					__m128i src = _mm_loadu_si128((__m128i*)(srcPtr + x));
					__m128i dst = _mm_loadu_si128((__m128i*)(dstPtr + x));
					__m128i cmp = _mm_cmpeq_epi8(src, trans);
					__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, src), _mm_and_si128(cmp, dst));
					_mm_storeu_si128((__m128i*)(dstPtr + x), result);
				}

				for (; x < blitWidth; x++)
				{
					if (srcPtr[x] != transi)
						dstPtr[x] = srcPtr[x];
				}

				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		#else
			uint8_t trans = (uint8_t)src->transparent_color;
			for (int y = 0; y < blitHeight; y++)
			{
				for (int x = 0; x < blitWidth; x++)
				{
					if (srcPtr[x] != trans)
						dstPtr[x] = srcPtr[x];
				}
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		#endif
		}
		return 1;
	}

	case 16:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		short* dstPtr = (short*)(dst->surface_lock_alloc + 2 * (dstX + dstY * dstPitch));
		short* srcPtr = (short*)(src->surface_lock_alloc + 2 * (srcX + srcY * srcPitch));

		if (!transparency)
		{
			for (int y = 0; y < blitHeight; y++)
			{
				memcpy(dstPtr, srcPtr, 2 * blitWidth);
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		}
		else
		{
		#ifdef TARGET_SSE
			uint16_t transi = (uint16_t)src->transparent_color;
			__m128i trans = _mm_set1_epi16(transi);

			for (int y = 0; y < blitHeight; y++)
			{
				int x = 0;
				for (; x <= blitWidth - 8; x += 8)
				{
					__m128i src = _mm_loadu_si128((__m128i*)(srcPtr + x));
					__m128i dst = _mm_loadu_si128((__m128i*)(dstPtr + x));
					__m128i cmp = _mm_cmpeq_epi16(src, trans);
					__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, src), _mm_and_si128(cmp, dst));
					_mm_storeu_si128((__m128i*)(dstPtr + x), result);
				}

				for (; x < blitWidth; x++)
				{
					if (srcPtr[x] != transi)
						dstPtr[x] = srcPtr[x];
				}

				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		#else
			uint16_t trans = (uint16_t)src->transparent_color;
			for (int y = 0; y < blitHeight; y++)
			{
				for (int x = 0; x < blitWidth; x++)
				{
					if (srcPtr[x] != trans)
						dstPtr[x] = srcPtr[x];
				}
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		#endif
		}
		return 1;
	}

	case 24:
	{
		int dstPitch = dst->format.width_in_bytes;
		int srcPitch = src->format.width_in_bytes;
		uint8_t* dstPtr = (uint8_t*)(dst->surface_lock_alloc + dstY * dstPitch + dstX * 3);
		uint8_t* srcPtr = (uint8_t*)(src->surface_lock_alloc + srcY * srcPitch + srcX * 3);

		for (int y = 0; y < blitHeight; y++)
		{
			memcpy(dstPtr, srcPtr, 3 * blitWidth);
			dstPtr += dstPitch;
			srcPtr += srcPitch;
		}
		return 1;
	}

	case 32:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		uint32_t* dstPtr = (uint32_t*)(dst->surface_lock_alloc + 4 * (dstX + dstY * dstPitch));
		uint32_t* srcPtr = (uint32_t*)(src->surface_lock_alloc + 4 * (srcX + srcY * srcPitch));

		if (!transparency)
		{
			for (int y = 0; y < blitHeight; y++)
			{
				memcpy(dstPtr, srcPtr, 4 * blitWidth);
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		}
		else
		{
		#ifdef TARGET_SSE
			uint32_t transi = src->transparent_color;
			const __m128i trans = _mm_set1_epi32(transi);

			int x = 0;
			for (; x <= blitWidth - 4; x += 4)
			{
				__m128i src = _mm_loadu_si128((__m128i*)(srcPtr + x));
				__m128i cmp = _mm_cmpeq_epi32(src, trans);
				__m128i dst = _mm_loadu_si128((__m128i*)(dstPtr + x));
				__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, src), _mm_and_si128(cmp, dst));
				_mm_storeu_si128((__m128i*)(dstPtr + x), result);
			}

			for (; x < blitWidth; x++)
			{
				if (srcPtr[x] != transi)
					dstPtr[x] = srcPtr[x];
			}

			dstPtr += dstPitch;
			srcPtr += srcPitch;
		#else
			const uint32_t trans = src->transparent_color;
			for (int y = 0; y < blitHeight; y++)
			{
				for (int x = 0; x < blitWidth; x++)
				{
					if (srcPtr[x] != trans)
						dstPtr[x] = srcPtr[x];
				}
				dstPtr += dstPitch;
				srcPtr += srcPitch;
			}
		#endif
		}
		return 1;
	}

	default:
		// Unsupported bpp
		return 0;
	}

	#if 0

	int self_copy = 0;
	int has_alpha = !(rect->width == 640) && (alpha_maybe & 1);

	// Handle self-copy case with temporary buffer
	if (dstPixels == srcPixels)
	{
		size_t buf_len = srcStride * dstRect.height;
		uint8_t* tempBuffer = (uint8_t*)_mm_malloc(buf_len, 16); // Aligned allocation
		SDL_Rect dstRect_inter = { 0, 0, rect->width, rect->height };

		// Process rows in blocks
		for (int j = 0; j < rect->height; j++)
		{
			int srcY = j + srcRect.y;
			if ((uint32_t)srcY >= (uint32_t)vbuf2->format.height) continue;

			uint8_t* srcRow = srcPixels + srcY * srcStride + srcRect.x;
			uint8_t* dstRow = tempBuffer + j * srcStride;

			int i = 0;
			// Process 16 bytes at a time
			for (; i + 15 < rect->width; i += 16)
			{
				// Load 16 pixels
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));

				if (has_alpha)
				{
					// Create mask for non-zero pixels
					__m128i zero = _mm_setzero_si128();
					__m128i mask = _mm_cmpeq_epi8(pixels, zero);

					// For self-copy, we just store all pixels (no alpha skip)
					_mm_store_si128((__m128i*)(dstRow + i), pixels);
				}
				else
				{
					_mm_store_si128((__m128i*)(dstRow + i), pixels);
				}
			}

			// Handle remaining pixels
			for (; i < rect->width; i++)
				dstRow[i] = srcRow[i];
		}

		srcPixels = tempBuffer;
		srcRect.x = 0;
		srcRect.y = 0;
		self_copy = 1;
	}

	// Main blitting loop with SSE optimization
	for (int j = 0; j < rect->height; j++)
	{
		int srcY = j + srcRect.y;
		int dstY = j + dstRect.y;

		if ((uint32_t)srcY >= (uint32_t)vbuf2->format.height) continue;
		if ((uint32_t)dstY >= (uint32_t)vbuf->format.height) continue;

		uint8_t* srcRow = srcPixels + srcY * srcStride + srcRect.x;
		uint8_t* dstRow = dstPixels + dstY * dstStride + dstRect.x;

		int i = 0;
		if (!has_alpha)
		{
			// Fast path: no alpha handling, copy entire rows
			for (; i + 15 < rect->width; i += 16)
			{
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));
				_mm_storeu_si128((__m128i*)(dstRow + i), pixels);
			}
		}
		else
		{
			 // Alpha handling path
			__m128i zero = _mm_setzero_si128();
			for (; i + 15 < rect->width; i += 16)
			{
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));
				__m128i mask = _mm_cmpeq_epi8(pixels, zero);

				// Load destination pixels
				__m128i dstPx = _mm_loadu_si128((__m128i*)(dstRow + i));

				// Blend: where mask is true (pixel==0), keep destination
				__m128i result = _mm_or_si128(
					_mm_and_si128(mask, dstPx),
					_mm_andnot_si128(mask, pixels)
				);

				_mm_storeu_si128((__m128i*)(dstRow + i), result);
			}
		}

		// Handle remaining pixels
		for (; i < rect->width; i++)
		{
			uint8_t pixel = srcRow[i];
			if (!(pixel == 0 && has_alpha))
				dstRow[i] = pixel;
		}
	}

	// Free temporary buffer if used
	if (self_copy)
		_mm_free(srcPixels);
	#endif

#else
    
    //if (vbuf == &Video_menuBuffer)
    //    stdPlatform_Printf("Vbuffer copy to menu %u,%u %ux%u %u,%u\n", rect->x, rect->y, rect->width, rect->height, blit_x, blit_y);
    
    if (vbuf->palette)
    {
        rdColor24* pal24 = (rdColor24*)vbuf->palette;
        SDL_Color* tmp = (SDL_Color*)malloc(sizeof(SDL_Color) * 256);
        for (int i = 0; i < 256; i++)
        {
            tmp[i].r = pal24[i].r;
            tmp[i].g = pal24[i].g;
            tmp[i].b = pal24[i].b;
            tmp[i].a = 0xFF;
        }
    
        SDL_SetPaletteColors(vbuf->sdlSurface->format->palette, tmp, 0, 256);
        free(tmp);
    }
    
    if (vbuf2->palette)
    {
        rdColor24* pal24 = (rdColor24*)vbuf2->palette;
        SDL_Color* tmp = (SDL_Color*)malloc(sizeof(SDL_Color) * 256);
        for (int i = 0; i < 256; i++)
        {
            tmp[i].r = pal24[i].r;
            tmp[i].g = pal24[i].g;
            tmp[i].b = pal24[i].b;
            tmp[i].a = 0xFF;
        }
        
        SDL_SetPaletteColors(vbuf2->sdlSurface->format->palette, tmp, 0, 256);
        free(tmp);
    }

    SDL_Rect dstRect = {(int)blit_x, (int)blit_y, (int)rect->width, (int)rect->height};
    SDL_Rect srcRect = {(int)rect->x, (int)rect->y, (int)rect->width, (int)rect->height};
    
    uint8_t* srcPixels = (uint8_t*)vbuf2->sdlSurface->pixels;
    uint8_t* dstPixels = (uint8_t*)vbuf->sdlSurface->pixels;
    uint32_t srcStride = vbuf2->format.width_in_bytes;
    uint32_t dstStride = vbuf->format.width_in_bytes;

#ifdef TARGET_SSE
	int self_copy = 0;
	int has_alpha = !(rect->width == 640) && (alpha_maybe & 1);

	// Handle self-copy case with temporary buffer
	if (dstPixels == srcPixels)
	{
		size_t buf_len = srcStride * dstRect.h;
		uint8_t* tempBuffer = (uint8_t*)_mm_malloc(buf_len, 16); // Aligned allocation
		SDL_Rect dstRect_inter = { 0, 0, rect->width, rect->height };

		// Process rows in blocks
		for (int j = 0; j < rect->height; j++)
		{
			int srcY = j + srcRect.y;
			if ((uint32_t)srcY >= (uint32_t)vbuf2->format.height) continue;

			uint8_t* srcRow = srcPixels + srcY * srcStride + srcRect.x;
			uint8_t* dstRow = tempBuffer + j * srcStride;

			int i = 0;
			// Process 16 bytes at a time
			for (; i + 15 < rect->width; i += 16)
			{
				// Load 16 pixels
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));

				if (has_alpha)
				{
					// Create mask for non-zero pixels
					__m128i zero = _mm_setzero_si128();
					__m128i mask = _mm_cmpeq_epi8(pixels, zero);

					// For self-copy, we just store all pixels (no alpha skip)
					_mm_store_si128((__m128i*)(dstRow + i), pixels);
				}
				else
				{
					_mm_store_si128((__m128i*)(dstRow + i), pixels);
				}
			}

			// Handle remaining pixels
			for (; i < rect->width; i++)
				dstRow[i] = srcRow[i];
		}

		srcPixels = tempBuffer;
		srcRect.x = 0;
		srcRect.y = 0;
		self_copy = 1;
	}

	// Main blitting loop with SSE optimization
	for (int j = 0; j < rect->height; j++)
	{
		int srcY = j + srcRect.y;
		int dstY = j + dstRect.y;

		if ((uint32_t)srcY >= (uint32_t)vbuf2->format.height) continue;
		if ((uint32_t)dstY >= (uint32_t)vbuf->format.height) continue;

		uint8_t* srcRow = srcPixels + srcY * srcStride + srcRect.x;
		uint8_t* dstRow = dstPixels + dstY * dstStride + dstRect.x;

		int i = 0;
		if (!has_alpha)
		{
			// Fast path: no alpha handling, copy entire rows
			for (; i + 15 < rect->width; i += 16)
			{
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));
				_mm_storeu_si128((__m128i*)(dstRow + i), pixels);
			}
		}
		else
		{
			 // Alpha handling path
			__m128i zero = _mm_setzero_si128();
			for (; i + 15 < rect->width; i += 16)
			{
				__m128i pixels = _mm_loadu_si128((__m128i*)(srcRow + i));
				__m128i mask = _mm_cmpeq_epi8(pixels, zero);

				// Load destination pixels
				__m128i dstPx = _mm_loadu_si128((__m128i*)(dstRow + i));

				// Blend: where mask is true (pixel==0), keep destination
				__m128i result = _mm_or_si128(
					_mm_and_si128(mask, dstPx),
					_mm_andnot_si128(mask, pixels)
				);

				_mm_storeu_si128((__m128i*)(dstRow + i), result);
			}
		}

		// Handle remaining pixels
		for (; i < rect->width; i++)
		{
			uint8_t pixel = srcRow[i];
			if (!(pixel == 0 && has_alpha))
				dstRow[i] = pixel;
		}
	}

	// Free temporary buffer if used
	if (self_copy)
		_mm_free(srcPixels);
	
#else
    int self_copy = 0;

    if (dstPixels == srcPixels)
    {
        size_t buf_len = srcStride * dstRect.w * dstRect.h;
        uint8_t* dstPixels = (uint8_t*)malloc(buf_len);
        int has_alpha = 0;//!(rect->width == 640);

        SDL_Rect dstRect_inter = {0, 0, rect->width, rect->height};

        for (int i = 0; i < rect->width; i++)
        {
            for (int j = 0; j < rect->height; j++)
            {
                if ((uint32_t)(i + srcRect.x) > (uint32_t)vbuf2->format.width) continue;
                if ((uint32_t)(j + srcRect.y) > (uint32_t)vbuf2->format.height) continue;
                
                uint8_t pixel = srcPixels[(i + srcRect.x) + ((j + srcRect.y)*srcStride)];

                if (!pixel && has_alpha) continue;
                if ((uint32_t)(i + dstRect_inter.x) > (uint32_t)vbuf->format.width) continue;
                if ((uint32_t)(j + dstRect_inter.y) > (uint32_t)vbuf->format.height) continue;

                dstPixels[(i + dstRect_inter.x) + ((j + dstRect_inter.y)*srcStride)] = pixel;
            }
        }
        
        srcPixels = dstPixels;
        srcRect.x = 0;
        srcRect.y = 0;

        self_copy = 1;
    }
    
    int once = 0;
    int has_alpha = !(rect->width == 640) && (alpha_maybe & 1);
    
    for (int i = 0; i < rect->width; i++)
    {
        for (int j = 0; j < rect->height; j++)
        {
            if ((uint32_t)(i + srcRect.x) >= (uint32_t)vbuf2->format.width) continue;
            if ((uint32_t)(j + srcRect.y) >= (uint32_t)vbuf2->format.height) continue;
            
            uint8_t pixel = srcPixels[(i + srcRect.x) + ((j + srcRect.y)*srcStride)];

            if (!pixel && has_alpha) continue;
            if ((uint32_t)(i + dstRect.x) >= (uint32_t)vbuf->format.width) continue;
            if ((uint32_t)(j + dstRect.y) >= (uint32_t)vbuf->format.height) continue;

            dstPixels[(i + dstRect.x) + ((j + dstRect.y)*dstStride)] = pixel;
        }
    }

    if (self_copy)
    {
        free(srcPixels);
    }
#endif

#ifdef HW_VBUFFER
	if (vbuf != vbuf2)
	{
		if (vbuf->device_surface && vbuf2->device_surface)
		{
			rdRect dstRect = { blit_x, blit_y, rect->width, rect->height };
			rdRect srcRect = { rect->x, rect->y, rect->width, rect->height };
			std3D_BlitDrawSurface(vbuf2->device_surface, &srcRect, vbuf->device_surface, &dstRect);
		}
	}
#endif
    
#endif
	//SDL_BlitSurface(vbuf2->sdlSurface, &srcRect, vbuf->sdlSurface, &dstRect); //TODO error check
    return 1;
}

int stdDisplay_VBufferFill(stdVBuffer *vbuf, int fillColor, rdRect *rect)
{    
	STD_BEGIN_PROFILER_LABEL();

#ifdef TILE_SW_RASTER

	// Get surface dimensions
	int width = vbuf->format.width_in_pixels;
	int height = vbuf->format.height;

	// Clamp rectangle
	int x = rect ? rect->x : 0;
	int y = rect ? rect->y : 0;
	int w = rect ? rect->width : width;
	int h = rect ? rect->height : height;

	// Clamp left/top
	if (x < 0)
	{
		w += x;  // shrink width
		x = 0;
	}
	if (y < 0)
	{
		h += y;  // shrink height
		y = 0;
	}

	// Clamp right/bottom
	if (x + w > width)
		w = width - x;
	if (y + h > height)
		h = height - y;

	// Reject if fully clipped
	if (w <= 0 || h <= 0)
		return 0;

	if (vbuf->bSurfaceLocked)
	{
		rdRect r = { x, y, w, h };
		std3D_FillSurface(vbuf->gpuHandle, fillColor, vbuf->format.width, vbuf->format.height, vbuf->format.format.bpp >> 3, &r);
		return 1;
	}

	switch (vbuf->format.format.bpp)
	{
	case 8:
		if (rect)
		{
			uint8_t* pPixels8 = &vbuf->surface_lock_alloc[vbuf->format.width * y + x];
			for (int32_t height = 0; height < h; ++height)
			{
				memset(pPixels8, (uint8_t)fillColor, w);
				pPixels8 += vbuf->format.width_in_bytes;
			}
		}
		else
		{
			memset(vbuf->surface_lock_alloc, (uint8_t)fillColor, vbuf->format.texture_size_in_bytes);
		}

		break;

	case 16:
		if (rect)
		{
			uint16_t* pPixels16 = (uint16_t*)&vbuf->surface_lock_alloc[2 * vbuf->format.width * y + 2 * x];
			for (int32_t height = 0; height < h; ++height)
			{
				stdDisplay_SetPixels16(pPixels16, fillColor, w);
				pPixels16 = (uint16_t*)((char*)pPixels16 + vbuf->format.width_in_bytes);
			}
		}
		else
		{
			stdDisplay_SetPixels16((uint16_t*)vbuf->surface_lock_alloc, fillColor, (size_t)(vbuf->format.texture_size_in_bytes / 2));
		}

		break;

	case 24:
		//STDLOG_FATAL("0"); // TODO: implement
		break;

	case 32:
		if (rect)
		{
			uint32_t* pPixels32 = (uint32_t*)&vbuf->surface_lock_alloc[4 * vbuf->format.width * y + 4 * x];
			for (int32_t height = 0; height < h; ++height)
			{
				stdDisplay_SetPixels32(pPixels32, fillColor, w);
				pPixels32 = (uint32_t*)((char*)pPixels32 + vbuf->format.width_in_bytes);
			}
		}
		else
		{
			stdDisplay_SetPixels32((uint32_t*)vbuf->surface_lock_alloc, fillColor, (size_t)(vbuf->format.texture_size_in_bytes / 4));
		}

		break;
	}

#else
	rdRect fallback = { 0,0,vbuf->format.width, vbuf->format.height };
	if (!rect)
	{
		rect = &fallback;
	}

    //if (vbuf == &Video_menuBuffer)
    //    stdPlatform_Printf("Vbuffer fill to menu %u,%u %ux%u\n", rect->x, rect->y, rect->width, rect->height);

    SDL_Rect dstRect = {rect->x, rect->y, rect->width, rect->height};
    
    //printf("%x; %u %u %u %u\n", fillColor, rect->x, rect->y, rect->width, rect->height);

	// eebs: this is stupid slow
	//uint8_t* dstPixels = (uint8_t*)vbuf->sdlSurface->pixels;
    //uint32_t dstStride = vbuf->format.width_in_bytes;
    //uint32_t max_idx = dstStride * vbuf->format.height;
    //for (int i = 0; i < rect->width; i++)
    //{
    //    for (int j = 0; j < rect->height; j++)
    //    {
    //        uint32_t idx = (i + dstRect.x) + ((j + dstRect.y)*dstStride);
    //        if (idx > max_idx)
    //            continue;
    //        
    //        dstPixels[idx] = fillColor;
    //    }
    //}
    
	// eebs: about 1ms faster than above but still pretty shit, can maybe fix with the hw version
    SDL_FillRect(vbuf->sdlSurface, &dstRect, fillColor); //TODO error check

#ifdef HW_VBUFFER
	if (vbuf->device_surface)
		std3D_ClearDrawSurface(vbuf->device_surface, fillColor, rect);
#endif
#endif
	STD_END_PROFILER_LABEL();

    return 1;
}

int stdDisplay_VBufferSetColorKey(stdVBuffer *vbuf, int color)
{
    //DDCOLORKEY v3; // [esp+0h] [ebp-8h] BYREF

    if ( vbuf->bSurfaceLocked )
    {
        /*if ( vbuf->bSurfaceLocked == 1 )
        {
            v3.dwColorSpaceLowValue = color;
            v3.dwColorSpaceHighValue = color;
            vbuf->ddraw_surface->lpVtbl->SetColorKey(vbuf->ddraw_surface, 8, &v3);
            return 1;
        }*/
        vbuf->transparent_color = color;
    }
    else
    {
        vbuf->transparent_color = color;
    }
    return 1;
}

void stdDisplay_VBufferFree(stdVBuffer *vbuf)
{
    // Added: Safety fallbacks
    if (!vbuf) {
        return;
    }
    stdDisplay_VBufferUnlock(vbuf);
#ifdef TILE_SW_RASTER
	if (vbuf->bSurfaceLocked)
		std3D_ReleaseSurface(vbuf->gpuHandle);
	else if (vbuf->surface_lock_alloc)
		_mm_free(vbuf->surface_lock_alloc);
	vbuf->gpuHandle = 0;
	vbuf->surface_lock_alloc = 0;
#else
    SDL_FreeSurface(vbuf->sdlSurface);
#ifdef HW_VBUFFER
	std3D_FreeDrawSurface(vbuf);
#endif
#endif
	std_pHS->free(vbuf);
}

void stdDisplay_ddraw_surface_flip2()
{
}

void stdDisplay_RestoreDisplayMode()
{

}

#ifdef TILE_SW_RASTER
stdVBuffer* stdDisplay_VBufferConvertColorFormat(const rdTexformat* pDesiredColorFormat, stdVBuffer* pSrc)
{
	assert(pSrc != NULL);
	if (memcmp(pDesiredColorFormat, &pSrc->format.format, sizeof(rdTexformat)) == 0)
		return pSrc;

	if (pSrc->format.format.colorMode == STDCOLOR_PAL)
	{
		if (pDesiredColorFormat->colorMode == STDCOLOR_PAL)
		{
			return pSrc;
		}
		assert(pSrc->format.format.colorMode != STDCOLOR_PAL);
	}

	const rdTexformat* pSrcColorFormat = &pSrc->format.format;
	assert(pDesiredColorFormat->colorMode != STDCOLOR_PAL);
	assert(pSrcColorFormat->r_bits != 0);
	assert(pSrcColorFormat->g_bits != 0);
	assert(pSrcColorFormat->b_bits != 0);
	assert(pDesiredColorFormat->r_bits != 0);
	assert(pDesiredColorFormat->g_bits != 0);
	assert(pDesiredColorFormat->b_bits != 0);
	assert(pSrcColorFormat->r_shift != 0 || pSrcColorFormat->g_shift != 0 || pSrcColorFormat->b_shift != 0);
	assert(pDesiredColorFormat->r_shift != 0 || pDesiredColorFormat->g_shift != 0 || pDesiredColorFormat->b_shift != 0);

	stdVBuffer* pDest;
	if (pSrc->format.format.bpp == pDesiredColorFormat->bpp)
	{
		pDest = pSrc;
		pDest = pSrc;
	}
	else
	{
		stdVBufferTexFmt rasterInfo;
		memcpy(&rasterInfo, &pSrc->format, sizeof(rasterInfo));
		memcpy(&rasterInfo.format, pDesiredColorFormat, sizeof(rasterInfo.format));

		pDest = stdDisplay_VBufferNew(&rasterInfo, 0, 0, 0);
		if (!pDest)
		{
			stdPrintf(std_pHS->errorPrint, ".\\Platform\\D3D\\std3D.c", __LINE__, "Unable to allocate memory for new stdVBuffer.\n");
			return NULL;
		}
	}

	// Convert pixel data
	assert(pSrc->surface_lock_alloc != NULL);
	assert(pDest->surface_lock_alloc != NULL);

	stdDisplay_VBufferLock(pSrc);
	stdDisplay_VBufferLock(pDest);

	size_t height = pDest->format.height;
	uint32_t dst_stride = pDest->format.width_in_bytes;
	uint32_t src_stride = pSrc->format.width_in_bytes;

	const uint8_t* pSrcRow = NULL;
	uint8_t* pDestRow = NULL;

	for (size_t row = 0; row < (signed int)pDest->format.height; ++row)
	{
		pSrcRow = pSrc->surface_lock_alloc + src_stride * row;
		pDestRow = pDest->surface_lock_alloc + dst_stride * row;

		stdColor_ColorConvertOneRow(
			pDestRow,
			pDesiredColorFormat,
			pSrcRow,
			&pSrc->format.format,
			pDest->format.width
		);
	}

	assert(pDestRow <= pDest->surface_lock_alloc + pDest->format.texture_size_in_bytes);
	assert(pSrcRow <= pSrc->surface_lock_alloc + pSrc->format.texture_size_in_bytes);
	stdDisplay_VBufferUnlock(pSrc);
	stdDisplay_VBufferUnlock(pDest);

	// Copy color format
	memcpy(&pDest->format.format, pDesiredColorFormat, sizeof(pDest->format.format));

	pDest->transparent_color = stdColor_ColorConvertOnePixel(pDesiredColorFormat, pSrc->transparent_color, &pSrc->format.format);
	
	if (pDest != pSrc)
	{
		stdDisplay_VBufferFree(pSrc);
	}

	return pDest;
}
#else
stdVBuffer* stdDisplay_VBufferConvertColorFormat(const rdTexformat* a, stdVBuffer* b)
{
    return b;
}
#endif

int stdDisplay_GammaCorrect3(int gammaIndex)
{
	uint8_t convertedPalette[768];

	if (stdDisplay_paGammaTable == NULL)
	{
		memcpy(stdDisplay_gammaPalette, stdDisplay_tmpGammaPal, sizeof(rdColor24) * 256);
		return 0;
	}

	if (gammaIndex == 0)
	{
		memcpy(stdDisplay_gammaPalette, stdDisplay_tmpGammaPal, sizeof(rdColor24) * 256);
	}
	else
	{
		stdColor_GammaCorrect(
			&stdDisplay_gammaPalette[0].r,
			stdDisplay_tmpGammaPal,
			768,
			stdDisplay_paGammaTable[gammaIndex - 1]
		);
	}

	// jk usually sets palette to the window with GDI or D3D here
	memcpy(stdDisplay_masterPalette, stdDisplay_gammaPalette, sizeof(stdDisplay_masterPalette));

	stdDisplay_gammaIdx = gammaIndex;
	return 1;
}

int stdDisplay_SetCooperativeLevel(uint32_t a){return 0;}
int stdDisplay_DrawAndFlipGdi(uint32_t a)
{
#ifdef TILE_SW_RASTER
	if (stdDisplay_bOpen == 0)
		return 0;

	if (stdDisplay_bModeSet == 0)
		return 0;

	int isActive = stdDisplay_pCurDevice->video_device[0].device_active + -1;
	if (isActive != 0)
		return isActive;

	stdDisplay_VBufferFill(&Video_menuBuffer, 0, (rdRect*)0x0);
	stdDisplay_DDrawGdiSurfaceFlip();
#endif
	return 0;
}

void stdDisplay_FreeBackBuffers()
{
#ifdef TILE_SW_RASTER
	if (stdDisplay_bModeSet)
	{
		//if (Video_otherBuf.sdlSurface)
		//	SDL_FreeSurface(Video_otherBuf.sdlSurface);

		stdDisplay_VBufferUnlock(&Video_otherBuf);

		if(Video_otherBuf.surface_lock_alloc)
			//std_pHS->free(Video_otherBuf.surface_lock_alloc);
			_mm_free(Video_otherBuf.surface_lock_alloc);
		Video_otherBuf.surface_lock_alloc = 0;
		Video_menuBuffer.surface_lock_alloc = 0;

		Video_otherBuf.palette = 0;
		Video_menuBuffer.palette = 0;

		//Video_otherBuf.sdlSurface = 0;
		//Video_menuBuffer.sdlSurface = 0;
		stdDisplay_bModeSet = 0;

		//std3D_FreeSwapChain();

		//SDL_RestoreWindow(stdGdi_GetHwnd());
		//SDL_UpdateWindowSurface(stdGdi_GetHwnd());
		//SDL_PumpEvents();
		//
		//SDL_SysWMinfo wmInfo;
		//SDL_VERSION(&wmInfo.version);
		//SDL_GetWindowWMInfo((SDL_Window*)stdGdi_GetHwnd(), &wmInfo);
		//HWND hwnd = wmInfo.info.win.window;
		//InvalidateRect(hwnd, NULL, TRUE);
		//UpdateWindow(hwnd);
	}
#endif
}
#endif

void stdDisplay_GammaCorrect(const void *pPal)
{
	memcpy(stdDisplay_tmpGammaPal, pPal, 768);

	if (stdDisplay_paGammaTable != NULL && stdDisplay_gammaIdx != 0)
	{
		double gammaValue = stdDisplay_paGammaTable[stdDisplay_gammaIdx - 1];
		stdColor_GammaCorrect(
			&stdDisplay_gammaPalette[0].r,
			stdDisplay_tmpGammaPal,
			256,
			gammaValue
		);
	}
	else
	{
		memcpy(stdDisplay_gammaPalette, pPal, 768);
	}
}

#ifdef TILE_SW_RASTER
// Added
void stdDisplay_VBufferCopyScaled(stdVBuffer* vbuf, stdVBuffer* vbuf2, unsigned int blit_x, unsigned int blit_y, rdRect* rect, int has_alpha, float scaleX, float scaleY)
{
	if (vbuf->bSurfaceLocked)
		return;

	rdRect fallback = { 0,0,vbuf2->format.width, vbuf2->format.height };
	if (!rect)
	{
		rect = &fallback;
		//memcpy(vbuf->sdlSurface->pixels, vbuf2->sdlSurface->pixels, 640*480);
		//return;
	}

	uint8_t* srcPixels = (uint8_t*)vbuf2->surface_lock_alloc;
	uint8_t* dstPixels = (uint8_t*)vbuf->surface_lock_alloc;
	uint32_t srcStride = vbuf2->format.width_in_bytes;
	uint32_t dstStride = vbuf->format.width_in_bytes;

	stdVBuffer* dst = vbuf;
	stdVBuffer* src = vbuf2;

	int srcX = rect->x;
	int srcY = rect->y;
	int dstX = blit_x;
	int dstY = blit_y;
	int width = rect->width;
	int height = rect->height;

	// Early out: completely out of bounds
	if (srcX >= src->format.width || srcY >= src->format.height ||
		(srcX + width) <= 0 || (srcY + height) <= 0)
	{
		return 0;
	}

	// Clamp to source bounds
	if (srcY < 0)
	{
		dstY -= srcY;
		height += srcY; // shrink height
		srcY = 0;
	}
	if (srcX < 0)
	{
		dstX -= srcX;
		width += srcX; // shrink width
		srcX = 0;
	}

	int maxX = src->format.width - 1;
	int maxY = src->format.height - 1;

	if (srcX + width > src->format.width)
		width = src->format.width - srcX;

	if (srcY + height > src->format.height)
		height = src->format.height - srcY;

	// Early out after clamping
	if (width <= 0 || height <= 0)
		return 1;

	int blitWidth = width;
	int blitHeight = height;

	if (blitWidth <= 0 || blitHeight <= 0) return 1;

	int bpp = dst->format.format.bpp;
	int transparency = (has_alpha & 1);

	int destWidth = (int)(blitWidth * scaleX);
	int destHeight = (int)(blitHeight * scaleY);

	// Avoid zero dimensions
	if (destWidth <= 0) destWidth = 1;
	if (destHeight <= 0) destHeight = 1;

	uint32_t xStep = ((blitWidth << 16)) / destWidth;
	uint32_t yStep = ((blitHeight << 16)) / destHeight;

	switch (bpp)
	{
	case 8:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		uint8_t* dstPtr = dst->surface_lock_alloc + dstX + dstY * dstPitch;
		uint8_t* srcPtr = src->surface_lock_alloc + srcX + srcY * srcPitch;

		if (!transparency)
		{
			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint8_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint8_t* dstRow = dstPtr + y * dstPitch;

				for (int x = 0, xPos = 0; x < destWidth; x++, xPos += xStep)
					dstRow[x] = srcRow[xPos >> 16];
			}
		}
		else
		{
#ifdef TARGET_SSE
			uint8_t transi = (uint8_t)src->transparent_color;
			__m128i trans = _mm_set1_epi8(transi);

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint8_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint8_t* dstRow = dstPtr + y * dstPitch;

				int x = 0;
				uint32_t xPos = 0;

				for (; x <= destWidth - 16; x += 16, xPos += 16 * xStep)
				{
					uint8_t tmp[16];
					for (int i = 0; i < 16; i++)
						tmp[i] = srcRow[(xPos + i * xStep) >> 16];

					__m128i src = _mm_loadu_si128((__m128i*)tmp);
					__m128i dst = _mm_loadu_si128((__m128i*)(dstRow + x));
					__m128i cmp = _mm_cmpeq_epi8(src, trans);
					__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, src), _mm_and_si128(cmp, dst));
					_mm_storeu_si128((__m128i*)(dstRow + x), result);
				}

				for (; x < destWidth; x++, xPos += xStep)
				{
					uint8_t srcPix = srcRow[xPos >> 16];
					if (srcPix != transi)
						dstRow[x] = srcPix;
				}
			}
#else
			uint8_t trans = (uint8_t)src->transparent_color;

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint8_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint8_t* dstRow = dstPtr + y * dstPitch;

				uint32_t xPos = 0;
				for (int x = 0; x < destWidth; x++, xPos += xStep)
				{
					uint8_t srcPix = srcRow[xPos >> 16];
					if (srcPix != trans)
						dstRow[x] = srcPix;
				}
			}
#endif
}
		return 1;
}

	case 16:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		uint16_t* dstPtr = (uint16_t*)(dst->surface_lock_alloc + 2 * (dstX + dstY * dstPitch));
		uint16_t* srcPtr = (uint16_t*)(src->surface_lock_alloc + 2 * (srcX + srcY * srcPitch));

		if (!transparency)
		{
			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint16_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint16_t* dstRow = dstPtr + y * dstPitch;

				for (int x = 0, xPos = 0; x < destWidth; x++, xPos += xStep)
					dstRow[x] = srcRow[xPos >> 16];
			}
		}
		else
		{
#ifdef TARGET_SSE
			uint16_t transi = (uint16_t)src->transparent_color;
			__m128i trans = _mm_set1_epi16(transi);

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint16_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint16_t* dstRow = dstPtr + y * dstPitch;

				int x = 0;
				uint32_t xPos = 0;

				for (; x <= destWidth - 8; x += 8, xPos += 8 * xStep)
				{
					uint16_t sx[8];
					for (int i = 0; i < 8; i++)
						sx[i] = srcRow[(xPos + i * xStep) >> 16];

					__m128i src = _mm_loadu_si128((__m128i*)sx);
					__m128i dst = _mm_loadu_si128((__m128i*)(dstRow + x));
					__m128i cmp = _mm_cmpeq_epi16(src, trans);
					__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, src), _mm_and_si128(cmp, dst));
					_mm_storeu_si128((__m128i*)(dstRow + x), result);
				}

				for (; x < destWidth; x++, xPos += xStep)
				{
					uint16_t srcPix = srcRow[xPos >> 16];
					if (srcPix != transi)
						dstRow[x] = srcPix;
				}
			}
#else
			uint16_t trans = (uint16_t)src->transparent_color;

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint16_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint16_t* dstRow = dstPtr + y * dstPitch;

				uint32_t xPos = 0;
				for (int x = 0; x < destWidth; x++, xPos += xStep)
				{
					uint16_t srcPix = srcRow[xPos >> 16];
					if (srcPix != trans)
						dstRow[x] = srcPix;
				}
			}
#endif
		}
		return 1;
	}

	case 24:
	{
		int dstPitch = dst->format.width_in_bytes;
		int srcPitch = src->format.width_in_bytes;
		uint8_t* dstPtr = (uint8_t*)(dst->surface_lock_alloc + dstY * dstPitch + dstX * 3);
		uint8_t* srcPtr = (uint8_t*)(src->surface_lock_alloc + srcY * srcPitch + srcX * 3);
	#ifdef TARGET_SSE
		for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
		{
			const uint8_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
			uint8_t* dstRow = dstPtr + y * dstPitch;

			int x = 0;
			uint32_t xPos = 0;
			for (; x <= destWidth - 4; x += 4, xPos += 4 * xStep)
			{
				int sx[4];
				for (int i = 0; i < 4; i++)
					sx[i] = (xPos + i * xStep) >> 16;

				ALIGNED_(16) uint8_t pixels[12];
				for (int i = 0; i < 4; i++)
				{
					pixels[i * 3 + 0] = srcRow[sx[i] * 3 + 0];
					pixels[i * 3 + 1] = srcRow[sx[i] * 3 + 1];
					pixels[i * 3 + 2] = srcRow[sx[i] * 3 + 2];
				}
				__m128i pixelData = _mm_loadu_si128((__m128i*)pixels);

				_mm_storel_epi64((__m128i*)(dstRow + x * 3), pixelData);
				*(uint32_t*)(dstRow + x * 3 + 8) = *(uint32_t*)(pixels + 8);
			}

			for (; x < destWidth; x++, xPos += xStep)
			{
				int sx = xPos >> 16;
				if (sx >= blitWidth) sx = blitWidth - 1;

				dstRow[x * 3 + 0] = srcRow[sx * 3 + 0];
				dstRow[x * 3 + 1] = srcRow[sx * 3 + 1];
				dstRow[x * 3 + 2] = srcRow[sx * 3 + 2];
			}
		}
	#else
		for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
		{
			const uint8_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
			uint8_t* dstRow = dstPtr + y * dstPitch;

			for (int x = 0, xPos = 0; x < destWidth; x++, xPos += xStep)
			{
				int sx = xPos >> 16;
				dstRow[x * 3 + 0] = srcRow[sx * 3 + 0];
				dstRow[x * 3 + 1] = srcRow[sx * 3 + 1];
				dstRow[x * 3 + 2] = srcRow[sx * 3 + 2];
			}
		}
	#endif
		return 1;
	}

	case 32:
	{
		int dstPitch = dst->format.width_in_pixels;
		int srcPitch = src->format.width_in_pixels;
		uint32_t* dstPtr = (uint32_t*)(dst->surface_lock_alloc + 4 * (dstX + dstY * dstPitch));
		uint32_t* srcPtr = (uint32_t*)(src->surface_lock_alloc + 4 * (srcX + srcY * srcPitch));

		if (!transparency)
		{
			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint32_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint32_t* dstRow = dstPtr + y * dstPitch;

				for (int x = 0, xPos = 0; x < destWidth; x++, xPos += xStep)
					dstRow[x] = srcRow[xPos >> 16];
			}
		}
		else
		{
#ifdef TARGET_SSE
			uint32_t transi = src->transparent_color;
			__m128i trans = _mm_set1_epi32(transi);

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint32_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint32_t* dstRow = dstPtr + y * dstPitch;

				int x = 0;
				uint32_t xPos = 0;

				for (; x <= destWidth - 4; x += 4, xPos += 4 * xStep)
				{
					uint32_t tmp[4];
					for (int i = 0; i < 4; i++)
						tmp[i] = srcRow[(xPos + i * xStep) >> 16];

					__m128i s = _mm_loadu_si128((__m128i*)tmp);
					__m128i d = _mm_loadu_si128((__m128i*)(dstRow + x));
					__m128i cmp = _mm_cmpeq_epi32(s, trans);
					__m128i result = _mm_or_si128(_mm_andnot_si128(cmp, s), _mm_and_si128(cmp, d));
					_mm_storeu_si128((__m128i*)(dstRow + x), result);
				}

				for (; x < destWidth; x++, xPos += xStep)
				{
					uint32_t srcPix = srcRow[xPos >> 16];
					if (srcPix != transi)
						dstRow[x] = srcPix;
				}
			}
#else
			uint32_t trans = src->transparent_color;

			for (int y = 0, yPos = 0; y < destHeight; y++, yPos += yStep)
			{
				const uint32_t* srcRow = srcPtr + (yPos >> 16) * srcPitch;
				uint32_t* dstRow = dstPtr + y * dstPitch;

				uint32_t xPos = 0;
				for (int x = 0; x < destWidth; x++, xPos += xStep)
				{
					uint32_t srcPix = srcRow[xPos >> 16];
					if (srcPix != trans)
						dstRow[x] = srcPix;
				}
			}
#endif
		}
		return 1;
	}

	default:
		// Unsupported bpp
		return 0;
}

#if 0
	rdRect srcRect;
	if (!rect)
	{
		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.width = vbuf2->format.width;
		srcRect.height = vbuf2->format.height;
	}
	else
	{
		srcRect = *rect;
	}

	// Calculate destination dimensions
	const int dstW = (int)(srcRect.width * scaleX);
	const int dstH = (int)(srcRect.height * scaleY);

	if (blit_x + dstW > vbuf->format.width || blit_y + dstH > vbuf->format.height)
		return; 

	uint8_t* srcPixels = (uint8_t*)vbuf2->surface_lock_alloc;//sdlSurface->pixels;
	uint8_t* dstPixels = (uint8_t*)vbuf->surface_lock_alloc;//sdlSurface->pixels;
	const int srcStride = vbuf2->format.width_in_bytes;
	const int dstStride = vbuf->format.width_in_bytes;

	const float invScaleX = 1.0f / scaleX;
	const float invScaleY = 1.0f / scaleY;

	for (int dstY = 0; dstY < dstH; dstY++)
	{
		const int srcY = srcRect.y + (int)(dstY * invScaleY);
		if ((uint32_t)srcY >= (uint32_t)vbuf2->format.height) continue;

		const uint8_t* srcRow = srcPixels + srcY * srcStride;
		uint8_t* dstRow = dstPixels + (blit_y + dstY) * dstStride + blit_x;

		if (!has_alpha)
		{
			// SSE-optimized path (no alpha)
			int dstX = 0;
			for (; dstX + 3 < dstW; dstX += 4)
			{
				// Compute source positions
				__m128 xOffsets = _mm_setr_ps(0, 1, 2, 3);
				__m128 srcXs = _mm_add_ps(_mm_set1_ps(dstX * invScaleX), xOffsets);
				__m128i srcXi = _mm_cvtps_epi32(srcXs);

				// Gather pixels
				uint8_t px[4];
				for (int k = 0; k < 4; k++)
				{
					int x = srcRect.x + ((int*)&srcXi)[k];
					px[k] = ((uint32_t)x < (uint32_t)vbuf2->format.width) ? srcRow[x] : 0;
				}
				_mm_storeu_si128((__m128i*)(dstRow + dstX), _mm_loadu_si128((__m128i*)px));
			}

			// Remaining pixels
			for (; dstX < dstW; dstX++)
			{
				int srcX = srcRect.x + (int)(dstX * invScaleX);
				if ((uint32_t)srcX < (uint32_t)vbuf2->format.width)
				{
					dstRow[dstX] = srcRow[srcX];
				}
			}
		}
		else
		{
			 // Alpha path (scalar)
			for (int dstX = 0; dstX < dstW; dstX++)
			{
				int srcX = srcRect.x + (int)(dstX * invScaleX);
				if ((uint32_t)srcX >= (uint32_t)vbuf2->format.width) continue;

				uint8_t px = srcRow[srcX];
				if (!(px == 0 && has_alpha))
				{
					dstRow[dstX] = px;
				}
			}
		}
	}
#endif
}

stdDisplayEnvironment* stdBuildDisplayEnvironment()
{
	stdDisplayEnvironment* result = (stdDisplayEnvironment*)std_pHS->alloc(sizeof(stdDisplayEnvironment));
	if (!result)
		return NULL;
	result->numDevices = 0;
	result->devices = NULL;

	if (stdDisplay_Startup() == 0)
	{
		std_pHS->free(result);
		return NULL;
	}

	uint32_t numDevices = stdDisplay_numDevices;
	result->numDevices = numDevices;

	if (numDevices == 0)
	{
		stdDisplay_RestoreDisplayMode();
		return result;
	}

	stdVideoDeviceEntry* devices = (stdVideoDeviceEntry*)std_pHS->alloc(numDevices * sizeof(stdVideoDeviceEntry));
	if (!devices)
	{
		stdDisplay_RestoreDisplayMode();
		std_pHS->free(result);
		return NULL;
	}
	result->devices = devices;
	
	// added
	memset(devices, 0, numDevices * sizeof(stdVideoDeviceEntry));

	int numModes;
	stdVideoMode* modes;

	for (uint32_t i = 0; i < numDevices; ++i)
	{
		stdVideoDeviceEntry* dst = &devices[i];
		//memset(dst, 0, sizeof(stdVideoDeviceEntry)); // moved
		memcpy(&dst->device, &stdDisplay_aDevices[i], sizeof(stdVideoDevice));

		if (stdDisplay_Open(i) == 0)
			goto fail;

		numModes = stdDisplay_numVideoModes;
		dst->max_modes = numModes;
		dst->field_2A4 = 0;
		dst->halDevices = NULL;

		if (numModes > 0)
		{
			modes = (stdVideoMode*)std_pHS->alloc(numModes * sizeof(stdVideoMode));
			if (!modes)
				goto fail;
			dst->videoModes = modes;
			memcpy(modes, Video_renderSurface, numModes * sizeof(stdVideoMode));

			dst->field_2A4 = 0;
			if (dst->device.video_device[0].has3DAccel)
			{
				//if (std3D_Startup() == 0)
				//	goto fail;
			
				dst->field_2A4 = std3D_d3dDeviceCount;
				if (dst->field_2A4 > 0)
				{
					stdVideoMode* modeList = std_pHS->alloc(dst->field_2A4 * sizeof(d3d_device)); // d3d_device isn't the same size? should be 0x22c?
					if (!modeList)
						goto fail;
					dst->halDevices = modeList;
					memcpy(modeList, std3D_d3dDevices, dst->field_2A4 * sizeof(d3d_device));
				}
				//std3D_Shutdown();
			}
		}

		stdDisplay_Close();
	}

	stdDisplay_RestoreDisplayMode();
	return result;

fail:
	if (result->devices)
	{
		for (uint32_t i = 0; i < result->numDevices; ++i)
		{
			stdVideoDeviceEntry* dev = &result->devices[i];
			if (dev->halDevices)
				std_pHS->free((void*)dev->halDevices);
			if (dev->videoModes)
				std_pHS->free((void*)dev->videoModes);
		}
		std_pHS->free(result->devices);
	}
	std_pHS->free(result);
	return NULL;
}

void stdFreeDisplayEnvironment(stdDisplayEnvironment* displayEnv)
{
	if (displayEnv->devices != NULL)
	{
		for (uint32_t i = 0; i < displayEnv->numDevices; ++i)
		{
			stdVideoDeviceEntry* dev = &displayEnv->devices[i];
			if (dev->halDevices)
				std_pHS->free((void*)dev->halDevices);
			if (dev->videoModes)
				std_pHS->free((void*)dev->videoModes);
		}

		std_pHS->free(displayEnv->devices);
		displayEnv->devices = NULL;
	}

	std_pHS->free(displayEnv);
}

#endif
