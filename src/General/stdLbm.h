#ifndef _STDLBM_H
#define _STDLBM_H

#include "types.h"
#include "globals.h"

#define stdLbm_Load_ADDR (0x00439570)
#define stdLbm_Write_ADDR (0x00439C10)
#define stdLbm_Compress_ADDR (0x0043A030)

stdBitmap* stdLbm_Load(const char *fpath, int create_ddraw_surface, int gpu_mem);
int stdLbm_Write(const char *fpath, stdBitmap *bitmap);
void stdLbm_Compress(char byte);

#endif // _STDLBM_H
