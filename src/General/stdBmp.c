#include "stdPcx.h"

#include "Engine/rdMaterial.h"
#include "General/stdBitmap.h"
#include "stdPlatform.h"
#include "jk.h"
#include "Win95/stdDisplay.h"
#include "Win95/std.h"

int stdBmp_Write(const char* filename, stdBitmap* bmp)
{
	if (!bmp || !bmp->mipSurfaces || !*bmp->mipSurfaces)
		return 0;

	stdVBuffer* vbuf = *bmp->mipSurfaces;
	int width = vbuf->format.width;
	int height = vbuf->format.height;
	int bpp = vbuf->format.format.bpp;

	// Only 24bpp truecolor or paletted modes are supported
	size_t paletteSize = 0;
	if (vbuf->format.format.colorMode)
	{
		if (bpp != 24)
		{
			std_pHS->assert("Only 24bpp supported", "stdBmp.c", 0x1c8);
			return 0;
		}
	}
	else
	{
		// Palette present (e.g., 8bpp)
		paletteSize = (1u << bpp) * 4; // BMP palette entries are 4 bytes each
	}

	size_t rowSize = (width * bpp) >> 3;
	size_t imageSize = rowSize * height;
	size_t fileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + paletteSize + imageSize;

	// Build headers
	BITMAPFILEHEADER fh = { 0 };
	fh.bfType = 0x4D42; // 'BM'
	fh.bfSize = fileSize;
	fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + paletteSize;

	BITMAPINFOHEADER ih = { 0 };
	ih.biSize = sizeof(BITMAPINFOHEADER);
	ih.biWidth = width;
	ih.biHeight = height;
	ih.biPlanes = 1;
	ih.biBitCount = (uint16_t)bpp;
	ih.biSizeImage = imageSize;
	ih.biXPelsPerMeter = 0xb12; // just a fixed DPI constant
	ih.biYPelsPerMeter = 0xb12;

	stdFile_t f = (*std_pHS->fileOpen)(filename, "wb");
	if (!f)
	{
		stdPrintf(std_pHS->errorPrint, "stdBmp.c", 0x1fb,
				  "Unable to open file '%s' for writing.", filename);
		return 0;
	}

	// Write headers
	if ((*std_pHS->fileWrite)(f, &fh, sizeof(fh)) != sizeof(fh) ||
		(*std_pHS->fileWrite)(f, &ih, sizeof(ih)) != sizeof(ih))
	{
		stdPrintf(std_pHS->errorPrint, "stdBmp.c", 0x204,
				  "Error writing BMP headers to '%s'", filename);
		(*std_pHS->fileClose)(f);
		return 0;
	}

	// Write palette if needed
	if (paletteSize > 0)
	{
		if ((*std_pHS->fileWrite)(f, bmp->palette, paletteSize) != paletteSize)
		{
			stdPrintf(std_pHS->errorPrint, "stdBmp.c", 0x21c,
					  "Error writing %zu bytes of palette to '%s'", paletteSize, filename);
			(*std_pHS->fileClose)(f);
			return 0;
		}
	}

	// Write pixel data bottom-up
	for (int y = height - 1; y >= 0; y--)
	{
		uint8_t* row = vbuf->surface_lock_alloc + y * vbuf->format.width_in_bytes;
		if ((*std_pHS->fileWrite)(f, row, rowSize) != rowSize)
		{
			stdPrintf(std_pHS->errorPrint, "stdBmp.c", 0x22d,
					  "Error writing %zu bytes of pixel data to '%s'", rowSize, filename);
			(*std_pHS->fileClose)(f);
			return 0;
		}
	}

	(*std_pHS->fileClose)(f);
	return 1;
}