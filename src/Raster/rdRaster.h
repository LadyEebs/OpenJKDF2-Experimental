#ifndef _RDRASTER_H
#define _RDRASTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "globals.h"

#define rdRaster_Startup_ADDR (0x0044BB40)

MATH_FUNC void rdRaster_Startup();

//static int (*rdRaster_Startup)(void) = (void*)rdRaster_Startup_ADDR;
#ifdef TILE_SW_RASTER

void rdRaster_StartBinning();
void rdRaster_EndBinning();
void rdRaster_ClearBins();
void rdRaster_BinFaceCoarse(rdProcEntry* face);
void rdRaster_BinFacesCoarse(rdProcEntry* faces, int numFaces);
void rdRaster_BinFaces();
#endif

#ifdef __cplusplus
}
#endif

#endif // _RDRASTER_H
