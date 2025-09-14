#include "stdBitmap.h"

#include "stdPlatform.h"
#include "General/stdColor.h"
#include "Win95/stdDisplay.h"
#include "Win95/std.h"
#include "Platform/std3D.h"
#include "jk.h"

stdBitmap* stdBitmap_Load(char *fpath, int bCreateDDrawSurface, int gpuMem)
{
    stdBitmap *outAlloc; // esi
    stdBitmap *result; // eax
    intptr_t fp; // edi
    signed int v6; // ebx
    const char *v7; // eax

    outAlloc = (stdBitmap *)std_pHS->alloc(sizeof(stdBitmap));
    if (!outAlloc)
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 68, "Error: Unable to allocate memory for bitmap '%s'\n", fpath);
        return NULL;
    }


    fp = std_pHS->fileOpen(fpath, "rb");
    if ( fp )
    {
        v7 = stdFileFromPath(fpath);
        _strncpy((char *)outAlloc->fpath, v7, 0x1Fu);
        outAlloc->fpath[31] = 0;
        v6 = stdBitmap_LoadEntryFromFile(fp, outAlloc, bCreateDDrawSurface, gpuMem);
        std_pHS->fileClose(fp);
    }
    else
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 147, "Error: Invalid load filename '%s'.\n", fpath);
        v6 = 0;
    }
    if ( v6 )
    {
        result = outAlloc;
    }
    else
    {
        std_pHS->free(outAlloc);
        result = 0;
    }
    
    return result;
}

// MOTS added
stdBitmap* stdBitmap_Load2(char *fpath, int bCreateDDrawSurface, int gpuMem)
{
    return stdBitmap_Load(fpath, bCreateDDrawSurface, gpuMem);
}

stdBitmap* stdBitmap_LoadFromFile(stdFile_t fd, int bCreateDDrawSurface, int gpuMem)
{
    stdBitmap* outAlloc = (stdBitmap*)std_pHS->alloc(sizeof(stdBitmap));
    if (!outAlloc)
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 103, "Error: Unable to allocate memory for bitmap.\n", 0, 0, 0, 0);
        return NULL;
    }

    if (stdBitmap_LoadEntryFromFile(fd, outAlloc, bCreateDDrawSurface, gpuMem))
    {
        return outAlloc;
    }
    else
    {
        stdBitmap_Free(outAlloc);
        return 0;
    }
}

int stdBitmap_LoadEntry(char *fpath, stdBitmap *out, int bCreateDDrawSurface, int gpuMem)
{
    stdFile_t fd; // esi
    const char *v6; // eax
    signed int v7; // edi

    fd = std_pHS->fileOpen(fpath, "rb");
    if ( fd )
    {
        v6 = stdFileFromPath(fpath);
        _strncpy((char *)out->fpath, v6, 0x1Fu);
        out->fpath[31] = 0;
        v7 = stdBitmap_LoadEntryFromFile(fd, out, bCreateDDrawSurface, gpuMem);
        std_pHS->fileClose(fd);
        return v7;
    }
    else
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 147, "Error: Invalid load filename '%s'.\n", fpath);
        return 0;
    }
}

int stdBitmap_LoadEntryFromFile(intptr_t fp, stdBitmap *out, int bCreateDDrawSurface, int gpuMem)
{
    int palFmt; // ebp
    int numMips_; // edx
    unsigned int vbufAllocSize; // esi
    stdVBuffer **vbufAlloc; // edi
    int v12; // eax
    stdVBuffer *surface; // esi
    char *lockAlloc; // ebp
    size_t v15; // edi
    unsigned int i; // ebx
    void *palette_map; // eax
    int v18; // [esp+10h] [ebp-DCh]
    int mipCount; // [esp+10h] [ebp-DCh]
    int numMips; // [esp+14h] [ebp-D8h]
    unsigned int v21[2]; // [esp+18h] [ebp-D4h] BYREF
    bitmapHeader bmp_header; // [esp+20h] [ebp-CCh] BYREF
    stdVBufferTexFmt vbufTexFmt; // [esp+A0h] [ebp-4Ch] BYREF

    std_pHS->fileRead(fp, &bmp_header, sizeof(bitmapHeader));
    if ( _memcmp((const char *)&bmp_header, "BM  ", 4u) )
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 213, "Error: Bad signature in header of bitmap file.\n", 0, 0, 0, 0);
        return 0;
    }
    if ( bmp_header.field_4 != 70 )
    {
        stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 220, "Error: Bad version %d for bitmap file\n", bmp_header.field_4);
        return 0;
    }
    palFmt = bmp_header.palFmt;
    v18 = bmp_header.field_8;
    numMips_ = bmp_header.numMips;
    _memset(out, 0, sizeof(stdBitmap));
    vbufAllocSize = 4 * numMips_;
    numMips = numMips_;
    vbufAlloc = (stdVBuffer **)std_pHS->alloc(sizeof(stdVBuffer*) * numMips_);
    out->mipSurfaces = vbufAlloc;
    if ( vbufAlloc )
    {
        _memset(vbufAlloc, 0, vbufAllocSize);
        out->field_20 = v18;
        _memcpy(&out->format, &bmp_header.format, sizeof(out->format));
        out->palFmt = palFmt;
        out->numMips = numMips;
        out->palette = 0;
    }
    else
    {
        stdPrintf(std_pHS->messagePrint, ".\\General\\stdBitmap.c", 843, "Ran out of memory trying allocate bitmap.\n", 0, 0, 0, 0);
    }
    out->colorkey = bmp_header.colorkey;
    out->xPos = bmp_header.xPos;
    out->yPos = bmp_header.yPos;
    _memset(&vbufTexFmt, 0, sizeof(vbufTexFmt));
    for (mipCount = 0; mipCount < out->numMips; mipCount++)
    {
        std_pHS->fileRead(fp, v21, 8);
        vbufTexFmt.height = v21[1];
        vbufTexFmt.width = v21[0];

        _memcpy(&vbufTexFmt.format, &out->format, sizeof(vbufTexFmt.format));

        surface = stdDisplay_VBufferNew(&vbufTexFmt, bCreateDDrawSurface, gpuMem, 0);
        if ( !surface )
            goto LABEL_17;

        out->mipSurfaces[mipCount] = surface;
        stdDisplay_VBufferLock(surface);
        lockAlloc = surface->surface_lock_alloc;
        
        v15 = surface->format.width * ((unsigned int)surface->format.format.bpp >> 3);
        for ( i = 0; i < vbufTexFmt.height; ++i )
        {
            std_pHS->fileRead(fp, lockAlloc, v15);
            lockAlloc += surface->format.width_in_bytes;
        }
        stdDisplay_VBufferUnlock(surface);
        if ( (out->palFmt & 1) != 0 ) {
            stdDisplay_VBufferSetColorKey(surface, out->colorkey);
        }
        // TODO: Eviction caching for stdBitmap, rdMaterial
#ifdef TARGET_TWL
        if (openjkdf2_bIsExtraLowMemoryPlatform && vbufTexFmt.width == 640 && vbufTexFmt.height == 480){
            std_pHS->free(surface->surface_lock_alloc);
            surface->surface_lock_alloc = NULL;
        }
#endif
    }

    if ( (out->palFmt & 2) != 0 )
    {
        palette_map = std_pHS->alloc(0x300);
        out->palette = palette_map;
        if ( !palette_map )
        {
LABEL_17:
            stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 297, "Error: Out of memory trying to load bitmap.\n", 0, 0, 0, 0);
            return 0;
        }
        std_pHS->fileRead(fp, palette_map, 0x300);
    }

#if defined(SDL2_RENDER) && !defined(TILE_SW_RASTER)
    out->aTextureIds = (uint32_t*)std_pHS->alloc(out->numMips * sizeof(uint32_t));
    out->abLoadedToGPU = (int*)std_pHS->alloc(out->numMips * sizeof(int));
    out->paDataDepthConverted = (void**)std_pHS->alloc(out->numMips * sizeof(void*));

    memset(out->aTextureIds, 0, (out->numMips * sizeof(uint32_t)));
    memset(out->abLoadedToGPU, 0, (out->numMips * sizeof(int)));
    memset(out->paDataDepthConverted, 0, (out->numMips * sizeof(void*)));
    for (int i = 0; i < out->numMips; i++)
    {
        std3D_AddBitmapToTextureCache(out, i, !(out->palFmt & 1), 0);
    }
#endif

    return 1;
}

void stdBitmap_ConvertColorFormat(rdTexformat *formatTo, stdBitmap *bitmap)
{
    rdTexformat *formatFrom_; // eax
    int v4; // esi
    stdVBuffer *v5; // eax
    rdTexformat *formatFrom; // [esp+18h] [ebp+8h]

    formatFrom_ = &bitmap->format;
    formatFrom = &bitmap->format;
    if ( _memcmp(formatTo, formatFrom, sizeof(rdTexformat)) && (formatFrom_->colorMode || formatTo->colorMode) )
    {
        v4 = 0;
        if ( bitmap->numMips > 0 )
        {
            do
            {
                v5 = stdDisplay_VBufferConvertColorFormat(formatTo, bitmap->mipSurfaces[v4]);
                bitmap->mipSurfaces[v4] = v5;
                if ( !v5 )
                    ((void (__cdecl *)(const char *, const char *, int))std_pHS->assert)(
                        "Unable to allocate a new frame when converting image from 24 to 16bpp.",
                        ".\\General\\stdBitmap.c",
                        570);
                if ( (bitmap->palFmt & 1) != 0 )
                    stdDisplay_VBufferSetColorKey(bitmap->mipSurfaces[v4], bitmap->mipSurfaces[v4]->transparent_color);
                ++v4;
            }
            while ( v4 < bitmap->numMips );
            formatFrom_ = formatFrom;
        }
        if ( (bitmap->palFmt & 1) != 0 )
        {
            bitmap->colorkey = stdColor_ColorConvertOnePixel(formatTo, bitmap->colorkey, formatFrom_);
            formatFrom_ = formatFrom;
        }
        _memcpy(formatFrom_, formatTo, sizeof(rdTexformat));
    }
}

void stdBitmap_Free(stdBitmap *pBitmap)
{
    unsigned int i; // esi
    
    // Added: nullptr check
    if (!pBitmap) return;

#if defined(SDL2_RENDER) && !defined(TILE_SW_RASTER)
    std3D_PurgeBitmapRefs(pBitmap);
    std_pHS->free(pBitmap->aTextureIds);
    pBitmap->aTextureIds = NULL;
    std_pHS->free(pBitmap->abLoadedToGPU);
    pBitmap->abLoadedToGPU = NULL;
    std_pHS->free(pBitmap->paDataDepthConverted);
    pBitmap->paDataDepthConverted = NULL;
#endif

    if ( pBitmap->mipSurfaces )
    {
        for ( i = 0; i < pBitmap->numMips; ++i )
        {
            if ( pBitmap->mipSurfaces[i] )
                stdDisplay_VBufferFree(pBitmap->mipSurfaces[i]);
        }
        std_pHS->free(pBitmap->mipSurfaces);
    }
    if ( pBitmap->palette )
        std_pHS->free(pBitmap->palette);
    //stdPrintf(std_pHS->debugPrint, ".\\General\\stdBitmap.c", 359, "Bitmap elements successfully freed.\n", 0, 0, 0, 0);
    std_pHS->free(pBitmap);
    //stdPrintf(std_pHS->debugPrint, ".\\General\\stdBitmap.c", 322, "Bitmap successfully freed.\n", 0, 0, 0, 0);
}

stdBitmap* stdBitmap_VBufferToBitmap(stdVBuffer* src, int a1, int a2)
{
	if (!src) return NULL;

	// Allocate the bitmap object
	stdBitmap* bmp = (stdBitmap*)(*std_pHS->alloc)(sizeof(stdBitmap));
	if (!bmp) return NULL;

	bmp->field_20 = 0;
	bmp->palFmt = (src->palette ? 2 : 0);
	bmp->numMips = 1;
	bmp->field_68 = 0;
	bmp->xPos = 0;
	bmp->yPos = 0;

	// Allocate space for mip surface array (1 entry)
	bmp->mipSurfaces = (stdVBuffer**)(*std_pHS->alloc)(sizeof(stdVBuffer*));
	if (!bmp->mipSurfaces)
	{
		return NULL;
	}

	// Create new vbuffer and copy from source
	stdVBuffer* vbuf = stdDisplay_VBufferNew(&src->format, a1, a2, NULL);
	*bmp->mipSurfaces = vbuf;
	if (!vbuf)
	{
		return NULL;
	}

	stdDisplay_VBufferCopy(vbuf, src, 0, 0, NULL, 0);
	stdDisplay_VBufferUnlock(src);

	// Copy palette if present
	if (bmp->palFmt & 2)
	{
		bmp->palette = (*std_pHS->alloc)(0x300); // 768 bytes
		if (!bmp->palette)
		{
			return NULL;
		}

		uint32_t* dst = (uint32_t*)bmp->palette;
		uint32_t* srcPal = (uint32_t*)src->palette;
		for (int i = 0; i < 0xC0; i++)
		{ // 192 * 4 = 768
			*dst++ = *srcPal++;
		}
	}
	else
	{
		bmp->palette = NULL;
	}

	return bmp;
}

/*
stdBitmap* stdBitmap_NewEntryFromRGBA(uint8_t* pixels, uint32_t width, uint32_t height, int bCreateDDrawSurface, int gpuMem)
{
	int palFmt; // ebp
	int numMips_; // edx
	unsigned int vbufAllocSize; // esi
	stdVBuffer** vbufAlloc; // edi
	int v12; // eax
	stdVBuffer* surface; // esi
	char* lockAlloc; // ebp
	size_t v15; // edi
	unsigned int i; // ebx
	void* palette_map; // eax
	int v18; // [esp+10h] [ebp-DCh]
	int mipCount; // [esp+10h] [ebp-DCh]
	int numMips; // [esp+14h] [ebp-D8h]
	unsigned int v21[2]; // [esp+18h] [ebp-D4h] BYREF

	stdVBufferTexFmt vbufTexFmt; // [esp+A0h] [ebp-4Ch] BYREF

	rdTexformat format;
	format.bpp = 32;
	format.colorMode = 2;
	format.r_bits = 8;
	format.g_bits = 8;
	format.b_bits = 8;
	format.r_shift = 0;
	format.g_shift = 8;
	format.b_shift = 16;
	format.r_bitdiff = 0;
	format.g_bitdiff = 0;
	format.b_bitdiff = 0;
	format.unk_40 = 8;
	format.unk_44 = 24;
	format.unk_48 = 0;

	palFmt = 0;
	numMips_ = 1;

	stdBitmap* out = (stdBitmap*)std_pHS->alloc(sizeof(stdBitmap));
	if (!out)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 103, "Error: Unable to allocate memory for bitmap.\n", 0, 0, 0, 0);
		return NULL;
	}
	_memset(out, 0, sizeof(stdBitmap));
	vbufAllocSize = 4 * numMips_;
	numMips = numMips_;
	vbufAlloc = (stdVBuffer**)std_pHS->alloc(sizeof(stdVBuffer*) * numMips_);
	out->mipSurfaces = vbufAlloc;
	if (vbufAlloc)
	{
		_memset(vbufAlloc, 0, vbufAllocSize);
		out->field_20 =0;// v18;
		_memcpy(&out->format, &format, sizeof(out->format));
		out->palFmt = palFmt;
		out->numMips = numMips;
		out->palette = 0;
	}
	else
	{
		stdPrintf(std_pHS->messagePrint, ".\\General\\stdBitmap.c", 843, "Ran out of memory trying allocate bitmap.\n", 0, 0, 0, 0);
	}
	out->colorkey = 0;
	out->xPos = 0;
	out->yPos = 0;
	_memset(&vbufTexFmt, 0, sizeof(vbufTexFmt));
	{
		vbufTexFmt.height = height;
		vbufTexFmt.width = width;

		_memcpy(&vbufTexFmt.format, &out->format, sizeof(vbufTexFmt.format));

		surface = stdDisplay_VBufferNew(&vbufTexFmt, bCreateDDrawSurface, gpuMem, 0);
		if (!surface)
		{
			stdPrintf(std_pHS->errorPrint, ".\\General\\stdBitmap.c", 297, "Error: Out of memory trying to create bitmap.\n", 0, 0, 0, 0);
			std_pHS->free(out);
			return NULL;
		}

		out->mipSurfaces[0] = surface;
		stdDisplay_VBufferLock(surface);
		lockAlloc = surface->surface_lock_alloc;
		memcpy(lockAlloc, pixels, sizeof(uint32_t) * width * height);
		stdDisplay_VBufferUnlock(surface);
		if ((out->palFmt & 1) != 0)
			stdDisplay_VBufferSetColorKey(surface, out->colorkey);
	}

#ifdef SDL2_RENDER
	out->aTextureIds = (uint32_t*)std_pHS->alloc(out->numMips * sizeof(uint32_t));
	out->abLoadedToGPU = (int*)std_pHS->alloc(out->numMips * sizeof(int));
	out->paDataDepthConverted = (void**)std_pHS->alloc(out->numMips * sizeof(void*));

	memset(out->aTextureIds, 0, (out->numMips * sizeof(uint32_t)));
	memset(out->abLoadedToGPU, 0, (out->numMips * sizeof(int)));
	memset(out->paDataDepthConverted, 0, (out->numMips * sizeof(void*)));
	for (int i = 0; i < out->numMips; i++)
	{
		std3D_AddBitmapToTextureCache(out, i, !(out->palFmt & 1), 0);
	}
#endif

	return out;
}
*/