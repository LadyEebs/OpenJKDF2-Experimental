#include "rdroid.h"

#include "Raster/rdRaster.h"
#include "Engine/rdActive.h"
#include "Raster/rdCache.h"
#include "Primitives/rdModel3.h"
#include "General/stdPalEffects.h"
#include "General/stdMath.h"
#include "Engine/rdCamera.h"
#include "Win95/stdDisplay.h"
#include "Primitives/rdPrimit3.h"

#include "Modules/rdroid/Engine/rdCluster.h"

#include "General/stdColor.h"

#ifdef FOG
int rdroid_curFogEnabled;
rdVector4 rdroid_curFogColor;
float rdroid_curFogStartDepth;
float rdroid_curFogEndDepth;
float rdroid_curFogRange;
#endif


#ifdef RENDER_DROID2

#include "General/stdMath.h"
#include "Primitives/rdQuat.h"
#include "Modules/std/std3D.h"

typedef uint32_t rdDirtyBit;
enum RD_DIRTYBIT
{
	RD_DIRTYBIT_MODEL          = 0x1,
	RD_DIRTYBIT_VIEW           = 0x2,
	RD_DIRTYBIT_PROJECTION     = 0x4,
#ifdef MOTION_BLUR
	RD_DIRTYBIT_MODEL_PREV     = 0x8,
	RD_DIRTYBIT_VIEW_PREV      = 0x10,
#endif

	RD_DIRTYBIT_MODELVIEW      = RD_DIRTYBIT_MODEL | RD_DIRTYBIT_VIEW,
	RD_DIRTYBIT_VIEWPROJECTION = RD_DIRTYBIT_VIEW | RD_DIRTYBIT_PROJECTION,

	RD_DIRTYBIT_ALL            = 0xFF
};

static rdDirtyBit rdroid_dirtyBits = RD_DIRTYBIT_ALL;
static const rdDirtyBit rdroid_matrixBit[RD_MATRIX_TYPES] =
{
	RD_DIRTYBIT_MODEL,
	RD_DIRTYBIT_VIEW,
	RD_DIRTYBIT_PROJECTION,
#ifdef MOTION_BLUR
	RD_DIRTYBIT_MODEL_PREV,
	RD_DIRTYBIT_VIEW_PREV
#endif
};

// todo: completely remove this in favor of a light type
rdVector4 rdroid_sgBasis[RD_AMBIENT_LOBES];

void rdUpdateDirtyState();

static std3D_DrawCallHeader    rdroid_dcHeader;
static std3D_DrawCallStateBits rdroid_stateBits;
static std3D_TransformState    rdroid_transformState;
static std3D_RasterState       rdroid_rasterState;
static std3D_FogState          rdroid_fogState;
static std3D_MaterialState     rdroid_materialState;
static std3D_TextureState      rdroid_textureState;
static std3D_LightingState     rdroid_lightingState;
static std3D_ShaderState       rdroid_shaderState;

static rdMatrixMode_t rdroid_curMatrixMode = RD_MATRIX_MODEL;
static rdMatrix44     rdroid_matrices[RD_MATRIX_TYPES];
static rdMatrix44     rdroid_curCamMatrix;
static rdMatrix44     rdroid_curViewProj;
static rdMatrix44     rdroid_curProjInv;
static rdMatrix44     rdroid_curViewProjInv;

static uint32_t  rdroid_vertexColorState = 0xFFFFFFFF;
static rdVector4 rdroid_vertexTexCoordState[RD_NUM_TEXCOORDS];
static rdVector3 rdroid_vertexNormalState = { 0.0f, 0.0f, 0.0f };

static int               rdroid_vertexCacheNum = 0;
static rdVertex          rdroid_vertexCache[64];
static rdPrimitiveType_t rdroid_curPrimitiveType = RD_PRIMITIVE_NONE;

float rdroid_curZNear = 0.0f;
float rdroid_curZFar = 1.0f;

void rdResetRasterState()
{
	rdroid_stateBits.geoMode = RD_GEOMODE_TEXTURED;
	rdroid_stateBits.cullMode = RD_CULL_MODE_BACK;

	rdroid_stateBits.scissorMode = RD_SCISSOR_DISABLED;
	rdroid_rasterState.scissor.x = rdroid_rasterState.scissor.y = 0;
	rdroid_rasterState.scissor.width = 640;
	rdroid_rasterState.scissor.height = 480;

	rdroid_rasterState.viewport.x = rdroid_rasterState.viewport.y = 0;
	rdroid_rasterState.viewport.width = 640;
	rdroid_rasterState.viewport.height = 480;

	rdroid_rasterState.colorMask = 0xFFFFFFFF;
}

void rdResetBlendState()
{
	rdroid_stateBits.blend = 0;
	rdroid_stateBits.srdBlend = RD_BLEND_SRCALPHA;
	rdroid_stateBits.dstBlend = RD_BLEND_INVSRCALPHA;
}

void rdResetDepthStencilState()
{
	rdroid_stateBits.zMethod = RD_ZBUFFER_READ_WRITE;
	rdroid_stateBits.zCompare = RD_COMPARE_LESS_EQUAL;
	rdroid_curZNear = 0.0f;
	rdroid_curZFar = 1.0f;
}

void rdResetTextureState()
{
	rdroid_stateBits.alphaTest = 0;
	rdroid_stateBits.chromaKey = RD_CHROMA_KEY_DISABLED;
	rdroid_stateBits.texMode = RD_TEXTUREMODE_PERSPECTIVE;
	rdroid_stateBits.texGen = RD_TEXGEN_NONE;

	rdroid_textureState.alphaRef = 0;
	rdroid_textureState.chromaKeyColor = 0;
	rdroid_textureState.pTexture = NULL;
	rdroid_textureState.texGenParams.x = rdroid_textureState.texGenParams.y = rdroid_textureState.texGenParams.z = rdroid_textureState.texGenParams.w = 0;
	memset(&rdroid_textureState.texOffset[0], 0, sizeof(rdVector2) * RD_NUM_TEXCOORDS);
	rdroid_textureState.flags = 0;
}

void rdResetMaterialState()
{
	rdroid_materialState.fillColor    = 0xFFFFFFFF;
	rdroid_materialState.emissive     = 0xFF000000;
	rdroid_materialState.albedo       = 0xFFFFFFFF;
	rdroid_materialState.displacement = 0.0f;
}

void rdResetLightingState()
{
	rdroid_stateBits.lightMode = RD_LIGHTMODE_GOURAUD;
	memset(&rdroid_lightingState, 0, sizeof(rdroid_lightingState));
	rdroid_lightingState.overbright = 1.0f;
	//rdVector_Zero3(&rdroid_lightingState.ambientColor);
	//rdAmbient_Zero(&rdroid_lightingState.ambientLobes);
}

void rdResetShaderState()
{
	rdroid_shaderState.shader = NULL;
	memset(&rdroid_shaderState.constants, 0, sizeof(rdShaderConstants));
}
#endif

int rdStartup(HostServices *p_hs)
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
    if (bRDroidStartup)
        return 1;

    rdroid_pHS = p_hs;
    rdCache_Startup();
    rdActive_Startup();
    rdRaster_Startup();

#ifdef RENDER_DROID2
	rdResetMatrices();
	rdResetRasterState();
	rdResetBlendState();
	rdResetDepthStencilState();
	rdResetTextureState();
	rdResetMaterialState();
	rdResetLightingState();
	rdResetShaderState();
	rdroid_dcHeader.sortOrder = 0;
	rdroid_dcHeader.sortDistance = 0;
#endif

    bRDroidStartup = 1;
    return 1;
}

void rdShutdown()
{
	rdCache_Shutdown();

    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);

    if (bRDroidStartup)
        bRDroidStartup = 0;
}

int rdOpen(int a1)
{
    if (bRDroidOpen)
        return 1;

    rdroid_curGeometryMode = 5;
    rdroid_curLightingMode = 5;
    rdroid_curRenderOptions = 1;
    rdroid_curTextureMode = 3;
    rdroid_curSortingMethod = 0;
    rdroid_curOcclusionMethod = 0;
    rdroid_curCullFlags = 3;
    rdroid_curProcFaceUserData = 0;
 
#ifdef FOG
	rdroid_curFogEnabled = 0;
	rdVector_Zero3(&rdroid_curFogColor);
	rdroid_curFogStartDepth = 0.0f;
	rdroid_curFogEndDepth = rdroid_curFogRange = 10000.0f;
#endif

    stdPalEffects_ResetEffect(&rdroid_curColorEffects);

    // MOTS added
    rdroid_curVertexColorMode = 0;
    
    rdroid_frameTrue = 0;
    rdCache_ClearFrameCounters();
    rdActive_ClearFrameCounters();
    rdModel3_ClearFrameCounters();
    rdroid_curAcceleration = a1;

    bRDroidOpen = 1;
    return 1;
}

void rdClose()
{
    if (bRDroidOpen)
        bRDroidOpen = 0;
}

void rdSetRenderOptions(int a1)
{
    rdroid_curRenderOptions = a1;
}

void rdSetGeometryMode(int a1)
{
    rdroid_curGeometryMode = a1;
}

void rdSetLightingMode(int a1)
{
    rdroid_curLightingMode = a1;
}

void rdSetTextureMode(int a1)
{
    rdroid_curTextureMode = a1;
}

void rdSetSortingMethod(int a1)
{
    rdroid_curSortingMethod = a1;
}

void rdSetOcclusionMethod(int a1)
{
    rdroid_curOcclusionMethod = a1;
}

void rdSetZBufferMethod(rdZBufferMethod_t val)
{
    rdroid_curZBufferMethod = val;
#ifdef RENDER_DROID2
	rdroid_stateBits.zMethod = val;
#endif
}

void rdSetCullFlags(int a1)
{
    rdroid_curCullFlags = a1;
}

void rdSetProcFaceUserData(int a1)
{
    rdroid_curProcFaceUserData = a1;
}

// MOTS added
void rdSetVertexColorMode(int a1)
{
#ifdef RGB_THING_LIGHTS
	rdroid_curVertexColorMode = 1;
#else
    rdroid_curVertexColorMode = a1;
#endif
}

#if defined(FOG) && !defined(RENDER_DROID2)

#ifdef TILE_SW_RASTER
uint8_t rdroid_fogTable[256 * 64];
void rdUpdateFogTable()
{
	if (rdroid_curAcceleration == 0)
	{
		for (int d = 0; d < 64; ++d)
		{
			flex_t depth = (flex_t)d / 64.0f;
			depth *= rdroid_curFogColor.w;

			flex_t invDepth = (1.0f - depth) / 255.0f;
			flex_t fogR = rdroid_curFogColor.x * depth;
			flex_t fogG = rdroid_curFogColor.y * depth;
			flex_t fogB = rdroid_curFogColor.z * depth;
			for (int i = 0; i < 256; ++i)
			{
				rdColor24 color = rdColormap_pCurMap->colors[i];
				flex_t fr = (flex_t)color.r * invDepth + fogR;
				flex_t fg = (flex_t)color.g * invDepth + fogG;
				flex_t fb = (flex_t)color.b * invDepth + fogB;
				

				uint32_t r = (uint32_t)(stdMath_Saturate(fr) * 64.0f);
				uint32_t g = (uint32_t)(stdMath_Saturate(fg) * 64.0f);
				uint32_t b = (uint32_t)(stdMath_Saturate(fb) * 64.0f);
				uint32_t offset = (b << 12) | (g << 6) | r;
				rdroid_fogTable[d * 256 + i] = rdColormap_pCurMap->searchTable[offset];
			}
		}
	}
}
#endif

void rdSetFog(int active, const rdVector4* color, float startDepth, float endDepth)
{
	rdroid_curFogEnabled = active;
	rdVector_Copy4(&rdroid_curFogColor, color);
	rdroid_curFogStartDepth = startDepth;
	rdroid_curFogEndDepth = endDepth;
	rdroid_curFogRange = 1.0 / (endDepth - startDepth);
#ifdef TILE_SW_RASTER
	rdUpdateFogTable();
#endif
}
#endif

int rdGetRenderOptions(void)
{
    return rdroid_curRenderOptions;
}

int rdGetGeometryMode(void)
{
    return rdroid_curGeometryMode;
}

int rdGetLightingMode(void)
{
    return rdroid_curLightingMode;
}

int rdGetTextureMode(void)
{
    return rdroid_curTextureMode;
}

int rdGetSortingMethod(void)
{
    return rdroid_curSortingMethod;
}

int rdGetOcclusionMethod(void)
{
    return rdroid_curOcclusionMethod;
}

int rdGetZBufferMethod(void)
{
    return rdroid_curZBufferMethod;
}

int rdGetCullFlags(void)
{
    return rdroid_curCullFlags;
}

int rdGetProcFaceUserData(void)
{
    return rdroid_curProcFaceUserData;
}

// MOTS added
int rdGetVertexColorMode(void)
{
#ifdef RGB_THING_LIGHTS
	return 1;
#else
    return rdroid_curVertexColorMode;
#endif
}

int rdSetMipDistances(rdVector4 *dists)
{
    rdVector_Copy4(&rdroid_aMipDistances, dists);

#if defined(QOL_IMPROVEMENTS) && !defined(TILE_SW_RASTER)
    static rdVector4 origLod;
    static int once = 0;
    static sithWorld* onceWorld = NULL;
    if (onceWorld != sithWorld_pCurrentWorld) {
        once = 0;
    }
    if (!once) {
        origLod = sithWorld_pCurrentWorld->lodDistance;
        once = 1;
    }
    onceWorld = sithWorld_pCurrentWorld;
#ifdef TARGET_TWL
    flex_t scale_factor = 0.7;
#else
    flex_t scale_factor = (Video_format.width / 640.0) * 2.0;
#endif
    rdroid_aMipDistances.x *= scale_factor;
    rdroid_aMipDistances.y *= scale_factor;
    rdroid_aMipDistances.z *= scale_factor;
    rdroid_aMipDistances.w *= scale_factor;

    if (sithWorld_pCurrentWorld) {
        sithWorld_pCurrentWorld->lodDistance.x = origLod.x * scale_factor;
        sithWorld_pCurrentWorld->lodDistance.y = origLod.y * scale_factor;
        sithWorld_pCurrentWorld->lodDistance.z = origLod.z * scale_factor;
        sithWorld_pCurrentWorld->lodDistance.w = origLod.w * scale_factor;
    }
#endif

    return 1;
}

int rdSetColorEffects(stdPalEffect *effects)
{
    _memcpy(&rdroid_curColorEffects, effects, sizeof(rdroid_curColorEffects));
    return 1;
}

void rdAdvanceFrame()
{
  rdCache_ClearFrameCounters();
  rdActive_ClearFrameCounters();
  rdModel3_ClearFrameCounters();
  
  ++rdroid_frameTrue;
  
  rdCamera_AdvanceFrame();
  rdCache_AdvanceFrame();
}

void rdFinishFrame()
{
  rdCache_Flush("rdFinishFrame");
  rdCache_FinishFrame();
  // rdPrimit3_ClearFrameCounters(); // MOTS added
  stdDisplay_ddraw_waitforvblank(); // MOTS removed
  rdCache_ClearFrameCounters(); // MOTS removed
  rdActive_ClearFrameCounters(); // MOTS removed
  rdModel3_ClearFrameCounters(); // MOTS removed

#ifdef RENDER_DROID2
  rdResetMatrices();
  rdResetRasterState();
  rdResetBlendState();
  rdResetDepthStencilState();
  rdResetTextureState();
  rdResetMaterialState();
  rdResetLightingState();
  rdResetShaderState();
#endif
}

void rdClearPostStatistics()
{
    rdPrimit3_ClearFrameCounters();
}


#ifdef RENDER_DROID2

void rdDepthRange(float znear, float zfar)
{
	rdroid_curZNear = znear;
	rdroid_curZFar = zfar;
}

// Matrix state

void rdUpdateDirtyState()
{
	if (rdroid_dirtyBits & RD_DIRTYBIT_MODELVIEW)
	{
		rdMatrix_Multiply44(&rdroid_transformState.modelView, &rdroid_matrices[RD_MATRIX_VIEW], &rdroid_matrices[RD_MATRIX_MODEL]);
	}

#ifdef MOTION_BLUR
	if (rdroid_dirtyBits & RD_DIRTYBIT_MODEL_PREV)
		rdroid_transformState.modelPrev = rdroid_matrices[RD_MATRIX_MODEL_PREV];

	if (rdroid_dirtyBits & RD_DIRTYBIT_VIEW_PREV)
		rdroid_transformState.viewPrev = rdroid_matrices[RD_MATRIX_VIEW_PREV];
#endif

	if (rdroid_dirtyBits & RD_DIRTYBIT_VIEW)
	{
		rdroid_transformState.view = rdroid_matrices[RD_MATRIX_VIEW];
		
		rdMatrix_Invert44(&rdroid_curCamMatrix, &rdroid_matrices[RD_MATRIX_VIEW]);

		rdMatrix34 viewMat;
		rdMatrix_Copy44to34(&viewMat, &rdroid_matrices[RD_MATRIX_VIEW]);
		for (int i = 0; i < RD_AMBIENT_LOBES; ++i)
		{
			rdMatrix_TransformVector34((rdVector3*)&rdroid_sgBasis[i].x, &rdLight_sgBasis[i].x, &viewMat);
			rdroid_sgBasis[i].w = rdLight_sgBasis[i].w;
		}
	}
	
	if (rdroid_dirtyBits & RD_DIRTYBIT_VIEWPROJECTION)
	{
		rdMatrix_Multiply44(&rdroid_curViewProj, &rdroid_matrices[RD_MATRIX_PROJECTION], &rdroid_matrices[RD_MATRIX_VIEW]);
		rdMatrix_Invert44(&rdroid_curViewProjInv, &rdroid_curViewProj);
	}

	if (rdroid_dirtyBits & RD_DIRTYBIT_PROJECTION)
	{
		rdMatrix_Invert44(&rdroid_curProjInv, &rdroid_matrices[RD_MATRIX_PROJECTION]);
		rdMatrix_Copy44(&rdroid_transformState.proj, &rdroid_matrices[RD_MATRIX_PROJECTION]);
	}

	rdroid_dirtyBits = 0;
}

void rdMatrixMode(rdMatrixMode_t mode)
{
	rdroid_curMatrixMode = mode;
}

void rdPerspective(float fov, float aspect, float nearPlane, float farPlane)
{
	rdMatrix44 persp;
	rdMatrix_BuildPerspective44(&persp, fov, aspect, nearPlane, farPlane);
	rdMatrix_PreMultiply44(&rdroid_matrices[rdroid_curMatrixMode], &persp);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdOrthographic(float width, float height, float near_plane, float far_plane)
{
	rdMatrix44 ortho;
	rdMatrix_BuildOrthographic44(&ortho, -width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f, near_plane, far_plane);
	rdMatrix_PreMultiply44(&rdroid_matrices[rdroid_curMatrixMode], &ortho);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdLookat(const rdVector3* pViewer, const rdVector3* pTarget, const rdVector3* pUp)
{
	rdMatrix34 lookat;
	rdMatrix_BuildLookAt34(&lookat, pViewer, pTarget, pUp);
	rdMatrix44 lookat44;
	rdMatrix_Copy34to44(&lookat44, &lookat);

	rdMatrix_PreMultiply44(&rdroid_matrices[rdroid_curMatrixMode], &lookat44);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdTranslate(const rdVector3* pTranslation)
{
	rdMatrix_PreTranslate44(&rdroid_matrices[rdroid_curMatrixMode], pTranslation);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdRotate(const rdVector3* pRotation)
{
	rdMatrix_PreRotate44(&rdroid_matrices[rdroid_curMatrixMode], pRotation);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdScale(const rdVector4* pScaling)
{
	rdMatrix_PreScale44(&rdroid_matrices[rdroid_curMatrixMode], pScaling);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdIdentity()
{
	rdMatrix_Identity44(&rdroid_matrices[rdroid_curMatrixMode]);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdTranspose()
{
	rdMatrix44 tmp;
	rdMatrix_Copy44(&tmp, &rdroid_matrices[rdroid_curMatrixMode]);
	rdMatrix_Transpose44(&rdroid_matrices[rdroid_curMatrixMode], &tmp);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdLoadMatrix34(const rdMatrix34* pMatrix)
{
	rdMatrix_Copy34to44(&rdroid_matrices[rdroid_curMatrixMode], pMatrix);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdLoadMatrix(const rdMatrix44* pMatrix)
{
	rdMatrix_Copy44(&rdroid_matrices[rdroid_curMatrixMode], pMatrix);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdPreMultiplyMatrix(const rdMatrix44* pMatrix)
{
	rdMatrix_PreMultiply44(&rdroid_matrices[rdroid_curMatrixMode], pMatrix);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdPostMultiplyMatrix(const rdMatrix44* pMatrix)
{
	rdMatrix_PostMultiply44(&rdroid_matrices[rdroid_curMatrixMode], pMatrix);
	rdroid_dirtyBits |= rdroid_matrixBit[rdroid_curMatrixMode];
}

void rdGetMatrix(rdMatrix44* out, rdMatrixMode_t mode)
{
	rdMatrix_Copy44(out, &rdroid_matrices[mode]);
}

void rdResetMatrices()
{
	rdMatrix_Identity44(&rdroid_matrices[RD_MATRIX_MODEL]);
	rdMatrix_Identity44(&rdroid_matrices[RD_MATRIX_PROJECTION]);
	rdMatrix_Identity44(&rdroid_matrices[RD_MATRIX_VIEW]);
	rdroid_dirtyBits |= rdroid_matrixBit[RD_MATRIX_MODEL];
	rdroid_dirtyBits |= rdroid_matrixBit[RD_MATRIX_PROJECTION];
	rdroid_dirtyBits |= rdroid_matrixBit[RD_MATRIX_VIEW];
}

// Viewport
void rdViewport(float x, float y, float width, float height)
{
	rdroid_rasterState.viewport.x = x;
	rdroid_rasterState.viewport.y = y;
	rdroid_rasterState.viewport.width = width;
	rdroid_rasterState.viewport.height = height;
}

void rdGetViewport(rdViewportRect* pOut)
{
	memcpy(pOut, &rdroid_rasterState.viewport, sizeof(rdViewportRect));
}

void rdScissorMode(rdScissorMode_t mode)
{
	rdroid_stateBits.scissorMode = mode;
}

void rdScissor(int x, int y, int width, int height)
{
	rdroid_rasterState.scissor.x = (float)x / rdroid_rasterState.viewport.width;
	rdroid_rasterState.scissor.y = (float)y / rdroid_rasterState.viewport.height;
	rdroid_rasterState.scissor.width = (float)width / rdroid_rasterState.viewport.width;
	rdroid_rasterState.scissor.height = (float)height / rdroid_rasterState.viewport.height;
}

void rdScissorf(float x, float y, float width, float height)
{
	rdroid_rasterState.scissor.x = x;
	rdroid_rasterState.scissor.y = y;
	rdroid_rasterState.scissor.width = width;
	rdroid_rasterState.scissor.height = height;
}

void rdFogRange(float startDepth, float endDepth)
{
	rdroid_fogState.startDepth = startDepth;
	rdroid_fogState.endDepth = endDepth;
}

void rdFogColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	rdroid_fogState.color = RD_PACK_COLOR8(r, g, b, a);
}

void rdFogColorf(float r, float g, float b, float a)
{
	float scale = 255.0f / rdroid_lightingState.overbright;

	uint32_t ir = stdMath_ClampInt(r * scale, 0, 255);
	uint32_t ig = stdMath_ClampInt(g * scale, 0, 255);
	uint32_t ib = stdMath_ClampInt(b * scale, 0, 255);
	uint32_t ia = stdMath_ClampInt(a * 255, 0, 255);
	rdFogColor(ir, ig, ib, ia);
}

void rdFogAnisotropy(float g)
{
	rdroid_fogState.anisotropy = g;
}

void rdFogLightDir(float x, float y, float z)
{
	rdVector4 lightDir = {x, y, z, 0.0};
	rdMatrix_TransformVector44Acc(&lightDir, &rdroid_matrices[RD_MATRIX_VIEW]);

	rdroid_fogState.lightDir.x = lightDir.x;
	rdroid_fogState.lightDir.y = lightDir.y;
	rdroid_fogState.lightDir.z = lightDir.z;
}

// Primitive
int rdBeginPrimitive(rdPrimitiveType_t type)
{
	// fail if we're already building a primitive
	if (rdroid_curPrimitiveType != RD_PRIMITIVE_NONE)
	{
		rdroid_pHS->errorPrint("rdBeginPrimitive: called twice. Missing rdEndPrimitive?\n");
		return 0;
	}

	rdroid_vertexCacheNum = 0;
	rdroid_curPrimitiveType = type;
	rdroid_textureState.maxTexcoord = 0;
	return 1;
}

void rdEndPrimitive()
{
	if (rdroid_vertexCacheNum > 0)
	{
		rdUpdateDirtyState();

		std3D_DrawCallState state;
		memcpy(&state.header,         &rdroid_dcHeader,       sizeof(std3D_DrawCallHeader));
		memcpy(&state.stateBits,      &rdroid_stateBits,      sizeof(std3D_DrawCallStateBits));
		memcpy(&state.transformState, &rdroid_transformState, sizeof(std3D_TransformState));
		memcpy(&state.rasterState,    &rdroid_rasterState,    sizeof(std3D_RasterState));
		memcpy(&state.fogState,       &rdroid_fogState,       sizeof(std3D_FogState));
		memcpy(&state.materialState,  &rdroid_materialState,  sizeof(std3D_MaterialState));
		memcpy(&state.textureState,   &rdroid_textureState,   sizeof(std3D_TextureState));
		memcpy(&state.lightingState,  &rdroid_lightingState,  sizeof(std3D_LightingState));
		memcpy(&state.shaderState,    &rdroid_shaderState,    sizeof(std3D_ShaderState));

		rdCache_AddDrawCall(rdroid_curPrimitiveType, &state, rdroid_vertexCache, rdroid_vertexCacheNum);
	}

	rdroid_vertexCacheNum = 0;
	rdroid_vertexColorState = 0xFFFFFFFF;
	rdVector_Set3(&rdroid_vertexTexCoordState[0], 0.0f, 0.0f, 1.0f);
	rdVector_Set3(&rdroid_vertexTexCoordState[1], 0.0f, 0.0f, 1.0f);
	rdVector_Set3(&rdroid_vertexTexCoordState[2], 0.0f, 0.0f, 1.0f);
	rdVector_Set3(&rdroid_vertexTexCoordState[3], 0.0f, 0.0f, 1.0f);
	rdVector_Set3(&rdroid_vertexNormalState, 0.0f, 0.0f, 1.0f);
	rdroid_curPrimitiveType = RD_PRIMITIVE_NONE;
}

void rdVertex3f(float x, float y, float z)
{
	if(rdroid_vertexCacheNum >= 24)
	{
		rdroid_pHS->errorPrint("too many vertices for primitive\n");
		return;
	}

	uint32_t nx = (uint32_t)((rdroid_vertexNormalState.x * 0.5 + 0.5) * 1023.0);
	uint32_t ny = (uint32_t)((rdroid_vertexNormalState.y * 0.5 + 0.5) * 1023.0);
	uint32_t nz = (uint32_t)((rdroid_vertexNormalState.z * 0.5 + 0.5) * 1023.0);

	rdVertex* pVert = &rdroid_vertexCache[rdroid_vertexCacheNum++];
	pVert->x = x;
	pVert->y = y;
	pVert->z = z;
	pVert->norm10a2 = RD_PACK_COLOR10(nx, ny, nz, 0);
	for (int i = 0; i < RD_NUM_TEXCOORDS; ++i)
	{
		pVert->texcoords[i].u = rdroid_vertexTexCoordState[i].x;
		pVert->texcoords[i].v = rdroid_vertexTexCoordState[i].y;
		pVert->texcoords[i].w = rdroid_vertexTexCoordState[i].z;
	}
	pVert->colors[0] = rdroid_vertexColorState;
}

void rdVertex3v(const float* v)
{
	rdVertex3f(v[0], v[1], v[2]);
}

void rdColor4f(float r, float g, float b, float a)
{
	float scale = 255.0f / rdroid_lightingState.overbright;
	r *= scale;
	g *= scale;
	b *= scale;
	a *= 255.0f;

	float m = fmax(r, fmax(g, b));
	if (m > 255.0)
	{
		float inv = 255.0f / m;
		r *= inv;
		b *= inv;
		g *= inv;
	}

	uint32_t ir = stdMath_ClampInt(r, 0, 255);
	uint32_t ig = stdMath_ClampInt(g, 0, 255);
	uint32_t ib = stdMath_ClampInt(b, 0, 255);
	uint32_t ia = stdMath_ClampInt(a, 0, 255);
	rdroid_vertexColorState = RD_PACK_COLOR8(ir, ig, ib, ia);
}

void rdColor4v(const float* v)
{
	rdColor4f(v[0], v[1], v[2], v[3]);
}

void rdTexCoord2f(uint8_t i, float u, float v)
{
	if(rdroid_textureState.maxTexcoord < i)
		rdroid_textureState.maxTexcoord = i;

	rdroid_vertexTexCoordState[i].x = u;
	rdroid_vertexTexCoordState[i].y = v;
	rdroid_vertexTexCoordState[i].z = 1;
}

void rdTexCoord2i(uint8_t i, float u, float v)
{
	if (rdroid_textureState.maxTexcoord < i)
		rdroid_textureState.maxTexcoord = i;

	if(rdroid_textureState.pTexture)
	{
		uint32_t out_width, out_height;
		std3D_GetValidDimension(
			rdroid_textureState.pTexture->width,
			rdroid_textureState.pTexture->height,
			&out_width,
			&out_height);

		float w_s = (float)rdroid_textureState.pTexture->width / (float)out_width;
		float h_s = (float)rdroid_textureState.pTexture->height / (float)out_height;

		rdroid_vertexTexCoordState[i].x = w_s * (float)u / rdroid_textureState.pTexture->width;
		rdroid_vertexTexCoordState[i].y = h_s * (float)v / rdroid_textureState.pTexture->height;
	}
	else
	{
		rdroid_pHS->warningPrint("rdTexCoord2i: called without a texture bound, using default size of 32 pixels.\n");
		rdroid_vertexTexCoordState[i].x = (float)u / 32.0f;
		rdroid_vertexTexCoordState[i].y = (float)v / 32.0f;
	}
	rdroid_vertexTexCoordState[i].z = 1;
}

void rdTexCoord2v(uint8_t i, const float* v)
{
	rdTexCoord2f(i, v[0], v[1]);
}

void rdTexCoord3i(uint8_t i, float u, float v, float w)
{
	rdTexCoord2i(i, u, v);
	rdroid_vertexTexCoordState[i].z = w;
}

// todo: move this to texstate or texgen or something (rdTexScale)
void rdTexCoordScaled2i(uint8_t i, float u, float v, float w, float h)
{
	rdTexCoord2i(i, u, v);

	rdroid_vertexTexCoordState[i].x -= floor(rdroid_vertexTexCoordState[i].x);
	rdroid_vertexTexCoordState[i].y -= floor(rdroid_vertexTexCoordState[i].y);

	rdroid_vertexTexCoordState[i].x = (rdroid_vertexTexCoordState[i].x - 0.5) * w + 0.5;
	rdroid_vertexTexCoordState[i].y = (rdroid_vertexTexCoordState[i].y - 0.5) * h + 0.5;
	rdroid_vertexTexCoordState[i].y = 1.0f - rdroid_vertexTexCoordState[i].y;
}

void rdNormal3f(float x, float y, float z)
{
	rdroid_vertexNormalState.x = x;
	rdroid_vertexNormalState.y = y;
	rdroid_vertexNormalState.z = z;
}

void rdNormal3v(const float* v)
{
	rdNormal3f(v[0], v[1], v[2]);
}

// Texture
int rdBindTexture(rdTexture* pTexture)
{
	if (!pTexture)
	{
		rdroid_stateBits.alphaTest = 0;
		return 0;
	}

	// todo: texture cache here
	rdroid_textureState.pTexture = &pTexture->alphaMats[0];

	rdroid_stateBits.alphaTest = (pTexture->alpha_en & 1) ? RD_COMPARE_LESS : RD_COMPARE_ALWAYS;

	rdroid_textureState.numMips = pTexture->num_mipmaps;

	return 1;
}

int rdBindMaterial(rdMaterial* pMaterial, int cel)
{
	if(!pMaterial)
		return 0; // todo: do something when it's null? maybe bind default?

	int alpha_is_opaque = 0;

	if(cel < 0)
		cel = pMaterial->celIdx;

	cel = stdMath_ClampInt(cel, 0, pMaterial->num_texinfo - 1);

	// set the material fill color
	rdVector3 fillColor;
	rdMaterial_GetFillColor(&fillColor, pMaterial, rdColormap_pCurMap, cel, -1);
	rdSetConstantColorf(fillColor.x, fillColor.y, fillColor.z, 1.f);

	rdTexinfo* texinfo = pMaterial->texinfos[cel];
	if (!texinfo || (texinfo->header.texture_type & 8) == 0)
	{
		rdroid_textureState.pTexture = NULL;
	}
	else
	{
		rdTexture* sith_tex_sel = texinfo->texture_ptr;
		if (!rdMaterial_AddToTextureCache(pMaterial, sith_tex_sel, 0, alpha_is_opaque, cel))
			return 0;

		rdroid_textureState.pTexture = &sith_tex_sel->alphaMats[0];
		if (alpha_is_opaque)
			rdroid_textureState.pTexture = &sith_tex_sel->opaqueMats[0];
		
		// todo: move me
		rdroid_stateBits.alphaTest = (sith_tex_sel->alpha_en & 1) != 0;

		//if(sith_tex_sel->has_jkgm_override)
		{
			uint32_t emissive_rgb = RD_PACK_COLOR8F(rdroid_textureState.pTexture->emissive_factor[0], rdroid_textureState.pTexture->emissive_factor[1], rdroid_textureState.pTexture->emissive_factor[2], 0.0f);
			if(!rdroid_textureState.pTexture->emissive_texture_id)
				emissive_rgb = 0;

			rdroid_materialState.albedo = RD_PACK_COLOR8F(rdroid_textureState.pTexture->albedo_factor[0], rdroid_textureState.pTexture->albedo_factor[1], rdroid_textureState.pTexture->albedo_factor[2], rdroid_textureState.pTexture->albedo_factor[3]);
			rdroid_materialState.emissive = (rdroid_materialState.emissive & 0xFF000000) | emissive_rgb;
			rdroid_materialState.displacement = rdroid_textureState.pTexture->displacement_factor;
		}
		//else
		//{
		//	rdroid_materialState.albedo = 0xFFFFFFFF;
		//	rdroid_materialState.emissive &= 0xFF000000;
		//	if(!rdroid_textureState.pTexture->is_16bit)
		//		rdroid_materialState.emissive |= 0xFFFFFF;
		//	rdroid_materialState.displacement = 0.0f;
		//}
		rdroid_materialState.displacement = rdroid_textureState.pTexture->displacement_factor;
		rdroid_textureState.numMips = sith_tex_sel->num_mipmaps;
	}

	return 1;
}

void rdTexFilterMode(rdTexFilter_t texFilter)
{
	rdroid_stateBits.texFilter = texFilter;
}

void rdTexGen(rdTexGen_t texGen)
{
	rdroid_stateBits.texGen = texGen;
}

void rdTexGenParams(float p0, float p1, float p2, float p3)
{
	rdroid_textureState.texGenParams.x = p0;
	rdroid_textureState.texGenParams.y = p1;
	rdroid_textureState.texGenParams.z = p2;
	rdroid_textureState.texGenParams.w = p3;
}

void rdTexClampMode(int modeU, int modeV)
{
	if (modeU)
		rdroid_textureState.flags |= RD_FF_TEX_CLAMP_X;
	else
		rdroid_textureState.flags &= ~RD_FF_TEX_CLAMP_X;
		
	if (modeV)
		rdroid_textureState.flags |= RD_FF_TEX_CLAMP_Y;
	else
		rdroid_textureState.flags &= ~RD_FF_TEX_CLAMP_Y;
}

void rdTexOffset(uint8_t i, float u, float v)
{
	rdroid_textureState.texOffset[i].x = u;
	rdroid_textureState.texOffset[i].y = v;
}

void rdTexOffseti(uint8_t i, float u, float v)
{
	if (rdroid_textureState.pTexture)
	{
		uint32_t out_width, out_height;
		std3D_GetValidDimension(
			rdroid_textureState.pTexture->width,
			rdroid_textureState.pTexture->height,
			&out_width,
			&out_height);

		float w_s = (float)rdroid_textureState.pTexture->width / (float)out_width;
		float h_s = (float)rdroid_textureState.pTexture->height / (float)out_height;

		rdroid_textureState.texOffset[i].x = w_s *(float)u / (float)rdroid_textureState.pTexture->width;
		rdroid_textureState.texOffset[i].y = h_s * (float)v / (float)rdroid_textureState.pTexture->height;
	}
	else
	{
		//rdroid_pHS->warningPrint("rdTexOffseti: called without a texture bound, using default size of 32 pixels.\n");
		rdroid_textureState.texOffset[i].x = 0.0f;// (float)u / 32.0f;
		rdroid_textureState.texOffset[i].y = 0.0f;// (float)v / 32.0f;
	}
}


// States
void rdSetZBufferCompare(rdCompare_t compare)
{
	rdroid_stateBits.zCompare = compare;
}

void rdSetFogMode(rdFogMode_t mode)
{
	rdroid_stateBits.fogMode = mode;
}

void rdSetBlendEnabled(int enabled)
{
	rdroid_stateBits.blend = enabled;
}

void rdSetBlendMode(rdBlend_t src, rdBlend_t dst)
{
	rdroid_stateBits.srdBlend = src;
	rdroid_stateBits.dstBlend = dst;
}

void rdSetCullMode(rdCullMode_t mode)
{
	rdroid_stateBits.cullMode = mode;
}

void rdAlphaTestFunction(rdCompare_t mode)
{
	rdroid_stateBits.alphaTest = mode;
}

void rdSetAlphaTestReference(uint8_t ref)
{
	rdroid_textureState.alphaRef = ref;
}

void rdSetConstantColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	rdroid_materialState.fillColor = RD_PACK_COLOR8(r, g, b, a);
}

void rdSetConstantColorf(float r, float g, float b, float a)
{
	uint32_t ir = stdMath_ClampInt(r * 255, 0, 255);
	uint32_t ig = stdMath_ClampInt(g * 255, 0, 255);
	uint32_t ib = stdMath_ClampInt(b * 255, 0, 255);
	uint32_t ia = stdMath_ClampInt(a * 255, 0, 255);
	rdSetConstantColor(ir, ig, ib, ia);
}

void rdSetChromaKey(rdChromaKeyMode_t mode)
{
	rdroid_stateBits.chromaKey = mode;
}

void rdSetChromaKeyValue(uint8_t r, uint8_t g, uint8_t b)
{
	rdroid_textureState.chromaKeyColor = RD_PACK_COLOR8(r, g, b, 0);
}

void rdSortOrder(int sortOrder)
{
	rdroid_dcHeader.sortOrder = sortOrder;
}

void rdSortDistance(float distance)
{
	rdroid_dcHeader.sortDistance = distance;
}

void rdSetGeoMode(rdGeoMode_t mode)
{
	rdroid_stateBits.geoMode = stdMath_ClampInt(mode, RD_GEOMODE_VERTICES, RD_GEOMODE_TEXTURED) - 1;
}

void rdSetLightMode(rdLightMode_t mode)
{
	rdroid_stateBits.lightMode = stdMath_ClampInt(mode, 0, RD_LIGHTMODE_6_UNK);
}

void rdSetTexMode(rdTexMode_t mode)
{
	rdroid_stateBits.texMode = stdMath_ClampInt(mode, 0, RD_TEXTUREMODE_PERSPECTIVE);
}

void rdDitherMode(rdDitherMode_t mode)
{
	rdroid_stateBits.ditherMode = mode;
}

void rdSetGlowIntensity(float intensity)
{
	rdroid_materialState.emissive = (rdroid_materialState.emissive & 0x00FFFFFF) | (stdMath_ClampInt(intensity * 255, 0, 255) << 24);
}

void rdSetDecalMode(rdDecalMode_t mode)
{

}

void rdSetOverbright(float overbright)
{
	rdroid_lightingState.overbright = stdMath_Clamp(overbright, 0.1f, 4.0f);
}

void rdStencilBit(uint8_t bit)
{
	rdroid_rasterState.stencilBit = 1;
}

void rdStencilMode(uint8_t mode)
{
	rdroid_rasterState.stencilMode = mode;
}

void rdColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	rdroid_rasterState.colorMask  = r ? 0x000000FF : 0x0;
	rdroid_rasterState.colorMask |= g ? 0x0000FF00 : 0x0;
	rdroid_rasterState.colorMask |= b ? 0x00FF0000 : 0x0;
	rdroid_rasterState.colorMask |= a ? 0xFF000000 : 0x0;
}

void rdSetShader(rdShader* shader)
{
	rdroid_shaderState.shader = shader;
}

void rdSetShaderConstant(uint8_t idx, const rdVector4* value)
{
	rdroid_shaderState.constants.constants[idx] = *value;
}

void rdSetShaderConstant4f(uint8_t idx, float x, float y, float z, float w)
{
	rdroid_shaderState.constants.constants[idx].x = x;
	rdroid_shaderState.constants.constants[idx].y = y;
	rdroid_shaderState.constants.constants[idx].z = z;
	rdroid_shaderState.constants.constants[idx].w = w;
}

// Lighting
int rdAddLight(rdLight* pLight, rdVector3* pPosition)
{
	rdVector4 pos4;
	rdVector_Copy3((rdVector3*)&pos4, pPosition);
	pos4.w = 1.0f;

	rdVector4 viewPos;
	rdMatrix_TransformPoint44(&viewPos, &pos4, &rdroid_matrices[RD_MATRIX_VIEW]);

	if (pLight->type == RD_LIGHT_SPOTLIGHT)
	{
		rdVector4 dir4;
		rdVector_Copy3((rdVector3*)&dir4, &pLight->direction);
		dir4.w = 0.0f;

		rdVector4 viewDir;
		rdMatrix_TransformVector44(&viewDir, &dir4, &rdroid_matrices[RD_MATRIX_VIEW]);

		return rdCluster_AddSpotLight(pLight, (rdVector3*)&viewPos, (rdVector3*)&viewDir, pLight->width, 1.0f / rdroid_lightingState.overbright);
	}
	else if (pLight->type == RD_LIGHT_RECTANGLE)
	{
		rdVector4 dir4;
		rdVector_Copy3((rdVector3*)&dir4, &pLight->direction);
		dir4.w = 0.0f;

		rdVector4 viewDir;
		rdMatrix_TransformVector44(&viewDir, &dir4, &rdroid_matrices[RD_MATRIX_VIEW]);
		//rdVector_Normalize3Acc((rdVector3*)&dir4);

		rdVector4 viewRight;
		rdVector_Copy3((rdVector3*)&dir4, &pLight->right);
		rdMatrix_TransformVector44(&viewRight, &dir4, &rdroid_matrices[RD_MATRIX_VIEW]);
		//rdVector_Normalize3Acc((rdVector3*)&dir4);

		rdVector4 viewUp;
		rdVector_Copy3((rdVector3*)&dir4, &pLight->up);
		rdMatrix_TransformVector44(&viewUp, &dir4, &rdroid_matrices[RD_MATRIX_VIEW]);
		//rdVector_Normalize3Acc((rdVector3*)&dir4);

		return rdCluster_AddRectangleLight(pLight, (rdVector3*)&viewPos, (rdVector3*)&viewDir, (rdVector3*)&viewRight, (rdVector3*)&viewUp, pLight->width, pLight->height, 1.0f / rdroid_lightingState.overbright);
	}

	return rdCluster_AddPointLight(pLight, (rdVector3*)&viewPos, 1.0f / rdroid_lightingState.overbright);
}

extern int jkPlayer_enableShadows;
void rdAddOccluder(rdVector3* position, float radius)
{
	if (!jkPlayer_enableShadows )
		return;
	
	rdVector4 pos4;
	rdVector_Copy3(&pos4, position);
	pos4.w = 1.0f;
	
	rdVector4 viewPos;
	rdMatrix_TransformPoint44(&viewPos, &pos4, &rdroid_matrices[RD_MATRIX_VIEW]);
	rdCluster_AddOccluder((rdVector3*)&viewPos, radius, NULL);
}

extern int jkPlayer_enableDecals;
void rdAddDecal(rdDecal* decal, rdMatrix34* modelMat, rdVector3* color, rdVector3* scale, float angleFade)
{
	if(!jkPlayer_enableDecals)
		return;

	if (!decal->material)
		return;

	rdTexture* sith_tex_sel = decal->material->texinfos[0]->texture_ptr;
	if (!rdMaterial_AddToTextureCache(decal->material, sith_tex_sel, 0, 0, 0))
		return 0;

	rdDDrawSurface* tex2_arr_sel = &sith_tex_sel->alphaMats[0];
	if (!tex2_arr_sel)
		return;

//	rdMatrix34 decalMatrix;
//	rdMatrix_Multiply34(&decalMatrix, &rdroid_matrices[RD_MATRIX_VIEW], modelMat);
//	rdMatrix_Copy34(&decalMatrix, modelMat);

	rdMatrix44 decalMatrix;
	rdMatrix_Multiply44(&decalMatrix, &rdroid_matrices[RD_MATRIX_VIEW], &rdroid_matrices[RD_MATRIX_MODEL]);

	rdCluster_AddDecal(sith_tex_sel->texture_struct[0], tex2_arr_sel, scale, &decalMatrix, color, decal->flags, angleFade);
}

void rdAmbientFlags(uint32_t flags)
{
	rdroid_lightingState.ambientFlags = flags;
}

void rdExtraLight(float extra)
{
	float scale = 255.0f / rdroid_lightingState.overbright;
	extra *= scale;

	uint32_t ie = stdMath_ClampInt(extra, 0, 255);

	rdroid_lightingState.ambientColor &= 0x00FFFFFF;
	rdroid_lightingState.ambientColor |= 0xFF000000 & RD_PACK_COLOR8(0, 0, 0, ie);
}

void rdAmbientLight(float r, float g, float b)
{
	float scale = 255.0f / rdroid_lightingState.overbright;
	r *= scale;
	g *= scale;
	b *= scale;

	float m = fmax(r, fmax(g, b));
	if (m > 255.0)
	{
		float inv = 255.0f / m;
		r *= inv;
		b *= inv;
		g *= inv;
	}

	uint32_t ir = stdMath_ClampInt(r, 0, 255);
	uint32_t ig = stdMath_ClampInt(g, 0, 255);
	uint32_t ib = stdMath_ClampInt(b, 0, 255);
	rdroid_lightingState.ambientColor &= 0xFF000000;
	rdroid_lightingState.ambientColor |= 0x00FFFFFF & RD_PACK_COLOR8(ir, ig, ib, 0);
}

void rdAmbientLightSH(rdAmbient* amb)
{
	if (!amb)
	{
		memset(rdroid_lightingState.ambientLobes, 0, RD_AMBIENT_LOBES * sizeof(uint32_t));
		return;
	}

	float scale = 255.0f / rdroid_lightingState.overbright; // still rgb8, but with 4x the dynamic range
	for(int i = 0; i < RD_AMBIENT_LOBES; ++i)
	{
		float r = amb->sgs[i].x * scale;
		float g = amb->sgs[i].y * scale;
		float b = amb->sgs[i].z * scale;

		float m = fmax(r, fmax(g, b));
		if (m > 1023.0)
		{
			float inv = 255.0f / m;
			r *= inv;
			b *= inv;
			g *= inv;
		}

		uint32_t ir = stdMath_ClampInt(r, 0, 1023);
		uint32_t ig = stdMath_ClampInt(g, 0, 1023);
		uint32_t ib = stdMath_ClampInt(b, 0, 1023);
		rdroid_lightingState.ambientLobes[i] = RD_PACK_COLOR10(ir, ig, ib, 1);
	}

	rdVector4 pos4;
	rdVector_Copy3(&pos4, &amb->center);
	pos4.w = 1.0f;

	rdVector4 viewPos;
	rdMatrix_TransformPoint44(&viewPos, &pos4, &rdroid_matrices[RD_MATRIX_VIEW]);

	rdroid_lightingState.ambientCenter.x = viewPos.x;
	rdroid_lightingState.ambientCenter.y = viewPos.y;
	rdroid_lightingState.ambientCenter.z = viewPos.z;
	rdroid_lightingState.ambientCenter.w = amb->center.w;
}

int rdGenShader()
{
	return std3D_GenShader();
}

void rdDeleteShader(int id)
{
	std3D_DeleteShader(id);
}

int rdCompileShader(int id, const char* path)
{
	return 0;
}

#endif