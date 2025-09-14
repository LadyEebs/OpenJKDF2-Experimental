#ifndef _STDBMP_H
#define _STDBMP_H

#include <stdint.h>

typedef struct stdBitmap stdBitmap;

int stdBmp_Write(const char* filename, stdBitmap* bmp);

#endif // _STDBMP_H
