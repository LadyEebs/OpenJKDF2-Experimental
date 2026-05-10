#include "rdCache.h"

#include "rdRaster.h"
#include "Engine/rdroid.h"
#include "Engine/rdActive.h"
#include "Platform/std3D.h"
#include "Engine/rdColormap.h"
#include "General/stdMath.h"
#include "stdPlatform.h"

#include <math.h>

#ifdef QOL_IMPROVEMENTS
int rdroid_curVertexColorMode = 0; // MOTS added
#endif

int rdCache_cacheFlushes = 0;

#ifdef RENDER_DROID2

#include "Modules/std/stdProfiler.h"

typedef struct rdCache_DrawCallList
{
	int              bSorted;
	int              bPosOnly;
	uint32_t         drawCallCount;
	uint32_t         drawCallIndexCount;
	uint32_t         drawCallVertexCount;
	std3D_DrawCall*  drawCalls;
	uint16_t*        drawCallIndices;
	rdVertex*        drawCallVertices;
	rdVertexBase*    drawCallPosVertices;
	std3D_DrawCall** drawCallPtrs;
} rdCache_DrawCallList;

rdCache_DrawCallList rdCache_drawCalls;
uint16_t rdCache_drawCallIndicesSorted[RD_CACHE_MAX_DRAW_CALL_INDICES];

std3D_DrawCallState rdCache_lastState;

typedef int(*rdCache_SortFunc)(std3D_DrawCall*, std3D_DrawCall*);

int rdCache_DrawCallCompareSortKey(std3D_DrawCall** a, std3D_DrawCall** b)
{
	if ((*a)->sortKey > (*b)->sortKey)
		return 1;
	if ((*a)->sortKey < (*b)->sortKey)
		return -1;
	return 0;
}

int rdCache_DrawCallCompareDepth(std3D_DrawCall** a, std3D_DrawCall** b)
{
	if ((*a)->state.header.sortDistance > (*b)->state.header.sortDistance)
		return 1;
	if ((*a)->state.header.sortDistance < (*b)->state.header.sortDistance)
		return -1;
	return rdCache_DrawCallCompareSortKey(a, b);
}


void rdCache_InitDrawList(rdCache_DrawCallList* pList)
{
	memset(pList, 0, sizeof(rdCache_DrawCallList));

	pList->drawCalls = malloc(sizeof(std3D_DrawCall) * RD_CACHE_MAX_DRAW_CALLS);
	pList->drawCallIndices = malloc(sizeof(uint16_t) * RD_CACHE_MAX_DRAW_CALL_INDICES);
	pList->drawCallPtrs = malloc(sizeof(std3D_DrawCall*) * RD_CACHE_MAX_DRAW_CALLS);

	if (pList->bPosOnly)
		pList->drawCallPosVertices = malloc(sizeof(rdVertexBase) * RD_CACHE_MAX_DRAW_CALL_VERTS);
	else
		pList->drawCallVertices = malloc(sizeof(rdVertex) * RD_CACHE_MAX_DRAW_CALL_VERTS);
}

void rdCache_FreeDrawList(rdCache_DrawCallList* pList)
{
	SAFE_DELETE(pList->drawCalls);
	SAFE_DELETE(pList->drawCallIndices);
	SAFE_DELETE(pList->drawCallVertices);
	SAFE_DELETE(pList->drawCallPosVertices);
	SAFE_DELETE(pList->drawCallPtrs);
}

uint64_t rdCache_GetSortKey(std3D_DrawCall* pDrawCall)
{
	int textureID = pDrawCall->state.textureState.pTexture ? pDrawCall->state.textureState.pTexture->texture_id : 0;

	uint64_t offset = 0ull;

	uint64_t sortKey = 0;
	sortKey |= pDrawCall->state.stateBits.data & 0xFFFFFFFF;
	sortKey |= ((uint64_t)textureID & 0xFFFF) << 32ull;
	sortKey |= ((uint64_t)pDrawCall->shaderID & 0x4) << 48ull;
	if (pDrawCall->state.shaderState.shader)
		sortKey |= ((uint64_t)pDrawCall->state.shaderState.shader->shaderid & 0xFF) << 52ull;
	sortKey |= ((uint64_t)pDrawCall->state.header.sortOrder & 0xF) << 60ull;

	return sortKey;
}

void rdCache_AddListVertices(std3D_DrawCall* pDrawCall, rdPrimitiveType_t type, rdCache_DrawCallList* pList, rdVertex* paVertices, int numVertices)
{
	int firstIndex = pList->drawCallIndexCount;
	int firstVertex = pList->drawCallVertexCount;

	// copy the vertices
	if (pList->bPosOnly)
	{
		for (int i = 0; i < numVertices; ++i)
		{
			pList->drawCallPosVertices[firstVertex + i].x = paVertices[i].x;
			pList->drawCallPosVertices[firstVertex + i].y = paVertices[i].y;
			pList->drawCallPosVertices[firstVertex + i].z = paVertices[i].z;
			//pList->drawCallPosVertices[firstVertex + i].norm10a2 = paVertices->norm10a2;
		}
	}
	else
	{
		memcpy(&pList->drawCallVertices[firstVertex], paVertices, sizeof(rdVertex) * numVertices);
	}
	pList->drawCallVertexCount += numVertices;

	// generate indices
	if (numVertices <= 3)
	{
		// single triangle fast path
		pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + 0;
		pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + 1;
		pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + 2;
	}
	else if (type == RD_PRIMITIVE_TRIANGLES)
	{
		// generate triangle indices directly
		int tris = numVertices / 3;
		for (int i = 0; i < tris; ++i)
		{
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i * 3 + 0;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i * 3 + 1;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i * 3 + 2;
		}
	}
	else if (type == RD_PRIMITIVE_TRIANGLE_FAN)
	{
		// build indices from a single corner vertex
		int verts = numVertices - 2;
		for (int i = 0; i < verts; i++)
		{
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + 0;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i + 1;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i + 2;
		}
	}
	else if (type == RD_PRIMITIVE_POLYGON)
	{
		// build indices through simple triangulation
		int verts = numVertices - 2;
		int i1 = 0;
		int i2 = 1;
		int i3 = numVertices - 1;
		for (int i = 0; i < verts; ++i)
		{
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i1;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i2;
			pList->drawCallIndices[pList->drawCallIndexCount++] = firstVertex + i3;
			if ((i & 1) != 0)
				i1 = i3--;
			else
				i1 = i2++;
		}
	}

	pDrawCall->firstIndex = firstIndex;
	pDrawCall->numIndices = pList->drawCallIndexCount - firstIndex;
}

void rdCache_AddListDrawCall(rdPrimitiveType_t type, rdCache_DrawCallList* pList, std3D_DrawCallState* pDrawCallState, int shaderID, rdVertex* paVertices, int numVertices)
{
	if (pList->drawCallCount + 1 > RD_CACHE_MAX_DRAW_CALLS)
		rdCache_Flush("rdCache_AddListDrawCall");

	if (pList->drawCallVertexCount + numVertices > RD_CACHE_MAX_DRAW_CALL_VERTS)
		rdCache_Flush("rdCache_AddListDrawCall");

	std3D_DrawCall* pDrawCall = &pList->drawCalls[pList->drawCallCount++];
	pDrawCall->sortKey = rdCache_GetSortKey(pDrawCallState);
	pDrawCall->state = *pDrawCallState;
	pDrawCall->shaderID = shaderID;

	rdCache_AddListVertices(pDrawCall, type, pList, paVertices, numVertices);
}

void rdCache_AddDrawCall(rdPrimitiveType_t type, std3D_DrawCallState* pDrawCallState, rdVertex* paVertices, int numVertices)
{
	if (pDrawCallState->rasterState.colorMask == 0) // no color write
	{
		pDrawCallState->stateBits.fogMode = 0;
		pDrawCallState->stateBits.blend = 0;
		pDrawCallState->stateBits.srdBlend = 1;
		pDrawCallState->stateBits.dstBlend = 0;
		pDrawCallState->stateBits.lightMode = 0;

		memset(&pDrawCallState->fogState, 0, sizeof(std3D_FogState));
		memset(&pDrawCallState->lightingState, 0, sizeof(std3D_LightingState));

		// draw it at the end
		pDrawCallState->header.sortOrder = 255;
	}

	rdCache_AddListDrawCall(type, &rdCache_drawCalls, pDrawCallState, 0, paVertices, numVertices);
}

uint16_t* rdCache_SortDrawCallIndices(rdCache_DrawCallList* pList)
{
	STD_BEGIN_PROFILER_LABEL();
	int drawCallIndexCount = 0;
	for (int i = 0; i < pList->drawCallCount; ++i)
	{
		memcpy(&rdCache_drawCallIndicesSorted[drawCallIndexCount], &pList->drawCallIndices[pList->drawCallPtrs[i]->firstIndex], sizeof(uint16_t) * pList->drawCallPtrs[i]->numIndices);
		drawCallIndexCount += pList->drawCallPtrs[i]->numIndices;
	}
	STD_END_PROFILER_LABEL();
	return rdCache_drawCallIndicesSorted;
}

void rdCache_SortDrawCallList(rdCache_DrawCallList* pList, rdCache_SortFunc sortFunc)
{
	STD_BEGIN_PROFILER_LABEL();
	//if(!pList->bSorted)
	_qsort(pList->drawCallPtrs, pList->drawCallCount, sizeof(std3D_DrawCall*), (int(__cdecl*)(const void*, const void*))sortFunc);
	pList->bSorted = 1;
	STD_END_PROFILER_LABEL();
}

uint32_t rdCache_CheckStateBits(std3D_DrawCall* pDrawCall)
{
	uint32_t dirtyBits = 0;
	//dirtyBits |= (lastShader != pDrawCall->shaderID) ? STD3D_SHADER : 0;
	//dirtyBits |= (last_tex != texid) ? STD3D_TEXTURE : 0;
	dirtyBits |= (rdCache_lastState.stateBits.data != pDrawCall->state.stateBits.data) ? RD_CACHE_STATEBITS : 0; // todo: this probably triggers too many updates, make it more granular
	dirtyBits |= (memcmp(&rdCache_lastState.fogState, &pDrawCall->state.fogState, sizeof(std3D_FogState)) != 0) ? RD_CACHE_FOG : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.rasterState, &pDrawCall->state.rasterState, sizeof(std3D_RasterState)) != 0) ? RD_CACHE_RASTER : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.textureState, &pDrawCall->state.textureState, sizeof(std3D_TextureState)) != 0) ? RD_CACHE_TEXTURE : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.materialState, &pDrawCall->state.materialState, sizeof(std3D_MaterialState)) != 0) ? RD_CACHE_TEXTURE : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.lightingState, &pDrawCall->state.lightingState, sizeof(std3D_LightingState)) != 0) ? RD_CACHE_LIGHTING : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.transformState, &pDrawCall->state.transformState, sizeof(std3D_TransformState)) != 0) ? RD_CACHE_TRANSFORM : 0;
	dirtyBits |= (memcmp(&rdCache_lastState.shaderState, &pDrawCall->state.shaderState, sizeof(std3D_ShaderState)) != 0) ? RD_CACHE_SHADER_ID : 0;
	return dirtyBits;
}

void rdCache_FlushDrawCallList(rdCache_DrawCallList* pList, const char* debugName)
{
	if (!pList->drawCallCount)
		return;

	STD_BEGIN_PROFILER_LABEL();
//	std3D_PushDebugGroup(debugName);

	// sort draw calls to reduce state changes and maximize batching
	uint16_t* indexArray;
	//if (rdroid_curSortingMethod)
	{
		if (rdroid_curSortingMethod == 2)
			rdCache_SortDrawCallList(pList, rdCache_DrawCallCompareDepth);
		else
			rdCache_SortDrawCallList(pList, rdCache_DrawCallCompareSortKey);

		// batching needs to follow the draw order, but index array becomes disjointed after sorting
		// build a sorted list of indices to ensure sequential access during batching
		indexArray = rdCache_SortDrawCallIndices(pList);
	}
	//else
	//{
	//	indexArray = pList->drawCallIndices;
	//}

	std3D_DrawCall* pDrawCall = pList->drawCallPtrs[0];
	std3D_DrawCallState* pState = &pDrawCall->state;

	memcpy(&rdCache_lastState, &pDrawCall->state, sizeof(std3D_DrawCallState));

	std3D_AdvanceFrame();

	std3D_SetState(pState, 0xFFFFFFFF);

	if (pList->bPosOnly)
		std3D_SendVerticesToHardware(pList->drawCallPosVertices, pList->drawCallVertexCount, sizeof(rdVertexBase));
	else
		std3D_SendVerticesToHardware(pList->drawCallVertices, pList->drawCallVertexCount, sizeof(rdVertex));

	std3D_SendIndicesToHardware(indexArray, pList->drawCallIndexCount, sizeof(uint16_t));
	
	int batchIndices = 0;
	int indexOffset = 0;
	for (int j = 0; j < pList->drawCallCount; j++)
	{
		pDrawCall = pList->drawCallPtrs[j];
		pState = &pDrawCall->state;

		int texid = pState->textureState.pTexture ? pState->textureState.pTexture->texture_id : -1;

		uint32_t dirtyBits = rdCache_CheckStateBits(pDrawCall);
		if (dirtyBits)
		{
			// draw the batch
			std3D_DrawElements(rdCache_lastState.stateBits.geoMode, batchIndices, indexOffset, sizeof(uint16_t));

			// set the state for the next batch
			std3D_SetState(pState, dirtyBits);

			// update last state and reset batch
			memcpy(&rdCache_lastState, &pDrawCall->state, sizeof(std3D_DrawCallState));
			indexOffset += batchIndices;
			batchIndices = 0;
		}

		batchIndices += pDrawCall->numIndices;
	}

	if (batchIndices)
		std3D_DrawElements(pDrawCall->state.stateBits.geoMode, batchIndices, indexOffset, sizeof(uint16_t));

	std3D_ResetState();

	std3D_PopDebugGroup();
	STD_END_PROFILER_LABEL();
}

void rdCache_FlushDrawCalls(const char* label)
{
	STD_BEGIN_PROFILER_LABEL();
	std3D_PushDebugGroup(label);

	rdCache_FlushDrawCallList(&rdCache_drawCalls, "Flush");

	std3D_PopDebugGroup();
	STD_END_PROFILER_LABEL();
}

int rdCache_Startup()
{
	//rdCache_drawCallLists[i].bPosOnly = (i <= DRAW_LIST_Z_REFRACTION);
	rdCache_InitDrawList(&rdCache_drawCalls);
	rdCache_Reset();

	return 1;
}

void rdCache_Shutdown()
{
	rdCache_FreeDrawList(&rdCache_drawCalls);
}

void rdCache_AdvanceFrame()
{
	rdroid_curAcceleration = 1;

	//memset(&rdCache_drawCalls, 0, sizeof(rdCache_DrawCallList));

	std3D_StartScene();
}

void rdCache_FinishFrame()
{
	std3D_EndScene();
}

void rdCache_Reset()
{
	rdCache_DrawCallList* pList = &rdCache_drawCalls;
	pList->bSorted = 0;
	pList->drawCallCount = 0;
	pList->drawCallIndexCount = 0;
	pList->drawCallVertexCount = 0;

	// store pointers for sorting
	std3D_DrawCall* drawCalls = pList->drawCalls;
	std3D_DrawCall** drawCallPtrs = pList->drawCallPtrs;
	for (int k = 0; k < RD_CACHE_MAX_DRAW_CALLS; ++k)
		drawCallPtrs[k] = &drawCalls[k];
}

void rdCache_ClearFrameCounters()
{
	rdCache_drawnFaces = 0;
}

void rdCache_Flush(const char* label)
{
	rdCache_FlushDrawCalls(label);
	rdCache_drawnFaces += rdCache_numProcFaces;
	rdCache_Reset();
}

#else

#ifdef QOL_IMPROVEMENTS
static int rdCache_totalLines = 0;
static rdLine rdCache_aHWLines[1024];
#endif

#ifdef JKM_LIGHTING
static flex_t rdCache_aRedIntensities[RDCACHE_MAX_VERTICES];
static flex_t rdCache_aGreenIntensities[RDCACHE_MAX_VERTICES];
static flex_t rdCache_aBlueIntensities[RDCACHE_MAX_VERTICES];
#endif

// Added: ability to redirect (ex. hardware memory)
static rdVector3* rdCache_paVertices = rdCache_aVertices;
static rdVector2* rdCache_paTexVertices = rdCache_aTexVertices;
static flex_t* rdCache_paIntensities = rdCache_aIntensities;
#ifdef JKM_LIGHTING
static flex_t* rdCache_paRedIntensities = rdCache_aRedIntensities;
static flex_t* rdCache_paGreenIntensities = rdCache_aGreenIntensities;
static flex_t* rdCache_paBlueIntensities = rdCache_aBlueIntensities;
#endif

#ifdef VIEW_SPACE_GBUFFER
rdVector3 rdCache_aVerticesVS[RDCACHE_MAX_VERTICES] = { 0 };
#endif

#ifdef DECAL_RENDERING
rdVector3 rdCache_aDecalColors[256];
rdMatrix34 rdCache_aDecalMatrices[256];
rdDecal* rdCache_aDecals[256];
rdVector3 rdCache_aDecalScales[256];
float rdCache_aDecalAngleFades[256];
int rdCache_numDecals;

void rdCache_FlushDecals();
#endif

#ifdef PARTICLE_LIGHTS
rdLight rdCache_aLights[4096];
rdVector3 rdCache_aLightPositions[4096];
int rdCache_numLights;

void rdCache_FlushLights();
#endif

#ifdef SPHERE_AO
rdVector3 rdCache_aOccluderPositions[4096];
float rdCache_aOccluderRadii[4096];
int rdCache_numOccluders;

void rdCache_FlushOccluders();
#endif

int rdCache_Startup()
{
    return 1;
}

void rdCache_Shutdown()
{

}

void rdCache_AdvanceFrame()
{
#if !defined(TILE_SW_RASTER) && defined(SDL2_RENDER) || defined(TARGET_TWL)
    rdroid_curAcceleration = 1;
#endif

#if defined(TILE_SW_RASTER) || !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( rdroid_curAcceleration > 0 )
#endif
        std3D_StartScene();

#ifdef DECAL_RENDERING
	rdCache_numDecals = 0;
#endif
	rdCache_cacheFlushes = 0; // added
}

void rdCache_FinishFrame()
{
#if defined(TILE_SW_RASTER) || !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( rdroid_curAcceleration > 0 )
#endif
        std3D_EndScene();

	//printf("Flushes for frame %d frame: %d\n", rdroid_frameTrue, rdCache_cacheFlushes);
}

void rdCache_Reset()
{
    rdCache_numProcFaces = 0;
    rdCache_numUsedVertices = 0;
    rdCache_numUsedTexVertices = 0;
    rdCache_numUsedIntensities = 0;
    rdCache_ulcExtent.x = 0x7FFFFFFF;
    rdCache_ulcExtent.y = 0x7FFFFFFF;
    rdCache_lrcExtent.x = 0;
    rdCache_lrcExtent.y = 0;
#ifdef DECAL_RENDERING
	//rdCache_numDecals = 0;
#endif

#ifdef TILE_SW_RASTER
	if(rdroid_curAcceleration)
	{
		rdCache_paVertices = std3D_LockVertexStream(0);
		rdCache_paTexVertices = std3D_LockVertexStream(1);
		rdCache_paIntensities = std3D_LockVertexStream(2);
	}
	else
	{
		rdCache_paVertices = rdCache_aVertices;
		rdCache_paTexVertices = rdCache_aTexVertices;
		rdCache_paIntensities = rdCache_aIntensities;
	}
#endif
}

void rdCache_ClearFrameCounters()
{
    rdCache_drawnFaces = 0;
}

rdProcEntry *rdCache_GetProcEntry()
{
    size_t idx;
    rdProcEntry *out_procEntry;

    idx = rdCache_numProcFaces;
    if ( rdCache_numProcFaces >= RDCACHE_MAX_TRIS )
    {
        rdCache_Flush();
        idx = rdCache_numProcFaces;
    }

    if ( (unsigned int)(RDCACHE_MAX_VERTICES - rdCache_numUsedVertices) < 0x20 )
        return 0;

    if ( (unsigned int)(RDCACHE_MAX_VERTICES - rdCache_numUsedTexVertices) < 0x20 )
        return 0;

    if ( (unsigned int)(RDCACHE_MAX_VERTICES - rdCache_numUsedIntensities) < 0x20 )
        return 0;

    out_procEntry = &rdCache_aProcFaces[idx];
    out_procEntry->vertices = &rdCache_paVertices[rdCache_numUsedVertices];
    out_procEntry->vertexUVs = &rdCache_paTexVertices[rdCache_numUsedTexVertices];
#ifdef VIEW_SPACE_GBUFFER
	out_procEntry->vertexVS = &rdCache_aVerticesVS[rdCache_numUsedTexVertices];
#endif
    out_procEntry->vertexIntensities = &rdCache_paIntensities[rdCache_numUsedIntensities];
#ifdef JKM_LIGHTING
    out_procEntry->paRedIntensities = &rdCache_paRedIntensities[rdCache_numUsedIntensities];
    out_procEntry->paGreenIntensities = &rdCache_paGreenIntensities[rdCache_numUsedIntensities];
    out_procEntry->paBlueIntensities = &rdCache_paBlueIntensities[rdCache_numUsedIntensities];
#endif
#ifdef VERTEX_COLORS
	rdVector_Set3(&out_procEntry->color, 1.0f, 1.0f, 1.0f);
#endif
    return out_procEntry;
}
#include "Win95/stdDisplay.h"
void rdCache_Flush()
{
    size_t v0; // eax
    size_t v1; // edi
    size_t v3; // edi
    rdProcEntry *face; // esi

#ifdef RENDER_DROID2
	//std3D_FlushDrawCalls();
#endif

    if (!rdCache_numProcFaces)
        return;

    if ( rdroid_curSortingMethod == 2 )
    {
#ifndef TARGET_TWL
        _qsort(rdCache_aProcFaces, rdCache_numProcFaces, sizeof(rdProcEntry), (int (__cdecl *)(const void *, const void *))rdCache_ProcFaceCompareByDistance);
#endif
    }
#ifdef QOL_IMPROVEMENTS
    else if ( rdroid_curSortingMethod == 1 )
    {
        _qsort(rdCache_aProcFaces, rdCache_numProcFaces, sizeof(rdProcEntry), (int (__cdecl *)(const void *, const void *))rdCache_ProcFaceCompareByState);
    }
#endif
#ifdef TILE_SW_RASTER
	rdRaster_StartBinning();

	for (v3 = 0; v3 < rdCache_numProcFaces; v3++)
	{
		face = &rdCache_aProcFaces[v3];
		rdRaster_BinFaceCoarse(face);
	}
	rdRaster_BinFaces();

	rdRaster_EndBinning();

	if (!rdroid_curAcceleration)
	{
		rdRaster_FlushBins();
	}
	else
	{
		std3D_UnlockVertexStream(0);
		std3D_UnlockVertexStream(1);
		std3D_UnlockVertexStream(2);
	}
#else
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( rdroid_curAcceleration <= 0 )
    {
        if ( rdroid_curOcclusionMethod )
        {
            if ( rdroid_curOcclusionMethod == 1 )
            {
                rdActive_AdvanceFrame();
                rdActive_DrawScene();
            }
        }
        else if ( rdroid_curZBufferMethod )
        {

            if ( rdroid_curZBufferMethod == RD_ZBUFFER_READ_WRITE )
            {
                for (v1 = 0; v1 < rdCache_numProcFaces; v1++)
                {
                    face = &rdCache_aProcFaces[v1];
                    if ( (face->extraData & 1) != 0 )
                        rdCache_DrawFaceUser(face);
                    else
                        rdCache_DrawFaceZ(face);
                }
            }
        }
        else
        {
            for (v3 = 0; v3 < rdCache_numProcFaces; v3++)
            {
                face = &rdCache_aProcFaces[v3];
                if ( (face->extraData & 1) != 0 )
                    rdCache_DrawFaceUser(face);
                else
                    rdCache_DrawFaceN(face);
            }

        }
    }
    else
#endif
    {
        rdCache_SendFaceListToHardware();
	#ifdef DECAL_RENDERING
		//rdCache_FlushDecals();
	#endif
    }
#endif // TILE_SW_RASTER
    rdCache_drawnFaces += rdCache_numProcFaces;
	rdCache_cacheFlushes++; // added
	rdCache_Reset();
}

#ifdef TARGET_TWL
const static flex_t res_fix_x = (1.0/512.0);
const static flex_t res_fix_y = (1.0/384.0);
#endif

#ifndef TILE_SW_RASTER

int rdCache_SendFaceListToHardware()
{
    int v0; // ecx
    int v1; // edx
    flex_d_t v2; // st7
    flex_d_t v3; // st6
    flex_d_t v4; // st5
    rdClipFrustum *v7; // edx
    flex_d_t invZFar; // st7
    int mipmap_level; // edi
    rdProcEntry *active_6c; // esi
    v11_struct v11; // edx
    int expected_alpha; // ecx
    int v14; // eax
    rdTexinfo *v15; // eax
    rdTexture *sith_tex_sel; // esi
    rdDDrawSurface *tex2_arr_sel; // eax
    flex_t *vert_lights_iter; // ecx
    int vert_lights_iter_cnt; // edx
    flex_d_t v21; // st7
    flex_d_t v22; // st7
    flex_d_t v23; // st7
    flex_d_t v24; // st7
    flex_d_t v25; // st7
#ifdef RGB_AMBIENT
	rdVector3 v26;
#else
    flex_d_t v26; // st7
#endif
    flex_d_t v27; // st7
    rdVector3 *iterating_6c_vtxs_; // esi
    int v35; // ecx
    flex_d_t v36; // st7
    flex_d_t d3dvtx_zval; // st7
    flex_d_t v38; // st6
    flex_d_t light_level; // st7
    int vertex_g; // ebx
    int vertex_r; // edi
    rdColormap *v45; // eax
    flex_d_t v47; // st7
    __int64 v48; // rax
    flex_d_t v49; // st7
    int vertex_b; // cl
    rdProcEntry *v52; // esi
    int final_vertex_color; // eax
    rdVector2 *uvs_in_pixels; // eax
    flex_d_t tex_v; // st7
    int v61; // ecx
    int lighting_maybe_2; // edx
    unsigned int v63; // edi
    int tri; // eax
    int lighting_maybe; // ebx
    size_t tri_idx; // eax
    flex_t *v70; // ecx
    int v71; // edx
    flex_d_t v72; // st7
    flex_d_t v73; // st7
    flex_d_t v75; // st7
    flex_d_t v76; // st7
    flex_d_t v78; // st7
    rdDDrawSurface *v80; // edx
    flex_d_t v87; // st7
    flex_d_t v88; // st7
    flex_d_t v89; // st6
    rdColormap *v91; // esi
    flex_d_t v92; // st7
    int v93; // eax
    int v94; // ebx
    int v95; // edx
    int v96; // edi
    int v97; // eax
    int v98; // ecx
    __int64 v100; // rax
    flex_d_t v101; // st7
    uint8_t v103; // cl
    int v104; // eax
    unsigned int v108; // edi
    int v109; // ecx
    int v110; // edx
    unsigned int v111; // edi
    int v112; // esi
    rdTri *v114; // eax
    int v115; // ebx
    unsigned int v117; // eax
    flex_t actual_width; // [esp+1Ch] [ebp-84h]
    flex_t actual_height; // [esp+20h] [ebp-80h]
    flex_t v121; // [esp+24h] [ebp-7Ch]
    flex_t green_scalar; // [esp+34h] [ebp-6Ch]
    flex_t blue_scalar; // [esp+38h] [ebp-68h]
    int rend_6c_current_idx; // [esp+3Ch] [ebp-64h]
    flex_t red_scalar; // [esp+40h] [ebp-60h]
    int v129; // [esp+44h] [ebp-5Ch]
    int v130; // [esp+48h] [ebp-58h]
    int vertex_a; // [esp+4Ch] [ebp-54h]
    int alpha_upshifta; // [esp+4Ch] [ebp-54h]
    int alpha_is_opaque; // [esp+50h] [ebp-50h]
    int tri_vert_idx; // [esp+58h] [ebp-48h]
    int flags_idk; // [esp+60h] [ebp-40h]
    rdTexinfo *v137; // [esp+64h] [ebp-3Ch]
    int iterating_6c_vtx_idx; // [esp+64h] [ebp-3Ch]
    int mipmap_related; // [esp+68h] [ebp-38h]
    rdVector3 *iterating_6c_vtxs; // [esp+68h] [ebp-38h]
    unsigned int out_width; // [esp+6Ch] [ebp-34h] BYREF
    unsigned int out_height; // [esp+74h] [ebp-2Ch] BYREF
    int flags_idk_; // [esp+78h] [ebp-28h]
    int a3; // [esp+7Ch] [ebp-24h]
    int lighting_capability; // [esp+80h] [ebp-20h]
#ifdef RGB_AMBIENT
    rdVector3 v148; // [esp+84h] [ebp-1Ch]
#else
	flex_d_t v148; // [esp+84h] [ebp-1Ch]
#endif
    int blue; // [esp+8Ch] [ebp-14h]
    int red_and_alpha; // [esp+98h] [ebp-8h]
    int green; // [esp+9Ch] [ebp-4h]
    int tmpiter;

    //printf("b %f %f %f ", rdroid_curColorEffects.tint.x, rdroid_curColorEffects.tint.z, rdroid_curColorEffects.tint.z);
    //printf("%d %d %d\n", rdroid_curColorEffects.filter.x, rdroid_curColorEffects.filter.z, rdroid_curColorEffects.filter.z);


    a3 = 0; // added? aaaaaaa undefined
    v0 = 0;
    v1 = 0;
    v130 = 0;
    v129 = 0;
    alpha_is_opaque = 0;
    
    switch ( rdroid_curZBufferMethod )
    {
        case RD_ZBUFFER_NOREAD_NOWRITE:
        default:
            flags_idk = 0x33;
            break;
        case RD_ZBUFFER_NOREAD_WRITE:
            flags_idk = 0x1033;
            break;
        case RD_ZBUFFER_READ_WRITE:
            flags_idk = 0x1833;
            break;
        case RD_ZBUFFER_READ_NOWRITE:
            flags_idk = 0x833;
            break;
    }
    if ( rdroid_curColorEffects.tint.x > 0.0 || rdroid_curColorEffects.tint.y > 0.0 || rdroid_curColorEffects.tint.z > 0.0 )
    {
        v2 = rdroid_curColorEffects.tint.y * 0.5;
        v3 = rdroid_curColorEffects.tint.z * 0.5;
        v0 = 1;
        v130 = 1;
        red_scalar = rdroid_curColorEffects.tint.x - (v3 + v2);
        v4 = rdroid_curColorEffects.tint.x * 0.5;
        green_scalar = rdroid_curColorEffects.tint.y - (v4 + v3);
        blue_scalar = rdroid_curColorEffects.tint.z - (v4 + v2);
    }
    if ( rdroid_curColorEffects.filter.x || rdroid_curColorEffects.filter.y || rdroid_curColorEffects.filter.z )
    {
        v1 = 1;
        v129 = 1;
    }
#ifdef TARGET_TWL
    // TODO: this breaks transparent color-only surfaces, maybe just check tri flags?
    rdSetVertexColorMode(1);
#endif
    if ( v0 || v1 || (rdGetVertexColorMode() == 1)) // MOTS added
    {
        flags_idk |= 0x8000;
    }

    std3D_ResetRenderList();
    rdCache_ResetRenderList();
    v7 = rdCamera_pCurCamera->pClipFrustum;
    invZFar = 1.0 / v7->zFar;
    rend_6c_current_idx = 0;
    
    for (rend_6c_current_idx = 0; rend_6c_current_idx < rdCache_numProcFaces; rend_6c_current_idx++)
    {        
        active_6c = &rdCache_aProcFaces[rend_6c_current_idx];
        mipmap_level = a3;

        flags_idk_ = flags_idk;

#ifdef RGB_AMBIENT
		if ((rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT) != 0)
			rdVector_Copy3(&v148, &active_6c->ambientLight);
		else
			rdVector_Zero3(&v148);
#else
        if ( (rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT) != 0 )
            v148 = active_6c->ambientLight;
        else
            v148 = 0.0;
#endif

        // Added: We need to know if a face is flex_d_t-sided
        if (active_6c->type & RD_FF_DOUBLE_SIDED && active_6c->light_flags)
        {
            flags_idk_ |= 0x10000;
        }

#if defined(SDL2_RENDER) || defined(TARGET_TWL)
        d3d_maxVertices = STD3D_MAX_VERTICES;
#endif
        if ( active_6c->numVertices + rdCache_totalVerts >= d3d_maxVertices )
        {
            rdCache_DrawRenderList();
            rdCache_ResetRenderList();
        }

        v11.mipmap_related = rdroid_curGeometryMode;
        tri_vert_idx = rdCache_totalVerts;

        if ( active_6c->geometryMode < rdroid_curGeometryMode )
            v11.mipmap_related = active_6c->geometryMode;

        mipmap_related = v11.mipmap_related;
        lighting_capability = active_6c->lightingMode;

        if ( lighting_capability >= rdroid_curLightingMode )
            lighting_capability = rdroid_curLightingMode;

        if ( (active_6c->type & RD_FF_TEX_TRANSLUCENT) != 0 )
        {
            expected_alpha = 90;
            flags_idk_ |= 0x200;
            red_and_alpha = 90;
        }
        else
        {
            expected_alpha = 255;
            red_and_alpha = 255;
        }

        if ( expected_alpha != 255 && !std3D_HasModulateAlpha() && !std3D_HasAlphaFlatStippled() )
        {
            red_and_alpha = 255;
            alpha_is_opaque = 1;
        }

        tex2_arr_sel = 0;

        v11.material = active_6c->material;
        if (!v11.material)
        {
            continue;
        }

        // Added
        rdMaterial_EnsureData(v11.material);

        v14 = active_6c->wallCel;
        if ( v14 == -1 )
        {
            v14 = v11.material->celIdx;
            if ( v14 >= 0 )
            {
                if ( v14 > v11.material->num_texinfo - 1 )
                    v14 = v11.material->num_texinfo - 1;
            }
        }
        else if ( v14 < 0 )
        {
            v14 = 0;
        }
        else if ( v14 > v11.material->num_texinfo - 1 )
        {
            v14 = v11.material->num_texinfo - 1;
        }

        v15 = v11.material->texinfos[v14];
        v137 = v15;
        if ( v11.mipmap_related == 4 && (v15 && v15->header.texture_type & 8) == 0 ) // Added: v15 nullptr check
        {
            v11.mipmap_related = 3;
            mipmap_related = 3;
        }
        if ( !v15 || (v15->header.texture_type & 8) == 0 || !v15->texture_ptr ) // Added: !v15->texture_ptr
        {
            tex2_arr_sel = 0;
        }
        else
        {
            sith_tex_sel = v15->texture_ptr;

            flex_t z_min = active_6c->z_min * rdCamera_GetMipmapScalar(); // MOTS added
            
            mipmap_level = 1;
            if (sith_tex_sel->num_mipmaps == 2)
            {
                if ( z_min <= (flex_d_t)rdroid_aMipDistances.y )
                {
                    mipmap_level = 0;
                }
            }
            else if (sith_tex_sel->num_mipmaps == 3)
            {
                if ( z_min <= (flex_d_t)rdroid_aMipDistances.x )
                {
                    mipmap_level = 0;
                }
                else if ( z_min > (flex_d_t)rdroid_aMipDistances.y )
                {
                    mipmap_level = 2;
                }
            }
            else if (sith_tex_sel->num_mipmaps == 4)
            {
                if ( z_min <= (flex_d_t)rdroid_aMipDistances.x )
                {
                    mipmap_level = 0;
                }
                else if ( z_min > (flex_d_t)rdroid_aMipDistances.y )
                {
                    if ( z_min > (flex_d_t)rdroid_aMipDistances.z )
                        mipmap_level = 3;
                    else
                        mipmap_level = 2;
                }
            }
            else if (sith_tex_sel->num_mipmaps == 1)
            {
                mipmap_level = 0;
            }

            // Look for the closest mipmap that's been loaded
#ifdef TARGET_TWL
            int mipmap_level_orig = mipmap_level;
            stdVBuffer* mipmap = sith_tex_sel->texture_struct[mipmap_level];

            alpha_is_opaque = 1;
            while (!mipmap && mipmap_level < sith_tex_sel->num_mipmaps) {
                mipmap = sith_tex_sel->texture_struct[mipmap_level];
                if (mipmap) {
                    break;
                }

                mipmap_level += 1;
            }

            if (!mipmap) {
                mipmap_level = mipmap_level_orig;
                while (!mipmap && mipmap_level > 0) {
                    mipmap = sith_tex_sel->texture_struct[mipmap_level];
                    if (mipmap) {
                        break;
                    }

                    mipmap_level -= 1;
                }
            }

            if (!mipmap) {
                mipmap_level = mipmap_level_orig;
            }
#endif

            a3 = mipmap_level;

            if ( (sith_tex_sel->alpha_en & 1) != 0 && std3D_HasAlpha() )
            {
#ifdef SDL2_RENDER
                if (sith_tex_sel->has_jkgm_override)
                {
                    tex2_arr_sel = &sith_tex_sel->alphaMats[mipmap_level];
                    if (tex2_arr_sel->albedo_factor[0] != 0.0
                        || tex2_arr_sel->albedo_factor[1] != 0.0
                        || tex2_arr_sel->albedo_factor[2] != 0.0
                        || tex2_arr_sel->albedo_factor[3] != 0.0)
                    {
                        flags_idk_ |= 0x0;
                    }
                    else {
                        // If a texture is only emissive (blaster shots, etc)
                        // then we probably want to keep the existing blend modes
                        flags_idk_ |= 0x400;
                    }
                }
                else {
                    flags_idk_ |= 0x400;
                }
#else
                flags_idk_ |= 0x400;
#endif
            }

#ifdef ADDITIVE_BLEND
			if ((active_6c->type & (0x400|0x800)) != 0)
			{				
				flags_idk_ |= 0x80000; // additive
				if((active_6c->type & 0x800) != 0)
					flags_idk_ |= 0x100000;
			}
#endif

#ifdef STENCIL_BUFFER
			if(active_6c->extraData & 2)
				flags_idk_ |= 0x200000; // stencil bit
#endif

            if ( !rdMaterial_AddToTextureCache(v11.material, sith_tex_sel, mipmap_level, alpha_is_opaque, v14) )
            {
                rdCache_DrawRenderList();
                rdCache_ResetRenderList();
                if ( !rdMaterial_AddToTextureCache(v11.material, sith_tex_sel, mipmap_level, alpha_is_opaque, v14) )
                    return 0;
            }

            tex2_arr_sel = &sith_tex_sel->alphaMats[mipmap_level];

            if ( alpha_is_opaque )
                tex2_arr_sel = &sith_tex_sel->opaqueMats[mipmap_level];

            // Added: nullptr check and fallback
            if (sith_tex_sel && sith_tex_sel->texture_struct[mipmap_level]) {
                std3D_GetValidDimension(
                    sith_tex_sel->texture_struct[mipmap_level]->format.width,
                    sith_tex_sel->texture_struct[mipmap_level]->format.height,
                    &out_width,
                    &out_height);
            }
            else {
                out_width = 8;
                out_height = 8;
            }
            
            v11.mipmap_related = mipmap_related;
            actual_width = (flex_t)(out_width << mipmap_level); // FLEXTODO
            actual_height = (flex_t)(out_height << mipmap_level); // FLEXTODO
        }

#ifdef TARGET_TWL
        // We need to know if a triangle is sky texture (affine texture)
        if (active_6c->textureMode == RD_TEXTUREMODE_AFFINE) {
            flags_idk_ |= 0x20000;
        }
#endif

        if ( v11.mipmap_related != 3 )
        {
#ifdef RGB_AMBIENT
			rdVector_Copy3(&v26, &v148);
			if (v11.mipmap_related != 4)
				continue;
			if (lighting_capability == 1)
			{
				v27 = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);

				if (v27 > v148.x)
					v26.x = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);
				else
					v26.x = v148.x;

				if (v27 > v148.y)
					v26.y = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);
				else
					v26.y = v148.y;

				if (v27 > v148.z)
					v26.z = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);
				else
					v26.z = v148.z;

				active_6c->light_level_static = (v26.x + v26.y + v26.z) * 0.3333f * 255.0;
			}
			else if (lighting_capability == 2)
			{
				v24 = active_6c->extralight + active_6c->light_level_static;
				v25 = stdMath_Clamp(v24, 0.0, 1.0);
				v26.x = stdMath_Clamp(v25, v148.x, 1.0);
				v26.y = stdMath_Clamp(v25, v148.y, 1.0);
				v26.z = stdMath_Clamp(v25, v148.z, 1.0);

				active_6c->light_level_static = (v26.x + v26.y + v26.z) * 0.3333f * 255.0;
			}
			else if (USES_VERTEX_LIGHTING(lighting_capability) && active_6c->numVertices)
			{
				//if (rdGetVertexColorMode() == 1)
				{
					float* iterRedIntense = active_6c->paRedIntensities;
					float* iterGreenIntense = active_6c->paGreenIntensities;
					float* iterBlueIntense = active_6c->paBlueIntensities;

					vert_lights_iter_cnt = active_6c->numVertices;
					do
					{
						*iterRedIntense = stdMath_Clamp(stdMath_Clamp(*iterRedIntense + active_6c->extralight, 0.0, 1.0), v148.x, 1.0) * 255.0;
						*iterGreenIntense = stdMath_Clamp(stdMath_Clamp(*iterGreenIntense + active_6c->extralight, 0.0, 1.0), v148.y, 1.0) * 255.0;
						*iterBlueIntense = stdMath_Clamp(stdMath_Clamp(*iterBlueIntense + active_6c->extralight, 0.0, 1.0), v148.z, 1.0) * 255.0;

						++iterRedIntense;
						++iterGreenIntense;
						++iterBlueIntense;
						--vert_lights_iter_cnt;
					} while (vert_lights_iter_cnt);
				}
			}
#else

            v26 = v148;
            if ( v11.mipmap_related != 4 )
                continue;
            if ( lighting_capability == 1 )
            {
                v27 = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);

                if ( v27 > v148 )
                {
                    v26 = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);
                }
                else
                {
                    v26 = v148;
                }

                active_6c->light_level_static = v26 * 255.0;
            }
            else if ( lighting_capability == 2 )
            {
                v24 = active_6c->extralight + active_6c->light_level_static;
                v25 = stdMath_Clamp(v24, 0.0, 1.0);
                v26 = stdMath_Clamp(v25, v148, 1.0);

                active_6c->light_level_static = v26 * 255.0;
            }
            else if (USES_VERTEX_LIGHTING(lighting_capability) && active_6c->numVertices )
            {
                // MOTS added
#ifdef JKM_LIGHTING
                if (rdGetVertexColorMode() == 1) {
                    flex_t* iterRedIntense = active_6c->paRedIntensities;
                    flex_t* iterGreenIntense = active_6c->paGreenIntensities;
                    flex_t* iterBlueIntense = active_6c->paBlueIntensities;

                    vert_lights_iter_cnt = active_6c->numVertices;
                    do
                    {
                        *iterRedIntense = stdMath_Clamp(stdMath_Clamp(*iterRedIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0) * 255.0;
                        *iterGreenIntense = stdMath_Clamp(stdMath_Clamp(*iterGreenIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0) * 255.0;
                        *iterBlueIntense = stdMath_Clamp(stdMath_Clamp(*iterBlueIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0) * 255.0;

                        ++iterRedIntense;
                        ++iterGreenIntense;
                        ++iterBlueIntense;
                        --vert_lights_iter_cnt;
                    }
                    while ( vert_lights_iter_cnt );
                }
                else
#endif
                {
                    vert_lights_iter = active_6c->vertexIntensities;
                    vert_lights_iter_cnt = active_6c->numVertices;
                    do
                    {
                        v21 = *vert_lights_iter + active_6c->extralight;

                        v22 = stdMath_Clamp(v21, 0.0, 1.0);
                        v23 = stdMath_Clamp(v22, v148, 1.0);

                        *vert_lights_iter = v23 * 255.0;
                        ++vert_lights_iter;
                        --vert_lights_iter_cnt;
                    }
                    while ( vert_lights_iter_cnt );
                }

                //active_6c->light_level_static = v26 * 255.0;
			}
#endif

            iterating_6c_vtxs = active_6c->vertices;
#ifdef VIEW_SPACE_GBUFFER
			rdVector3* iter_vs = active_6c->vertexVS;
#endif
            vertex_a = red_and_alpha << 8;

            for (int vtx_idx = 0; vtx_idx < active_6c->numVertices; vtx_idx++)
            {
#ifndef TARGET_TWL
                rdCache_aHWVertices[rdCache_totalVerts].x = (iterating_6c_vtxs[vtx_idx].x); // Added: The original game rounded to ints here (with ceilf?)
                rdCache_aHWVertices[rdCache_totalVerts].y = (active_6c->vertices[vtx_idx].y); // Added: The original game rounded to ints here (with ceilf?)
#endif
                iterating_6c_vtxs_ = active_6c->vertices;

                // DSi prefers z vertices directly
#ifdef TARGET_TWL
                v36 = iterating_6c_vtxs_[vtx_idx].z;
                iterating_6c_vtxs = iterating_6c_vtxs_;
                d3dvtx_zval = iterating_6c_vtxs_[vtx_idx].z;
                v38 = (flex_t)1.0 - d3dvtx_zval;
#else
                v36 = iterating_6c_vtxs_[vtx_idx].z;
                iterating_6c_vtxs = iterating_6c_vtxs_;
                if ( v36 == 0.0 )
                    d3dvtx_zval = 0.0;
                else
                    d3dvtx_zval = 1.0 / iterating_6c_vtxs_[vtx_idx].z;
                v38 = d3dvtx_zval * invZFar;
#endif
                if ( rdCache_dword_865258 != 16 )
                    v38 = 1.0 - v38;
#ifdef VIEW_SPACE_GBUFFER
				rdCache_aHWVertices[rdCache_totalVerts].vx = iter_vs[vtx_idx].x;
				rdCache_aHWVertices[rdCache_totalVerts].vy = iter_vs[vtx_idx].y;
				rdCache_aHWVertices[rdCache_totalVerts].vz = iter_vs[vtx_idx].z;
#endif
#ifdef TARGET_TWL
                rdCache_aHWVertices[rdCache_totalVerts].x = (active_6c->vertices[vtx_idx].x * res_fix_x) * v38; // Added: The original game rounded to ints here (with ceilf?)
                rdCache_aHWVertices[rdCache_totalVerts].y = (active_6c->vertices[vtx_idx].y * res_fix_y) * v38; // Added: The original game rounded to ints here (with ceilf?)
#endif
                rdCache_aHWVertices[rdCache_totalVerts].z = v38;

                // Don't waste time with this on DSi
#ifdef TARGET_TWL
                rdCache_aHWVertices[rdCache_totalVerts].nx = 0.0;
#else
                rdCache_aHWVertices[rdCache_totalVerts].nx = d3dvtx_zval / 32.0;
#endif
                rdCache_aHWVertices[rdCache_totalVerts].nz = 0.0;
                if ( lighting_capability == 0 )
                {
                    vertex_b = 255;
                    vertex_g = 255;
                    blue = 255;
                    green = 255;
                    vertex_r = 255;
#ifdef SDL2_RENDER
                    rdCache_aHWVertices[rdCache_totalVerts].lightLevel = 1.0;
#endif

                    /*vertex_r = 0;
                    vertex_g = 0;
                    green = 0;
                    vertex_b = 255;
                    blue = 255;*/
                }
                else
                {
                    // MOTS added
                    if (rdGetVertexColorMode() != 1 || (!USES_VERTEX_LIGHTING(lighting_capability)))
                    {
                        if (USES_VERTEX_LIGHTING(lighting_capability))
                            light_level = active_6c->vertexIntensities[vtx_idx];
                        else
                            light_level = active_6c->light_level_static;
#ifdef SDL2_RENDER
#ifdef CLASSIC_EMISSIVE
						rdCache_aHWVertices[rdCache_totalVerts].lightLevel = 0.0f;
#else
                        rdCache_aHWVertices[rdCache_totalVerts].lightLevel = light_level / 255.0;
#endif
#endif
                        

                        vertex_b = (__int64)light_level;
                        vertex_g = vertex_b;
                        vertex_r = vertex_b;
                        blue = vertex_b;
                        green = vertex_b;

                        /*vertex_r = 255;
                    vertex_g = 0;
                    green = 0;
                    vertex_b = 255;
                    blue = 255;*/
                    }
#ifdef JKM_LIGHTING
                    else
                    {
                        //printf("%f\n", active_6c->paRedIntensities[vtx_idx]);
                        light_level = 1.0;
                        flex_d_t intRed = active_6c->paRedIntensities[vtx_idx];
                        flex_d_t intGreen = active_6c->paGreenIntensities[vtx_idx];
                        flex_d_t intBlue = active_6c->paBlueIntensities[vtx_idx];

                        // Added for SDL2
#ifdef SDL2_RENDER
#ifdef CLASSIC_EMISSIVE
						rdCache_aHWVertices[rdCache_totalVerts].lightLevel = 0.0f;
#else
                        flex_d_t luma = (0.2126 * intRed) + (0.7152 * intGreen) + (0.0722 * intBlue);
                        light_level = luma;

                        rdCache_aHWVertices[rdCache_totalVerts].lightLevel = luma / 255.0;
#endif
#endif

                        vertex_b = (int)intBlue;
                        vertex_g = (int)intGreen;
                        vertex_r = (int)intRed;
                        blue = vertex_b;
                        green = vertex_g;

                        /*vertex_r = 0;
                    vertex_g = 255;
                    green = 255;
                    vertex_b = 0;
                    blue = 0;*/

                    }
#endif
                }

                red_and_alpha = vertex_r;
                v45 = active_6c->colormap;
                if ( v45 != rdColormap_pIdentityMap )
                {
                    v47 = v45->tint.y * (flex_d_t)green;
                    vertex_r = (uint8_t)(__int64)(v45->tint.x * (flex_d_t)red_and_alpha);
                    red_and_alpha = vertex_r;
                    v48 = (__int64)v47;
                    v49 = v45->tint.z * (flex_d_t)blue;
                    vertex_g = (uint8_t)v48;
                    green = (uint8_t)v48;
                    vertex_b = (uint8_t)(__int64)v49;
                    flags_idk_ |= 0x8000;
                    blue = vertex_b;
                }
                if ( v129 )
                {
                    if ( !rdroid_curColorEffects.filter.x )
                    {
                        vertex_r = 0;
                        red_and_alpha = 0;
                    }
                    if ( !rdroid_curColorEffects.filter.y )
                    {
                        vertex_g = 0;
                        green = 0;
                    }
                    if ( !rdroid_curColorEffects.filter.z )
                    {
                        vertex_b = 0;
                        blue = 0;
                    }
                }
                if ( v130 )
                {
                    vertex_r += (__int64)((flex_d_t)red_and_alpha * red_scalar);
                    red_and_alpha = vertex_r;
                    vertex_g += (__int64)((flex_d_t)green * green_scalar);
                    green = vertex_g;
                    vertex_b += (__int64)((flex_d_t)blue * blue_scalar);
                    blue = vertex_b;
                }
                if ( rdroid_curColorEffects.fade < 1.0 )
                {
                    vertex_r = (__int64)((flex_d_t)red_and_alpha * rdroid_curColorEffects.fade);
                    vertex_g = (__int64)((flex_d_t)green * rdroid_curColorEffects.fade);
                    vertex_b = (__int64)((flex_d_t)blue * rdroid_curColorEffects.fade);
                }
                
#ifdef VERTEX_COLORS
				if (active_6c->type & RD_FF_VERTEX_COLORS)
				{
					vertex_r = (__int64)((double)vertex_r * active_6c->color.x);
					red_and_alpha = vertex_r;
					vertex_g = (__int64)((double)vertex_g * active_6c->color.y);
					green = vertex_g;
					vertex_b = (__int64)((double)vertex_b * active_6c->color.z);
					blue = vertex_b;
				}
#endif

                vertex_r = stdMath_ClampInt(vertex_r, 0, 255);
                vertex_g = stdMath_ClampInt(vertex_g, 0, 255);
                vertex_b = stdMath_ClampInt(vertex_b, 0, 255);

                v52 = active_6c;
                final_vertex_color = vertex_b | (((uint8_t)vertex_g | ((vertex_a | (uint8_t)vertex_r) << 8)) << 8);
                
                // For some reason, ny holds the vertex color.
                rdCache_aHWVertices[rdCache_totalVerts].color = final_vertex_color;
                uvs_in_pixels = v52->vertexUVs;

                // DSi wants UVs in pixels
#ifdef TARGET_TWL
                rdCache_aHWVertices[rdCache_totalVerts].tu = uvs_in_pixels[vtx_idx].x >> mipmap_level;
                rdCache_aHWVertices[rdCache_totalVerts].tv = uvs_in_pixels[vtx_idx].y >> mipmap_level;
#else
                rdCache_aHWVertices[rdCache_totalVerts].tu = uvs_in_pixels[vtx_idx].x / actual_width;
                rdCache_aHWVertices[rdCache_totalVerts].tv = uvs_in_pixels[vtx_idx].y / actual_height;
#endif
                
                ++rdCache_totalVerts;
            }
#ifdef QOL_IMPROVEMENTS
            if ( active_6c->numVertices <= 2 )
            {
                tri_idx = rdCache_totalLines;
                rdCache_aHWLines[tri_idx].v2 = tri_vert_idx;
                rdCache_aHWLines[tri_idx].v1 = tri_vert_idx + 1;
                rdCache_aHWLines[tri_idx].flags = flags_idk_;
                
                rdCache_aHWVertices[rdCache_aHWLines[tri_idx].v1].color = active_6c->extraData;
                rdCache_aHWVertices[rdCache_aHWLines[tri_idx].v2].color = active_6c->extraData;

                rdCache_aHWVertices[rdCache_aHWLines[tri_idx].v1].nz = 0.0;
                rdCache_aHWVertices[rdCache_aHWLines[tri_idx].v2].nz = 0.0;
                
                rdCache_totalLines++;
            }
            else 
#endif
            if ( active_6c->numVertices <= 3 )
            {
                tri_idx = rdCache_totalNormalTris;
                rdCache_aHWNormalTris[tri_idx].v3 = tri_vert_idx;
                rdCache_aHWNormalTris[tri_idx].v2 = tri_vert_idx + 1;
                rdCache_aHWNormalTris[tri_idx].v1 = tri_vert_idx + 2;
                rdCache_aHWNormalTris[tri_idx].flags = flags_idk_;
                rdCache_aHWNormalTris[tri_idx].texture = tex2_arr_sel;
                rdCache_totalNormalTris++;
            }
            else
            {
                v61 = active_6c->numVertices - 2;
                v63 = active_6c->numVertices - 1;
                lighting_maybe_2 = 0;
                lighting_capability = 1;
                for (int pushed_tris = 0; pushed_tris <= v61; pushed_tris++)
                {
                    lighting_maybe = lighting_capability;
                    rdCache_aHWNormalTris[rdCache_totalNormalTris+pushed_tris].v3 = tri_vert_idx + lighting_maybe_2;
                    rdCache_aHWNormalTris[rdCache_totalNormalTris+pushed_tris].v2 = tri_vert_idx + lighting_maybe;
                    rdCache_aHWNormalTris[rdCache_totalNormalTris+pushed_tris].v1 = tri_vert_idx + v63;
                    rdCache_aHWNormalTris[rdCache_totalNormalTris+pushed_tris].flags = flags_idk_;
                    rdCache_aHWNormalTris[rdCache_totalNormalTris+pushed_tris].texture = tex2_arr_sel;
                    if ( (pushed_tris & 1) != 0 )
                    {
                        lighting_maybe_2 = v63--;
                    }
                    else
                    {
                        lighting_maybe_2 = lighting_maybe;
                        lighting_capability = lighting_maybe + 1;
                    }
                }
                rdCache_totalNormalTris += v61;
            }
            continue;
        }

solid_tri:
        if ( lighting_capability == 1 )
        {
	#ifdef RGB_AMBIENT
			v78 = stdMath_Clamp(active_6c->extralight, 0.0, 1.0);
			v26.x = stdMath_Clamp(v78, v148.x, 1.0);
			v26.y = stdMath_Clamp(v78, v148.y, 1.0);
			v26.z = stdMath_Clamp(v78, v148.z, 1.0);
			active_6c->light_level_static = (v26.x + v26.y + v26.z) * 0.3333f * 63.0;
	#else
            v78 = stdMath_Clamp(stdMath_Clamp(active_6c->extralight, 0.0, 1.0), v148, 1.0);
            active_6c->light_level_static = v78 * 63.0;
	#endif
        }
        else if ( lighting_capability == 2 )
        {
            v75 = active_6c->extralight + active_6c->light_level_static;

#ifdef RGB_AMBIENT
			v76 = stdMath_Clamp(v75, 0.0, 1.0);
			v26.x = stdMath_Clamp(v76, v148.x, 1.0);
			v26.y = stdMath_Clamp(v76, v148.y, 1.0);
			v26.z = stdMath_Clamp(v76, v148.z, 1.0);

			active_6c->light_level_static = (v26.x + v26.y + v26.z) * 0.3333f * 63.0;
#else
			v76 = stdMath_Clamp(stdMath_Clamp(v75, 0.0, 1.0), v148, 1.0);
            active_6c->light_level_static = v76 * 63.0;
#endif
        }
        else if (USES_VERTEX_LIGHTING(lighting_capability) && active_6c->numVertices )
        {
#ifdef RGB_AMBIENT
			//if (rdGetVertexColorMode() == 1)
			{
				float* iterRedIntense = active_6c->paRedIntensities;
				float* iterGreenIntense = active_6c->paGreenIntensities;
				float* iterBlueIntense = active_6c->paBlueIntensities;

				v71 = active_6c->numVertices;
				do
				{
					v73 = stdMath_Clamp(stdMath_Clamp(*iterRedIntense + active_6c->extralight, 0.0, 1.0), v148.x, 1.0);
					*iterRedIntense = v73 * 63.0;

					v73 = stdMath_Clamp(stdMath_Clamp(*iterGreenIntense + active_6c->extralight, 0.0, 1.0), v148.y, 1.0);
					*iterGreenIntense = v73 * 63.0;

					v73 = stdMath_Clamp(stdMath_Clamp(*iterBlueIntense + active_6c->extralight, 0.0, 1.0), v148.z, 1.0);
					*iterBlueIntense = v73 * 63.0;

					++iterRedIntense;
					++iterGreenIntense;
					++iterBlueIntense;
					--v71;
				} while (v71);
			}
#else
			// MOTS added
#ifdef JKM_LIGHTING
            if (rdGetVertexColorMode() == 1) {
                flex_t* iterRedIntense = active_6c->paRedIntensities;
                flex_t* iterGreenIntense = active_6c->paGreenIntensities;
                flex_t* iterBlueIntense = active_6c->paBlueIntensities;

                v71 = active_6c->numVertices;
                do
                {
                    v73 = stdMath_Clamp(stdMath_Clamp(*iterRedIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0);
                    *iterRedIntense = v73 * 63.0;

                    v73 = stdMath_Clamp(stdMath_Clamp(*iterGreenIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0);
                    *iterGreenIntense = v73 * 63.0;

                    v73 = stdMath_Clamp(stdMath_Clamp(*iterBlueIntense + active_6c->extralight, 0.0, 1.0), v148, 1.0);
                    *iterBlueIntense = v73 * 63.0;

                    ++iterRedIntense;
                    ++iterGreenIntense;
                    ++iterBlueIntense;
                    --v71;
                }
                while ( v71 );
            }
            else
#endif
            {
                v70 = active_6c->vertexIntensities;
                v71 = active_6c->numVertices;
                do
                {
                    v72 = *v70 + active_6c->extralight;

                    v73 = stdMath_Clamp(stdMath_Clamp(v72, 0.0, 1.0), v148, 1.0);

                    *v70 = v73 * 63.0;
                    ++v70;
                    --v71;
                }
                while ( v71 );
            }
#endif
            goto LABEL_232;
        }
        else
        {
            goto LABEL_232;
        }

LABEL_232:
        tmpiter = 0;
        alpha_upshifta = red_and_alpha << 8;
        for (int vtx_idx = 0; vtx_idx < active_6c->numVertices; vtx_idx++)
        {
#ifndef TARGET_TWL
            rdCache_aHWVertices[rdCache_totalVerts].x = (active_6c->vertices[tmpiter].x);  // Added: The original game rounded to ints here (with ceilf?)
            rdCache_aHWVertices[rdCache_totalVerts].y = (active_6c->vertices[tmpiter].y);  // Added: The original game rounded to ints here (with ceilf?)
#endif
            v87 = active_6c->vertices[tmpiter].z;

            // DSi prefers z vertices directly
#ifdef TARGET_TWL
            v88 = 1.0 - active_6c->vertices[tmpiter].z;
            v89 = v88;
#else
            if ( v87 == 0.0 )
                v88 = 0.0;
            else
                v88 = 1.0 / active_6c->vertices[tmpiter].z;
            v89 = v88 * invZFar;
#endif
            if ( rdCache_dword_865258 != 16 )
                v89 = 1.0 - v89;
#ifdef TARGET_TWL
            rdCache_aHWVertices[rdCache_totalVerts].x = ((active_6c->vertices[tmpiter].x * res_fix_x) * v89);  // Added: The original game rounded to ints here (with ceilf?)
            rdCache_aHWVertices[rdCache_totalVerts].y = ((active_6c->vertices[tmpiter].y * res_fix_y) * v89);  // Added: The original game rounded to ints here (with ceilf?)
#endif
            rdCache_aHWVertices[rdCache_totalVerts].z = v89;

            // Don't waste time with this on DSi
#ifdef TARGET_TWL
            rdCache_aHWVertices[rdCache_totalVerts].nx = 0.0;
#else
            rdCache_aHWVertices[rdCache_totalVerts].nx = v88 / 32.0;
#endif
            rdCache_aHWVertices[rdCache_totalVerts].nz = 0.0;

            // Added: nullptr check and fallback
            if (!(rdColormap *)active_6c->colormap || !v137) {
                v93 = 0xFF;
                v94 = 0xFF;
                v95 = 0xFF;
                v96 = 0xFF;
                red_and_alpha = v96;
                green = v94;
                blue = v95;
                v91 = (rdColormap *)active_6c->colormap;
                goto skip_colormap_deref;
            }

            if ( lighting_capability == 0) // Added: v137 nullptr check
            {
                v91 = (rdColormap *)active_6c->colormap;
                v97 = v137->header.field_4;
                v94 = (uint8_t)v91->colors[v97].g;
                v98 = (uint8_t)v91->colors[v97].b;
                v96 = (uint8_t)v91->colors[v97].r;
                red_and_alpha = v96;
                green = v94;
                blue = v98;
#ifdef SDL2_RENDER
                rdCache_aHWVertices[rdCache_totalVerts].lightLevel = 1.0;
#endif
            }
            else
            {
                // MOTS added
                if (rdGetVertexColorMode() != 1 || lighting_capability != 3)
                {
                    v91 = (rdColormap *)active_6c->colormap;
                    if (USES_VERTEX_LIGHTING(lighting_capability))
                        v92 = active_6c->vertexIntensities[vtx_idx];
                    else
                        v92 = active_6c->light_level_static;
#ifdef SDL2_RENDER
#ifdef CLASSIC_EMISSIVE
					rdCache_aHWVertices[rdCache_totalVerts].lightLevel = 0.0f;
#else
                    rdCache_aHWVertices[rdCache_totalVerts].lightLevel = v92 / 255.0;
#endif
#endif
                    v93 = *((uint8_t *)v91->lightlevel + 256 * ((__int64)v92 & 0x3F) + v137->header.field_4);
                    v94 = (uint8_t)v91->colors[v93].g;
                    v95 = (uint8_t)v91->colors[v93].b;
                    v96 = (uint8_t)v91->colors[v93].r;
                    red_and_alpha = v96;
                    green = v94;
                    blue = v95;
                }
#ifdef JKM_LIGHTING
                else
                {
                    v91 = (rdColormap *)active_6c->colormap;
                    uint8_t baseLight = *((uint8_t *)v91->lightlevel + v137->header.field_4);
                    flex_d_t intRed = active_6c->paRedIntensities[vtx_idx] * 255.0 - 0.5;
                    flex_d_t intGreen = active_6c->paGreenIntensities[vtx_idx] * 255.0 - 0.5;
                    flex_d_t intBlue = active_6c->paBlueIntensities[vtx_idx] * 255.0 - 0.5;


                    intRed += (flex_t)v91->colors[baseLight].r; // FLEXTODO
                    intGreen += (flex_t)v91->colors[baseLight].r; // FLEXTODO
                    intBlue += (flex_t)v91->colors[baseLight].r; // FLEXTODO

                    v94 = (uint8_t)stdMath_Clamp(intGreen, 0.0, 255.0);
                    v95 = (uint8_t)stdMath_Clamp(intBlue, 0.0, 255.0);
                    v96 = (uint8_t)stdMath_Clamp(intRed, 0.0, 255.0);
                    red_and_alpha = v96;
                    green = v94;
                    blue = v95;
                }
#endif
            }
skip_colormap_deref:
            if ( v91 && v91 != rdColormap_pIdentityMap ) // Added: nullptr check
            {
                v96 = (uint8_t)(__int64)(v91->tint.x * (flex_d_t)red_and_alpha);
                red_and_alpha = v96;
                v100 = (__int64)(v91->tint.y * (flex_d_t)green);
                v101 = v91->tint.z * (flex_d_t)blue;
                v94 = (uint8_t)v100;
                green = (uint8_t)v100;
                blue = (uint8_t)(__int64)v101;
            }
            flags_idk_ |= 0x8000;
            if ( v129 )
            {
                if ( !(rdroid_curColorEffects.filter.x) )
                {
                    v96 = 0;
                    red_and_alpha = 0;
                }
                if ( !(rdroid_curColorEffects.filter.y) )
                {
                    v94 = 0;
                    green = 0;
                }
                if ( !(rdroid_curColorEffects.filter.z) )
                    blue = 0;
            }
            if ( v130 )
            {
                v96 += (__int64)((flex_d_t)red_and_alpha * red_scalar);
                red_and_alpha = v96;
                v94 += (__int64)((flex_d_t)green * green_scalar);
                green = v94;
                blue += (__int64)((flex_d_t)blue * blue_scalar);
            }
            if ( rdroid_curColorEffects.fade < 1.0 )
            {
                v96 = (__int64)((flex_d_t)red_and_alpha * rdroid_curColorEffects.fade);
                v94 = (__int64)((flex_d_t)green * rdroid_curColorEffects.fade);
                blue = (__int64)((flex_d_t)blue * rdroid_curColorEffects.fade);
            }
            if ( v96 < 0 )
            {
                v96 = (v96 & ~0xFF) | 0;
            }
            else if ( v96 > 255 )
            {
                v96 = (v96 & ~0xFF) | 0xff;
            }
            if ( v94 < 0 )
            {
                v94 = (v94 & ~0xFF) | 0;
            }
            else if ( v94 > 255 )
            {
                v94 = (v94 & ~0xFF) | 0xff;
            }
            v103 = blue;
            if ( blue < 0 )
            {
                v103 = 0;
            }
            else if ( blue > 255 )
            {
                v103 = -1;
            }
            v104 = v103 | (((uint8_t)v94 | ((alpha_upshifta | (uint8_t)v96) << 8)) << 8);
            rdCache_aHWVertices[rdCache_totalVerts].color = v104;
            rdCache_aHWVertices[rdCache_totalVerts].tu = 0.0;
            rdCache_aHWVertices[rdCache_totalVerts].tv = 0.0;
            rdCache_totalVerts++;
            tmpiter++;
        }
        v108 = active_6c->numVertices;
#ifdef QOL_IMPROVEMENTS
        if ( v108 <= 2 )
        {
            v117 = rdCache_totalLines;
            rdCache_aHWLines[v117].v2 = tri_vert_idx;
            rdCache_aHWLines[v117].v1 = tri_vert_idx + 1;
            rdCache_aHWLines[v117].flags = flags_idk_;
            
            rdCache_aHWVertices[rdCache_aHWLines[v117].v1].color = active_6c->extraData;
            rdCache_aHWVertices[rdCache_aHWLines[v117].v2].color = active_6c->extraData;
            
            rdCache_totalLines++;
        }
        else 
#endif
        if ( v108 <= 3 )
        {
            v117 = rdCache_totalSolidTris;
            rdCache_aHWSolidTris[v117].v3 = tri_vert_idx;
            rdCache_aHWSolidTris[v117].v2 = tri_vert_idx + 1;
            rdCache_aHWSolidTris[v117].v1 = tri_vert_idx + 2;
            rdCache_aHWSolidTris[v117].flags = flags_idk_;
            rdCache_aHWSolidTris[v117].texture = 0;
            rdCache_totalSolidTris++;
        }
        else
        {
            v109 = v108 - 2;
            v110 = 0;
            v111 = v108 - 1;
            lighting_capability = 1;
            for (v112 = 0; v112 < v109; v112++)
            {
                v115 = lighting_capability;
                rdCache_aHWSolidTris[rdCache_totalSolidTris+v112].v3 = tri_vert_idx + v110;
                rdCache_aHWSolidTris[rdCache_totalSolidTris+v112].v2 = tri_vert_idx + v115;
                rdCache_aHWSolidTris[rdCache_totalSolidTris+v112].v1 = tri_vert_idx + v111;
                rdCache_aHWSolidTris[rdCache_totalSolidTris+v112].flags = flags_idk_;
                rdCache_aHWSolidTris[rdCache_totalSolidTris+v112].texture = 0;
                if ( (v112 & 1) != 0 )
                {
                    v110 = v111--;
                }
                else
                {
                    v110 = v115;
                    lighting_capability = v115 + 1;
                }
            }
            rdCache_totalSolidTris += v109;
        }
    }

    rdCache_DrawRenderList();
    //rdCache_ResetRenderList(); // Added
    return 1;
}


#endif

#ifndef TILE_SW_RASTER
void rdCache_ResetRenderList()
{
    std3D_ResetRenderList();
    rdCache_totalNormalTris = 0;
    rdCache_totalSolidTris = 0;
    rdCache_totalVerts = 0;
#ifdef QOL_IMPROVEMENTS
    rdCache_totalLines = 0;
#endif
}

void rdCache_DrawRenderList()
{
    if ( rdCache_totalVerts )
    {
        if ( !std3D_AddRenderListVertices(rdCache_aHWVertices, rdCache_totalVerts) )
        {
            std3D_DrawRenderList();
            std3D_AddRenderListVertices(rdCache_aHWVertices, rdCache_totalVerts);
        }
        std3D_RenderListVerticesFinish();
#ifndef TARGET_TWL
        if ( rdroid_curZBufferMethod == RD_ZBUFFER_READ_WRITE )
            _qsort(rdCache_aHWNormalTris, rdCache_totalNormalTris, sizeof(rdTri), rdCache_TriCompare);
#endif
        if ( rdCache_totalSolidTris )
            std3D_AddRenderListTris(rdCache_aHWSolidTris, rdCache_totalSolidTris);
        if ( rdCache_totalNormalTris )
            std3D_AddRenderListTris(rdCache_aHWNormalTris, rdCache_totalNormalTris);
#ifdef QOL_IMPROVEMENTS
#ifdef SDL2_RENDER
        if ( rdCache_totalLines )
            std3D_AddRenderListLines(rdCache_aHWLines, rdCache_totalLines);
#endif
#endif

        std3D_DrawRenderList();
    }

#ifdef RENDER_DROID2
	//std3D_FlushDrawCalls();
#endif
}
#endif

int rdCache_TriCompare(const void* a_, const void* b_)
{
    const rdTri* a = (const rdTri*)a_;
    const rdTri* b = (const rdTri*)b_;

    rdDDrawSurface *tex_b;
    rdDDrawSurface *tex_a;

    tex_b = b->texture;
    tex_a = a->texture;

    if ( tex_a->is_16bit == tex_b->is_16bit )
        return tex_a - tex_b;
    else
        return tex_a->is_16bit != 0 ? 1 : -1;
}

int rdCache_ProcFaceCompareByDistance(rdProcEntry *a, rdProcEntry *b)
{
#ifdef QOL_IMPROVEMENTS
	// Added: sort priority
	if(a->sortId < b->sortId)
		return -1;
	if (a->sortId > b->sortId)
		return 1;
#endif

    if ( a->z_min == b->z_min )
        return 0;

    if ( a->z_min >= b->z_min )
        return -1;

    return 1;
}

#ifdef QOL_IMPROVEMENTS
// Added: state based sorting, via sort method 0
int rdCache_GetSortHash(rdProcEntry* proc) // todo: precompute?
{
	int hash = 0;
	hash |= ((proc->type & RD_FF_TEX_TRANSLUCENT) == RD_FF_TEX_TRANSLUCENT) << 31;
	hash |= ((proc->type & RD_FF_DOUBLE_SIDED) == RD_FF_DOUBLE_SIDED) << 30;
	//RD_FF_TEX_CLAMP_X
	//RD_FF_TEX_CLAMP_Y
	//RD_FF_TEX_FILTER_NEAREST
	//RD_FF_ZWRITE_DISABLED
#ifdef ADDITIVE_BLEND
	hash |= ((proc->type & RD_FF_ADDITIVE) == RD_FF_ADDITIVE) << 30;
	hash |= ((proc->type & RD_FF_SCREEN) == RD_FF_SCREEN) << 28;
#endif
	hash |= (proc->material ? proc->material->id : 0);
	return hash;
}

int rdCache_ProcFaceCompareByState(rdProcEntry* a, rdProcEntry* b)
{
	//int aSortId = ((a->type) << 16) | (a->material ? a->material->id : 0);
	//int bSortId = ((b->type) << 16) | (b->material ? b->material->id : 0);
	int aSortId = rdCache_GetSortHash(a);
	int bSortId = rdCache_GetSortHash(b);

	if (aSortId == bSortId)
		return 0;

	if (aSortId >= bSortId)
		return -1;

	return 1;
}
#endif

// MOTS altered
int rdCache_AddProcFace(int a1, unsigned int num_vertices, char flags)
{
    int v6; // edx
    size_t current_rend_6c_idx; // eax
    rdProcEntry *procFace; // esi
    int v9; // ecx
    rdVector3 *v10; // edx
    flex_d_t y_min_related; // ebx
    flex_d_t v12; // st7
    flex_d_t y_max_related; // [esp+Ch] [ebp-18h]
    flex_t v27; // [esp+10h] [ebp-14h]
    flex_t z_max; // [esp+14h] [ebp-10h]
    flex_t z_min; // [esp+18h] [ebp-Ch]
    flex_t y_max; // [esp+1Ch] [ebp-8h]
    flex_t y_min; // [esp+20h] [ebp-4h]
    flex_t x_min; // [esp+2Ch] [ebp+8h]
    flex_t extdataa; // [esp+2Ch] [ebp+8h]
    flex_t extdatab; // [esp+2Ch] [ebp+8h]
    flex_t extdatac; // [esp+2Ch] [ebp+8h]
    flex_t x_max; // [esp+30h] [ebp+Ch]

    if ( rdCache_numProcFaces >= RDCACHE_MAX_TRIS )
        return 0;
    v6 = rdroid_curProcFaceUserData;
    current_rend_6c_idx = rdCache_numProcFaces;
    x_min = 3.4e38;
    x_max = -3.4e38;
    y_min = 3.4e38;
    procFace = &rdCache_aProcFaces[rdCache_numProcFaces];
    y_max = -3.4e38;
    z_min = 3.4e38;
    z_max = -3.4e38;
    procFace->extraData = a1;
    v9 = 0;
    procFace->numVertices = num_vertices;
    procFace->vertexColorMode = v6;
    
    // Added (MOTS also added? lol)
    y_min_related = 3.4e38;
    y_max_related = 3.4e38;
    
    if ( num_vertices )
    {
        v10 = rdCache_aProcFaces[current_rend_6c_idx].vertices;

        do
        {
            v12 = v10->x;
            if ( v12 < x_min )
                x_min = v10->x;
            if ( v12 > x_max )
                x_max = v10->x;

            if ( v10->y < y_min )
            {
                y_min = v10->y;
                y_min_related = (flex_t)v9; // FLEXTODO
            }
            if ( v10->y > y_max )
            {
                y_max = v10->y;
                y_max_related = (flex_t)v9; // FLEXTODO
            }
            if ( v10->z < z_min )
                z_min = v10->z;
            if ( v10->z > z_max )
                z_max = v10->z;
            v9++; // There used to be some weird undefined behavior here with y_min_related
            ++v10;
        }
        while ( v9 < num_vertices );
    }

    procFace->x_min = (int32_t)stdMath_Ceil(x_min);
    procFace->x_max = (int32_t)stdMath_Ceil(x_max);
    procFace->y_min = (int32_t)stdMath_Ceil(y_min);
    procFace->y_max = (int32_t)stdMath_Ceil(y_max);
    procFace->z_min = z_min;
    procFace->z_max = z_max;
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( procFace->x_min >= (unsigned int)procFace->x_max )
        return 0;
    if ( procFace->y_min >= (unsigned int)procFace->y_max )
        return 0;
#endif
    procFace->y_min_related = (int)y_min_related;
    procFace->y_max_related = (int)y_max_related;
    if ( (flags & 1) != 0 )
        rdCache_numUsedVertices += num_vertices;
    if ( (flags & 2) != 0 )
        rdCache_numUsedTexVertices += num_vertices;
    if ( (flags & 4) != 0 )
        rdCache_numUsedIntensities += num_vertices;
    procFace->colormap = rdColormap_pCurMap;
    ++rdCache_numProcFaces;
    if ( procFace->x_min < (unsigned int)rdCache_ulcExtent.x )
        rdCache_ulcExtent.x = procFace->x_min;
    if ( procFace->x_max > (unsigned int)rdCache_lrcExtent.x )
        rdCache_lrcExtent.x = procFace->x_max;
    if ( procFace->y_min < (unsigned int)rdCache_ulcExtent.y )
        rdCache_ulcExtent.y = procFace->y_min;
    if ( procFace->y_max > (unsigned int)rdCache_lrcExtent.y )
        rdCache_lrcExtent.y = procFace->y_max;
    return 1;
}

#ifdef DECAL_RENDERING
extern int jkPlayer_enableDecals;

void rdCache_DrawDecal(rdDecal* decal, rdMatrix34* matrix, rdVector3* color, rdVector3* scale, float angleFade)
{
	if (!jkPlayer_enableDecals || rdCache_numDecals >= 256)
	{
		return; // todo: revive me
		//rdCache_FlushDecals();
		//rdCache_numDecals = 0;
	}

	if (!rdMaterial_AddToTextureCache(decal->material, &decal->material->textures[0], 0, 0, 0))
	{
		//rdCache_FlushDecals();
		//if (!rdMaterial_AddToTextureCache(decal->material, &decal->material->textures[0], 0, 0, 0))
			return;
	}

	rdCache_aDecals[rdCache_numDecals] = decal;
	rdMatrix_Copy34(&rdCache_aDecalMatrices[rdCache_numDecals], matrix);
	rdVector_Copy3(&rdCache_aDecalColors[rdCache_numDecals], color);
	rdVector_Copy3(&rdCache_aDecalScales[rdCache_numDecals], scale);
	rdCache_aDecalAngleFades[rdCache_numDecals] = angleFade;
	++rdCache_numDecals;
}

void rdCache_FlushDecals()
{
	if (!jkPlayer_enableDecals)
	{
		rdCache_numDecals = 0;
		return;
	}

	for (int i = 0; i < rdCache_numDecals; ++i)
	{
		rdDecal* decal = rdCache_aDecals[i];

		if(!decal->material)
			continue;

		rdDDrawSurface* tex2_arr_sel = &decal->material->textures[0].alphaMats[0];
		if(!tex2_arr_sel)
			continue;

		// copy the matrix for modification, need to do this because it needs to be reused
		rdMatrix34 decalMatrix;
		rdMatrix_Copy34(&decalMatrix, &rdCache_aDecalMatrices[i]);

		// apply the size
		rdVector3 size;
		size.x = decal->size.x * rdCache_aDecalScales[i].x;
		size.y = decal->size.y * rdCache_aDecalScales[i].y;
		size.z = decal->size.z * rdCache_aDecalScales[i].z;
		rdMatrix_PreScale34(&decalMatrix, &size);

		// transform it to view space
		rdMatrix34 decalViewMat;
		rdMatrix_Multiply34(&decalViewMat, &rdCamera_pCurCamera->view_matrix, &decalMatrix);

		// project the decal box
		rdVector3 verts[8] =
		{
			{ -0.5f, -0.5f,  0.5f },
			{  0.5f, -0.5f,  0.5f },
			{  0.5f,  0.5f,  0.5f },
			{ -0.5f,  0.5f,  0.5f },
			{ -0.5f, -0.5f, -0.5f },
			{  0.5f, -0.5f, -0.5f },
			{  0.5f,  0.5f, -0.5f },
			{ -0.5f,  0.5f, -0.5f }
		};

		float inv = 1.0 / rdCamera_pCurCamera->pClipFrustum->field_0.z;
		for (int v = 0; v < 8; ++v)
		{
			rdMatrix_TransformPoint34Acc(&verts[v], &decalViewMat);

			rdVector3 proj;
			rdCamera_pCurCamera->fnProject(&proj, &verts[v]);

			// this rly needs to be made into a function or something
			if (proj.z == 0.0)
				proj.z = 0.0;
			else
				proj.z = 1.0 / proj.z;
			proj.z = proj.z * inv;
			if (rdCache_dword_865258 != 16)
				proj.z = 1.0 - proj.z;

			rdVector_Copy3(&verts[v], &proj);
		}

		// invertortho doesn't handle scale properly so we need to apply the inverse scale manually
		rdMatrix34 decalMatrixInvScale;
		rdMatrix_Copy34(&decalMatrixInvScale, &rdCache_aDecalMatrices[i]);

		size.x = 1.0f / size.x;
		size.y = 1.0f / size.y;
		size.z = 1.0f / size.z;
		rdMatrix_PreScale34(&decalMatrixInvScale, &size);
		rdMatrix_Multiply34(&decalViewMat, &rdCamera_pCurCamera->view_matrix, &decalMatrixInvScale);

		// invert it, so that the drawing is done in local space
		rdMatrix34 invDecalViewMatrix;
		rdMatrix_InvertOrtho34(&invDecalViewMatrix, &decalViewMat);
	
		// calculate the camera pos in local space
		// we could probably do this in pure view space but I'm being lazy rn
		rdMatrix34 invDecalMatrix;
		rdMatrix_InvertOrtho34(&invDecalMatrix, &decalMatrixInvScale);

		rdVector3 localCamera;
		rdMatrix_TransformPoint34(&localCamera, &rdCamera_camMatrix.scale, &invDecalMatrix);

		uint32_t flags = decal->flags;
		float radius = rdCamera_pCurCamera->pClipFrustum->field_0.y; // give it a radius to account for near plane
		if (localCamera.z - radius >= -1.0f
			&& localCamera.y - radius >= -1.0f
			&& localCamera.x - radius >= -1.0f
			&& localCamera.x + radius <= 1.0f
			&& localCamera.y + radius <= 1.0f
			&& localCamera.z + radius <= 1.0f
			)
		{
			flags |= RD_DECAL_INSIDE;
		}
		else
		{
			flags &= ~RD_DECAL_INSIDE;
		}


		std3D_DrawDecal(decal->material->textures[0].texture_struct[0], tex2_arr_sel, verts, &invDecalViewMatrix, &rdCache_aDecalColors[i], flags, rdCache_aDecalAngleFades[i]);
	}
	rdCache_numDecals = 0;
}
#endif

#ifdef PARTICLE_LIGHTS
void rdCache_DrawLight(rdLight* light, rdVector3* position)
{
	if (rdCache_numLights >= 4096)
	{
		rdCache_FlushLights();
		rdCache_numLights = 0;
	}

	rdCache_aLights[rdCache_numLights] = *light;
	rdVector_Copy3(&rdCache_aLightPositions[rdCache_numLights], position);
	++rdCache_numLights;
}

void rdCache_FlushLights()
{
	for (int i = 0; i < rdCache_numLights; ++i)
	{
		rdLight* light = &rdCache_aLights[i];

		// todo: sphere?
		rdVector3 verts[8] =
		{
			{ -light->falloffMin, -light->falloffMin,  light->falloffMin },
			{  light->falloffMin, -light->falloffMin,  light->falloffMin },
			{  light->falloffMin,  light->falloffMin,  light->falloffMin },
			{ -light->falloffMin,  light->falloffMin,  light->falloffMin },
			{ -light->falloffMin, -light->falloffMin, -light->falloffMin },
			{  light->falloffMin, -light->falloffMin, -light->falloffMin },
			{  light->falloffMin,  light->falloffMin, -light->falloffMin },
			{ -light->falloffMin,  light->falloffMin, -light->falloffMin }
		};

		float inv = 1.0 / rdCamera_pCurCamera->pClipFrustum->field_0.z;
		for (int v = 0; v < 8; ++v)
		{
			rdVector_Add3Acc(&verts[v], &rdCache_aLightPositions[i]);

			rdVector3 proj;
			rdCamera_pCurCamera->fnProject(&proj, &verts[v]);

			// this rly needs to be made into a function or something
			if (proj.z == 0.0)
				proj.z = 0.0;
			else
				proj.z = 1.0 / proj.z;
			proj.z = proj.z * inv;
			if (rdCache_dword_865258 != 16)
				proj.z = 1.0 - proj.z;

			rdVector_Copy3(&verts[v], &proj);
		}

		std3D_DrawLight(light, &rdCache_aLightPositions[i], verts);
	}
	rdCache_numLights = 0;
}
#endif



#ifdef SPHERE_AO
extern int jkPlayer_enableShadows;

void rdCache_DrawOccluder(rdVector3* position, float radius)
{
	if (!jkPlayer_enableShadows || rdCache_numOccluders >= 4096)
		return;

	rdCache_aOccluderRadii[rdCache_numOccluders] = radius;
	rdVector_Copy3(&rdCache_aOccluderPositions[rdCache_numOccluders], position);
	++rdCache_numOccluders;
}

void rdCache_FlushOccluders()
{
	if (!jkPlayer_enableShadows)
	{
		rdCache_numOccluders = 0;
		return;
	}

	for (int i = 0; i < rdCache_numOccluders; ++i)
	{
		float radius = rdCache_aOccluderRadii[i];

		rdVector3 verts[8] =
		{
			{ -radius, -radius,  radius },
			{  radius, -radius,  radius },
			{  radius,  radius,  radius },
			{ -radius,  radius,  radius },
			{ -radius, -radius, -radius },
			{  radius, -radius, -radius },
			{  radius,  radius, -radius },
			{ -radius,  radius, -radius }
		};

		float inv = 1.0 / rdCamera_pCurCamera->pClipFrustum->field_0.z;
		for (int v = 0; v < 8; ++v)
		{
			rdVector_Add3Acc(&verts[v], &rdCache_aOccluderPositions[i]);

			rdVector3 proj;
			rdCamera_pCurCamera->fnProject(&proj, &verts[v]);

			// this rly needs to be made into a function or something
			if (proj.z == 0.0)
				proj.z = 0.0;
			else
				proj.z = 1.0 / proj.z;
			proj.z = proj.z * inv;
			if (rdCache_dword_865258 != 16)
				proj.z = 1.0 - proj.z;

			rdVector_Copy3(&verts[v], &proj);
		}

		std3D_DrawOccluder(&rdCache_aOccluderPositions[i], rdCache_aOccluderRadii[i], verts);
	}
	rdCache_numOccluders = 0;
}
#endif

#endif