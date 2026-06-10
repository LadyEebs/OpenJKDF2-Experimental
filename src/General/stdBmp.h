#ifndef _STDBMP_H
#define _STDBMP_H

#include <stdint.h>

#include "types.h"
#include "globals.h"

#define stdBmp_Load_ADDR (0x0042C3A0)
#define stdBmp_LoadEntryFromFile_ADDR (0x0042C3E0)
#define stdBmp_Write_ADDR (0x0042C830)

#pragma pack(push, 1)
typedef struct stdBmp_Header
{
    uint16_t magic;       // 'BM'
    uint32_t fileSize;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t dataOffset;
} stdBmp_Header;

typedef struct stdBmp_InfoHeader
{
    uint32_t headerSize;  // 0x28
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t imageSize;
    int32_t xPpm;
    int32_t yPpm;
    uint32_t colorsUsed;
    uint32_t colorsImportant;
} stdBmp_InfoHeader;
#pragma pack(pop)

stdBitmap* stdBmp_Load(const char *fpath, int create_ddraw_surface, int gpu_mem);
int stdBmp_LoadEntryFromFile(const char *fpath, stdBitmap *bitmap, int create_ddraw_surface, int gpu_mem);
int stdBmp_Write(const char *fpath, stdBitmap *bitmap);
#endif // _STDBMP_H
