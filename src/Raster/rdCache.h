#ifndef _RDCACHE_H
#define _RDCACHE_H

#include "types.h"
#include "globals.h"
#include "Engine/rdMaterial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define rdCache_Startup_ADDR (0x0043AD60)
#define rdCache_AdvanceFrame_ADDR (0x0043AD70)
#define rdCache_FinishFrame_ADDR (0x0043AD80)
#define rdCache_Reset_ADDR (0x0043AD90)
#define rdCache_ClearFrameCounters_ADDR (0x0043ADD0)
#define rdCache_GetProcEntry_ADDR (0x0043ADE0)
#define rdCache_Flush_ADDR (0x0043AE70)
#define rdCache_AddProcFace_ADDR (0x0043AF90)
#define rdCache_SendFaceListToHardware_ADDR (0x0043B1C0)
#define rdCache_ResetRenderList_ADDR (0x0043C2C0)
#define rdCache_DrawRenderList_ADDR (0x0043C2E0)
#define rdCache_TriCompare_ADDR (0x0043C380)
#define rdCache_DrawFaceN_ADDR (0x0043C3C0)
#define rdCache_DrawFaceZ_ADDR (0x0043CED0)
#define rdCache_DrawFaceUser_ADDR (0x0043D9E0)
#define rdCache_ProcFaceCompareByDistance_ADDR (0x0043E170)

int rdCache_Startup();
MATH_FUNC void rdCache_AdvanceFrame();
MATH_FUNC void rdCache_FinishFrame();
MATH_FUNC void rdCache_Reset();
void rdCache_ClearFrameCounters();
rdProcEntry *rdCache_GetProcEntry();
MATH_FUNC FAST_FUNC int rdCache_SendFaceListToHardware();

void rdCache_ResetRenderList();
MATH_FUNC void rdCache_DrawRenderList();
#ifndef RDCACHE_RENDER_NGONS
int rdCache_TriCompare(const void* a_, const void* b_);
#else
int rdCache_NGonCompare(const void* a_, const void* b_);
#endif

#ifdef RENDER_DROID2
MATH_FUNC void rdCache_Flush(const char* label);

void rdCache_AddDrawCall(rdPrimitiveType_t type, std3D_DrawCallState* pDrawCallState, rdVertex* paVertices, int numVertices);

#else
MATH_FUNC void rdCache_Flush();
#endif

int rdCache_ProcFaceCompareByDistance(rdProcEntry *a, rdProcEntry *b);
#ifdef QOL_IMPROVEMENTS
int rdCache_ProcFaceCompareByState(rdProcEntry* a, rdProcEntry* b);
#endif
MATH_FUNC int rdCache_AddProcFace(int a1, unsigned int num_vertices, char flags);

#ifdef DECAL_RENDERING
void rdCache_FlushDecals();
void rdCache_DrawDecal(rdDecal* decal, rdMatrix34* matrix, rdVector3* color, rdVector3* scale, float angleFade);
#endif

#ifdef PARTICLE_LIGHTS
void rdCache_FlushLights();
void rdCache_DrawLight(rdLight* light, rdVector3* position);
#endif

#ifdef SPHERE_AO
void rdCache_DrawOccluder(rdVector3* position, float radius);
void rdCache_FlushOccluders();
#endif

#ifndef __cplusplus

#ifndef TILE_SW_RASTER
static void (*rdCache_DrawFaceUser)(rdProcEntry* face) = (void*)rdCache_DrawFaceUser_ADDR;
static void (*rdCache_DrawFaceN)(rdProcEntry* face) = (void*)rdCache_DrawFaceN_ADDR;
static void (*rdCache_DrawFaceZ)(rdProcEntry* face) = (void*)rdCache_DrawFaceZ_ADDR;
#endif
//static int (*rdCache_SendFaceListToHardware)(void) = (void*)rdCache_SendFaceListToHardware_ADDR;
//static void (*rdCache_ClearFrameCounters)(void) = (void*)rdCache_ClearFrameCounters_ADDR;
//static void (*rdCache_AdvanceFrame)(void) = (void*)rdCache_AdvanceFrame_ADDR;
//static void (*rdCache_Flush)(void) = (void*)rdCache_Flush_ADDR;
//static void (*rdCache_FinishFrame)(void) = (void*)rdCache_FinishFrame_ADDR;

//static rdProcEntry* (*rdCache_GetProcEntry)(void) = (void*)rdCache_GetProcEntry_ADDR;
//static int (*__cdecl rdCache_AddProcFace)(int extdata, unsigned int numVertices, char flags) = (void*)rdCache_AddProcFace_ADDR;
#endif

#ifdef __cplusplus
}
#endif

#endif // _RDCACHE_H
