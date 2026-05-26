#include "Modules/std/std3D.h"

#include "types.h"
#include "globals.h"

#ifdef RENDER_DROID2

#include "Raster/rdCache.h"
#include "Win95/stdDisplay.h"
#include "Win95/Window.h"
#include "World/sithWorld.h"
#include "Engine/rdColormap.h"
#include "Main/jkGame.h"
#include "World/jkPlayer.h"
#include "General/stdBitmap.h"
#include "stdPlatform.h"
#include "Engine/rdClip.h"
#include "Primitives/rdMath.h"

#include "Modules/rdroid/World/rdShader.h"

#include "jk.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "Platform/GL/shader_utils.h"
#include "Platform/GL/jkgm.h"

#include "SDL2_helper.h"

#include "General/stdMath.h"
#include "General/stdHashTable.h"
#include "Modules/std/stdJob.h"
#include "Modules/std/stdProfiler.h"
#include "General/stdString.h"

#ifdef WIN32
// Force Optimus/AMD to use non-integrated GPUs by default.
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#define TEX_MODE_TEST 0
#define TEX_MODE_WORLDPAL 1
#define TEX_MODE_BILINEAR 2
#define TEX_MODE_16BPP 5
#define TEX_MODE_BILINEAR_16BPP 6

// legacy names
#define TEX_SLOT_DIFFUSE         0
#define TEX_SLOT_EMISSIVE        1
#define TEX_SLOT_DISPLACEMENT    2

#define TEX_SLOT_TEX0			 0
#define TEX_SLOT_TEX1			 1
#define TEX_SLOT_TEX2			 2
#define TEX_SLOT_TEX3			 3
#define TEX_SLOT_REFRACTION      4
#define TEX_SLOT_WORLD_PAL       5
#define TEX_SLOT_WORLD_LIGHT_PAL 6
#define TEX_SLOT_CLUSTER_BUFFER  7
#define TEX_SLOT_DECAL_ATLAS     8
#define TEX_SLOT_DEPTH           9
#define TEX_SLOT_AO             10
#define TEX_SLOT_CLIP           11
#define TEX_SLOT_PHASE          12
#define TEX_SLOT_DIFFUSE_LIGHT  13
#define TEX_SLOT_BLACKBODY      14

// todo: should be a UBO
#define U_MODEL_MATRIX		0
#define U_PROJ_MATRIX		1
#define U_VIEW_MATRIX		2
#define U_VIEW_MATRIX_INV   3

#ifdef MOTION_BLUR

#define U_MODEL_MATRIX_PREV 4
#define U_MODEL_VIEW_PREV   5

#endif

#define U_LIGHT_MODE		6
#define U_GEO_MODE			7
#define U_BLEND_MODE		8
#define U_FLAGS				9
#define U_OVERBRIGHT		10
#define U_INV_OVERBRIGHT    11
#define U_AMB_COLOR			12
#define U_AMB_CENTER		13
#define U_AMB_NUM_SG		14
#define U_AMB_SGS			15

#define UBO_SLOT_LIGHTS        0
#define UBO_SLOT_OCCLUDERS     1
#define UBO_SLOT_DECALS        2
#define UBO_SLOT_SHARED        3
#define UBO_SLOT_FOG           4
#define UBO_SLOT_TEX           5
#define UBO_SLOT_MATERIAL      6
#define UBO_SLOT_SHADER        7
#define UBO_SLOT_SHADER_CONSTS 8

typedef struct std3DSimpleTexStage
{
    GLuint program;

    GLint attribute_coord3d;
    GLint attribute_v_color;
    GLint attribute_v_uv;
    GLint attribute_v_norm;

    GLint uniform_mvp;
	GLint uniform_proj;
	
	GLint uniform_tex;
    GLint uniform_tex2;
    GLint uniform_tex3;
	GLint uniform_tex4;
	GLint uniform_iResolution;
	GLint uniform_iResolution2;
	GLint uniform_iTime;

    GLint uniform_param1;
    GLint uniform_param2;
    GLint uniform_param3;

	GLint uniform_rt;
	GLint uniform_lt;
	GLint uniform_rb;
	GLint uniform_lb;

	GLint uniform_shared;
	GLint uniform_shader;
	GLint uniform_shaderConsts;
	GLint uniform_fog;
	GLint uniform_tex_block;
	GLint uniform_material;
	GLint uniform_lightbuf;
	GLint uniform_lights;
	GLint uniform_occluders;
	GLint uniform_decals;

	GLint uniform_tint;
	GLint uniform_filter;
	GLint uniform_fade;
	GLint uniform_add;
} std3DSimpleTexStage;

typedef struct std3DIntermediateFbo
{
    GLuint fbo;
    GLuint tex;

    GLuint rbo;
    int32_t w;
    int32_t h;

    int32_t iw;
    int32_t ih;
} std3DIntermediateFbo;

// todo: move to stdVBuffer/stdDisplay?
typedef struct std3DFramebuffer
{
	int32_t w;
	int32_t h;
	uint8_t samples;
	bool    downscale;
	int     msaaMode;

	int32_t internalWidth;
	int32_t internalHeight;

	//GLuint rbo;
	GLuint zfbo;
    GLuint fbo;
	GLuint ntex;
	GLuint ztex;
	GLuint tex0; // color
    GLuint tex1; // emissive
#ifdef MOTION_BLUR
	GLuint tex2; // velocity
#endif

	GLuint resolveFbo; // color resolved
	GLuint resolve0; // color resolved
	GLuint resolve1; // emissive resolve
#ifdef MOTION_BLUR
	GLuint resolve2; // velocity resolve
#endif
	GLuint resolveZ;
} std3DFramebuffer;

std3DIntermediateFbo window;
std3DIntermediateFbo std3D_mainFbo;

enum
{
	FBO_BLOOM  = 0x1,
	FBO_SSAO   = 0x2,
	FBO_32_BIT = 0x4
};
int std3D_framebufferFlags = 0;

#define NUM_BLOOM_LAYERS 4

std3DIntermediateFbo deferred; // opaque lighting prepass
std3DIntermediateFbo refr; // refraction tex
std3DIntermediateFbo refrZ; // refraction z clipping tex
std3DIntermediateFbo ssaoDepth;
std3DIntermediateFbo ssao;
std3DIntermediateFbo bloomLayers[NUM_BLOOM_LAYERS];

GLint std3D_windowFbo = 0;
std3DFramebuffer std3D_framebuffer;

static bool has_initted = false;

static void* last_overlay = NULL;

int init_once = 0;
GLuint programMenu;
GLint attribute_coord3d, attribute_v_color, attribute_v_light, attribute_v_uv, attribute_v_norm;

GLint uniform_mvp, uniform_tex, uniform_texEmiss, uniform_displacement_map, uniform_tex_mode, uniform_blend_mode, uniform_worldPalette, uniform_worldPaletteLights;
GLint uniform_tint, uniform_filter, uniform_fade, uniform_add, uniform_emissiveFactor, uniform_albedoFactor;
GLint uniform_light_mult, uniform_displacement_factor, uniform_iResolution, uniform_enableDither;
#ifdef FOG
GLint uniform_fog, uniform_fog_color, uniform_fog_start, uniform_fog_end;
#endif

GLuint cluster_buffer;
GLuint cluster_tbo;

GLint defaultShaderUBO;
GLint shaderConstsUbo;

// uniforms shared across draw lists during flush
typedef struct std3D_SharedUniforms
{
	rdVector4 sgBasis[RD_AMBIENT_LOBES];
	rdVector4 mipDistances;

	float     timeSeconds;
	float     pad0;//lightMult;
	float     pad1;//invlightMult;
	float     ditherScale;

	rdVector2 resolution;

	rdVector2 clusterTileSizes;
	rdVector2 clusterScaleBias;

	float deltaTime;
	float ditherScaleAlways;

	rdVector4 scale_bias[8];

	//rdVector2 resolution2;
	//rdVector2 padding;

} std3D_SharedUniforms;
std3D_SharedUniforms sharedUniforms;

GLuint shared_ubo;

typedef struct std3D_FogUniforms
{
	rdVector4 fogColor;
	int32_t   fogEnabled;
	float     fogStartDepth;
	float     fogEndDepth;
	float     fogAnisotropy;
	rdVector4 fogLightDir;
} std3D_FogUniforms;

GLuint fog_ubo;

typedef struct std3D_DrawUniforms
{
	rdMatrix44 projeciton;
	rdMatrix44 modelMatrix;
} std3D_DrawUniforms;

typedef struct std3D_TextureUniforms
{
	int32_t   tex_mode;
	int32_t   uv_mode;
	int32_t   texgen;
	int32_t   numMips;

	rdVector2 texsize;
	int32_t  texwidth;
	int32_t  texheight;

	rdVector4 uv_offset[RD_NUM_TEXCOORDS];

	rdVector4 texgen_params;

	rdVector4 padding1;
} std3D_TextureUniforms;
GLuint tex_ubo;

typedef struct std3D_MaterialUniforms
{
	rdVector4 fillColor;
	rdVector4 albedo_factor;
	rdVector4 emissive_factor;
	rdVector4 specular_factor;

	float    displacement_factor;
	float    roughnessFactor;
	float    texPad1, texPad2;
} std3D_MaterialUniforms;
GLuint material_ubo;

typedef struct std3D_LightUniformHeader
{
	uint32_t firstLight;
	uint32_t numLights;
	uint32_t lightPad0, lightPad1;
} std3D_LightUniformHeader;

typedef struct std3D_LightUniforms
{
	std3D_LightUniformHeader header;
	rdClusterLight           tmpLights[STD3D_CLUSTER_MAX_LIGHTS];
} std3D_LightUniforms;

GLuint light_ubo;

typedef struct std3D_OccluderHeader
{
	uint32_t firstOccluder;
	uint32_t numOccluders;
	uint32_t occluderPad0, occluderPad1;
} std3D_OccluderHeader;

typedef struct std3D_OccluderUniforms
{
	std3D_OccluderHeader header;
	rdClusterOccluder    tmpOccluders[STD3D_CLUSTER_MAX_OCCLUDERS];
} std3D_OccluderUniforms;

GLuint occluder_ubo;

typedef struct std3D_DecalHeader
{
	uint32_t firstDecal;
	uint32_t numDecals;
	uint32_t decalPad0, decalPad1;
} std3D_DecalHeader;

typedef struct std3D_DecalUniforms
{
	std3D_DecalHeader header;
	rdClusterDecal    tmpDecals[STD3D_CLUSTER_MAX_DECALS];
} std3D_DecalUniforms;

GLuint decal_ubo;

typedef struct std3D_decalAtlasNode
{
	char name[32];
	rdRect rect;
	struct std3D_decalAtlasNode* children[2];
	rdDDrawSurface* texture;
} std3D_decalAtlasNode;

#define DECAL_ATLAS_SIZE 1024

static int numAllocNodes = 0;
static std3D_decalAtlasNode nodePool[(DECAL_ATLAS_SIZE / 4) * (DECAL_ATLAS_SIZE / 4)];
static std3D_decalAtlasNode decalRootNode;
static stdHashTable* decalHashTable = NULL;

// define the maximum number of staging buffers needed for a whole frame to prevent reuse (vbo/ibo)
#define STD3D_STAGING_COUNT (4)

// try not to add too many permutations
typedef enum STD3D_WORLD_STAGE_INDEX
{
	WORLD_STAGE_COLOR,
	WORLD_STAGE_COLOR_ALPHATEST,

	//SHADER_COLOR_REFRACTION,

	WORLD_STAGE_COUNT
} STD3D_WORLD_STAGE_INDEX;

typedef enum STD3D_WORLD_REG_POOL
{
	WORLD_REG_2, // 2 registers
	WORLD_REG_4, // 4 registers
	WORLD_REG_8, // 8 registers

	WORLD_REG_COUNT
} STD3D_WORLD_REG_POOL;

typedef struct std3D_worldStage
{
	GLuint program;
	GLuint vao[STD3D_STAGING_COUNT];
} std3D_worldStage;

std3D_worldStage worldStages[WORLD_STAGE_COUNT][WORLD_REG_COUNT][RD_NUM_TEXCOORDS];

#define FOR_EACH_STAGE(FUNC) \
	for (int i = 0; i < RD_NUM_TEXCOORDS; ++i) \
	for (int j = 0; j < WORLD_REG_COUNT; ++j) \
	for (int k = 0; k < WORLD_STAGE_COUNT; ++k) \
	{ \
		std3D_worldStage* pStage = &worldStages[k][j][i]; \
		FUNC; \
	}
		

GLint programMenu_attribute_coord3d, programMenu_attribute_v_color, programMenu_attribute_v_uv;
GLint programMenu_uniform_mvp, programMenu_uniform_tex, programMenu_uniform_displayPalette;

std3DSimpleTexStage std3D_uiProgram;
std3DSimpleTexStage std3D_texFboStage;
std3DSimpleTexStage std3D_deferredStage;
std3DSimpleTexStage std3D_postfxStage;
std3DSimpleTexStage std3D_bloomStage;
std3DSimpleTexStage std3D_ssaoStage[2];
std3DSimpleTexStage std3D_motionblurStage;
std3DSimpleTexStage std3D_resolveStage;

std3DSimpleTexStage std3D_decalAtlasStage;
std3DIntermediateFbo decalAtlasFBO;

unsigned int vao;
GLuint blank_tex, blank_tex_white;
void* blank_data = NULL, *blank_data_white = NULL;
GLuint worldpal_texture;
void* worldpal_data = NULL;
GLuint worldpal_lights_texture;
void* worldpal_lights_data = NULL;
GLuint displaypal_texture;
void* displaypal_data = NULL;
GLuint tiledrand_texture;
rdVector3 tiledrand_data[4 * 4];

uint16_t* phase_data = NULL;
GLuint phase_texture;
GLuint blackbody_texture;

GLuint linearSampler[4];
GLuint nearestSampler[4];

size_t std3D_loadedUITexturesAmt = 0;
stdBitmap* std3D_aUIBitmaps[STD3D_MAX_TEXTURES] = {0};
GLuint std3D_aUITextures[STD3D_MAX_TEXTURES] = {0};
static rdUITri GL_tmpUITris[STD3D_MAX_UI_TRIS] = {0};
static size_t GL_tmpUITrisAmt = 0;
GLuint last_ui_tex = 0;
int last_ui_flags = 0;
static D3DVERTEX GL_tmpUIVertices[STD3D_MAX_UI_VERTICES] = {0};
static size_t GL_tmpUIVerticesAmt = 0;

rdDDrawSurface* std3D_aLoadedSurfaces[STD3D_MAX_TEXTURES] = {0};
GLuint std3D_aLoadedTextures[STD3D_MAX_TEXTURES] = {0};
size_t std3D_loadedTexturesAmt = 0;
static rdTri GL_tmpTris[STD3D_MAX_TRIS] = {0};
static size_t GL_tmpTrisAmt = 0;
static rdLine GL_tmpLines[STD3D_MAX_VERTICES] = {0};
static size_t GL_tmpLinesAmt = 0;
static D3DVERTEX GL_tmpVertices[STD3D_MAX_VERTICES] = {0};
static size_t GL_tmpVerticesAmt = 0;
static size_t rendered_tris = 0;

static void* loaded_colormap = NULL;

rdDDrawSurface* last_tex = NULL;
int last_flags = 0;

int bufferIdx;
GLuint world_vbo_all[STD3D_STAGING_COUNT];
GLuint world_ibo_triangle[STD3D_STAGING_COUNT];

D3DVERTEX* menu_data_all = NULL;
GLushort* menu_data_elements = NULL;
GLuint menu_vao;
GLuint menu_vbo_all;
GLuint menu_ibo_triangle;

extern int jkGuiBuildMulti_bRendering;

int std3D_bInitted = 0;
rdColormap std3D_ui_colormap;
int std3D_bReinitHudElements = 0;

static inline bool std3D_isIntegerFormat(GLuint format)
{
	switch (format)
	{
	case GL_R8UI:
	case GL_R16UI:
	case GL_R16I:
	case GL_R32UI:
	case GL_R32I:
		return true;
	default:
		return false;
	}
}

static inline GLuint std3D_getUploadFormat(GLuint format)
{
	switch (format)
	{
	case GL_R3_G3_B2:
		return GL_UNSIGNED_BYTE_3_3_2;
	case GL_RGB565:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GL_RGBA4:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GL_RGB5_A1:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GL_R8:
	case GL_RG8:
	case GL_RGB8:
	case GL_RGBA8:
		return GL_UNSIGNED_BYTE;
	case GL_RGB10_A2:
		return GL_UNSIGNED_INT_2_10_10_10_REV;
	case GL_R8_SNORM:
	case GL_RG8_SNORM:
	case GL_RGB8_SNORM:
	case GL_RGBA8_SNORM:
		return GL_BYTE;
	case GL_R16:
	case GL_RG16:
	case GL_RGB16:
	case GL_RGBA16:
		return GL_UNSIGNED_SHORT;
	case GL_R16_SNORM:
	case GL_RG16_SNORM:
	case GL_RGB16_SNORM:
	case GL_RGBA16_SNORM:
		return GL_SHORT;
	case GL_R11F_G11F_B10F:
	case GL_R16F:
	case GL_RG16F:
	case GL_RGB16F:
	case GL_RGBA16F:
		return GL_HALF_FLOAT;
	case GL_R32F:
	case GL_RG32F:
	case GL_RGB32F:
	case GL_RGBA32F:
		return GL_FLOAT;
	case GL_R32UI:
	case GL_RGBA32UI:
		return GL_UNSIGNED_INT;
	case GL_R32I:
		return GL_INT;
	case GL_R16UI:
		return GL_UNSIGNED_SHORT;
	case GL_R16I:
		return GL_SHORT;
	case GL_R8UI:
		return GL_UNSIGNED_BYTE;
	case GL_RGB4:
	case GL_RGB5:
	default:
		return GL_UNSIGNED_BYTE;
	};
}

static inline uint8_t std3D_getNumChannels(GLuint format)
{
	switch (format)
	{
	case GL_R8:
	case GL_R8_SNORM:
	case GL_R16:
	case GL_R16_SNORM:
	case GL_R16F:
	case GL_R32F:
	case GL_R8UI:
	case GL_R16UI:
	case GL_R16I:
	case GL_R32UI:
	case GL_R32I:
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24:
	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH_COMPONENT32F:
		return 1;
	case GL_RG8:
	case GL_RG8_SNORM:
	case GL_RG16:
	case GL_RG16_SNORM:
	case GL_RG16F:
	case GL_RG16UI:
	case GL_RG16I:
	case GL_RG32F:
		return 2;
	case GL_R3_G3_B2:
	case GL_RGB4:
	case GL_RGB5:
	case GL_RGB565:
	case GL_RGB8:
	case GL_RGB8_SNORM:
	case GL_RGB16:
	case GL_RGB16_SNORM:
	case GL_RGB16F:
	case GL_RGB32F:
	case GL_R11F_G11F_B10F:
	case GL_SRGB8:
		return 3;
	case GL_RGBA4:
	case GL_RGB5_A1:
	case GL_RGBA8:
	case GL_RGBA8_SNORM:
	case GL_RGB10_A2:
	case GL_RGBA16:
	case GL_RGBA16_SNORM:
	case GL_RGBA16F:
	case GL_RGBA32F:
	case GL_RGBA32UI:
	case GL_SRGB8_ALPHA8:
		return 4;
	default:
		break;
	}
	return 0;
}

static inline GLuint std3D_getImageFormat(GLuint format)
{
	static GLuint typeForChannels[] =
	{
		GL_RGB, // 0 channels
		GL_RED,
		GL_RG,
		GL_RGB,
		GL_RGBA
	};
	static GLuint intTypeForChannels[] =
	{
		GL_RGB_INTEGER, // 0 channels
		GL_RED_INTEGER,
		GL_RG_INTEGER,
		GL_RGB_INTEGER,
		GL_RGBA_INTEGER
	};
	bool isInteger = std3D_isIntegerFormat(format);
	int numChannels = std3D_getNumChannels(format);
	return isInteger ? intTypeForChannels[numChannels] : typeForChannels[numChannels];
}

void std3D_PushDebugGroup(const char* name)
{
	if(GLEW_KHR_debug)
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void std3D_PopDebugGroup()
{
	if(GLEW_KHR_debug)
		glPopDebugGroup();
}

inline void std3D_bindTexture(int type, int texId, int slot)
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(type, texId);
}

// shader is just a handle around a buffer of code info
int std3D_GenShader()
{
	int buffer;
	glGenBuffers(1, &buffer);
	return buffer;
}

void std3D_DeleteShader(int id)
{
	glDeleteBuffers(1, &id);
}

void std3D_UploadShader(rdShader* shader)
{
	glBindBuffer(GL_UNIFORM_BUFFER, shader->shaderid);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(rdShaderByteCode), &shader->byteCode, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}


void std3D_initDefaultShader()
{
	defaultShaderUBO = std3D_GenShader();

	rdShaderByteCode shader;
	memset(&shader, 0, sizeof(shader));

	shader.version = 1.0;
	shader.instructionCount = 4;

	shader.instructions[0].op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_TEX, RD_SHADER_U8, 0, RD_SWIZZLE_XYZW, 0, RD_WRITE_RGBA, 0, 0, 0, 0);
	shader.instructions[0].src0 = rdShader_AssembleSrc(0, RD_SHADER_TEX, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);
	shader.instructions[0].src1 = rdShader_AssembleSrc(1, RD_SHADER_TEX, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);

	shader.instructions[1].op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_MUL, RD_SHADER_U8, 0, RD_SWIZZLE_XYZW, 0, RD_WRITE_RGBA, 0, 0, 0, 0);
	shader.instructions[1].src0 = rdShader_AssembleSrc(0, RD_SHADER_GPR, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);
	shader.instructions[1].src1 = rdShader_AssembleSrc(1, RD_SHADER_CLR, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);

	shader.instructions[2].op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_TEX, RD_SHADER_U8, 1, RD_SWIZZLE_XYZW, 0, RD_WRITE_RGBA, 0, 0, 0, 0);
	shader.instructions[2].src0 = rdShader_AssembleSrc(0, RD_SHADER_TEX, RD_SHADER_U8, 1, 0, RD_SWIZZLE_XYZW, 0, 0, 0);
	shader.instructions[2].src1 = rdShader_AssembleSrc(1, RD_SHADER_TEX, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);

	shader.instructions[3].op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_MAX, RD_SHADER_U8, 0, RD_SWIZZLE_XYZW, 0, RD_WRITE_RGB, 0, 0, 0, 0);
	shader.instructions[3].src0 = rdShader_AssembleSrc(0, RD_SHADER_GPR, RD_SHADER_U8, 0, 0, RD_SWIZZLE_XYZW, 0, 0, 0);
	shader.instructions[3].src1 = rdShader_AssembleSrc(1, RD_SHADER_GPR, RD_SHADER_U8, 1, 0, RD_SWIZZLE_XYZW, 0, 0, 0);

	glBindBuffer(GL_UNIFORM_BUFFER, defaultShaderUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(rdShaderByteCode), &shader, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void std3D_generateIntermediateFbo(int32_t width, int32_t height, std3DIntermediateFbo* pFbo, uint32_t format, int mipMaps, int linear, int useDepth, int rbo)
{
    // Generate the framebuffer
    memset(pFbo, 0, sizeof(*pFbo));

    pFbo->w = width;
    pFbo->h = height;
    pFbo->iw = width;
    pFbo->ih = height;

    glActiveTexture(GL_TEXTURE0);

    glGenFramebuffers(1, &pFbo->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pFbo->fbo);
    
    // Set up our framebuffer texture
    glGenTextures(1, &pFbo->tex);
    glBindTexture(GL_TEXTURE_2D, pFbo->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, std3D_getImageFormat(format), std3D_getUploadFormat(format), NULL);
	if (linear)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mipMaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipMaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mipMaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipMaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
	}
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    if(mipMaps)
    {
    //    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFbo->tex, 0);

    // Set up our render buffer
	if(useDepth)
	{
		if(rbo == 0)
		{
			glGenRenderbuffers(1, &pFbo->rbo);
			glBindRenderbuffer(GL_RENDERBUFFER, pFbo->rbo);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
			// Bind it to our framebuffer fb
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pFbo->rbo);
		}
		else
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, rbo, 0);
			//glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
		}
	}

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void std3D_deleteIntermediateFbo(std3DIntermediateFbo* pFbo)
{
    glDeleteFramebuffers(1, &pFbo->fbo);
    glDeleteTextures(1, &pFbo->tex);
	if(pFbo->rbo)
		glDeleteRenderbuffers(1, &pFbo->rbo);

	memset(pFbo, 0, sizeof(std3DIntermediateFbo));
}

void std3D_generateFramebuffer(int32_t width, int32_t height, std3DFramebuffer* pFb)
{
    // Generate the framebuffer
    memset(pFb, 0, sizeof(*pFb));

    pFb->w = width;
    pFb->h = height;

	if (jkPlayer_enable32Bit)
		std3D_framebufferFlags |= FBO_32_BIT;
	else
		std3D_framebufferFlags &= ~FBO_32_BIT;

    glActiveTexture(GL_TEXTURE0);

#ifdef CHROMA_SUBSAMPLING
	GLuint fboFormat = jkPlayer_enable32Bit ? GL_RG16_SNORM : GL_RG8_SNORM;
	GLuint fboLayout = GL_RG;
#else
	GLuint fboFormat = jkPlayer_enable32Bit ? GL_RGB10_A2 : GL_RGB5_A1;
	GLuint fboLayout = GL_RGBA;
#endif

	pFb->msaaMode = stdMath_ClampInt(jkPlayer_multiSample, GL_ARB_sample_locations ? SAMPLE_MODE_MIN : SAMPLE_NONE, SAMPLE_MODE_MAX);
	pFb->samples = abs(pFb->msaaMode) << 1;
	pFb->downscale = pFb->msaaMode < SAMPLE_NONE;

	pFb->internalWidth = width;
	pFb->internalHeight = height;
	if (pFb->downscale)
	{
		if (pFb->msaaMode == SAMPLE_2x1)
		{
			// 2:1 horizontal
			// 1:1 vertical
			pFb->internalWidth /= 2;
		}
		else if (pFb->msaaMode == SAMPLE_2x2)
		{
			// 2:1 horizontal
			// 2:1 vertical
			pFb->internalWidth /= 2;
			pFb->internalHeight /= 2;
		}
	}

    // Set up our framebuffer texture
	// we never really use the alpha channel, so for 32bit we use deep color (rgb10a20, and for 16bit we use high color (rgb5a1, to avoid green shift)
	glGenTextures(1, &pFb->resolve0);
	glBindTexture(GL_TEXTURE_2D, pFb->resolve0);
	glTexImage2D(GL_TEXTURE_2D, 0, fboFormat, width, height, 0, fboLayout, GL_UNSIGNED_BYTE, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);//GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);//GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if(pFb->samples)
	{
		glGenTextures(1, &pFb->tex0);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pFb->tex0);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, pFb->samples, fboFormat, pFb->internalWidth, pFb->internalHeight, GL_TRUE);

		//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	else
	{
		pFb->tex0 = pFb->resolve0;
	}
	
	if(jkPlayer_enableBloom)
	{
		std3D_framebufferFlags |= FBO_BLOOM;

		GLuint emissiveFormat = jkPlayer_enable32Bit ? GL_RGB10_A2 : GL_RGB565;
		GLuint emissiveLayout = jkPlayer_enable32Bit ? GL_RGBA : GL_RGB;

		glGenTextures(1, &pFb->resolve1);
		glBindTexture(GL_TEXTURE_2D, pFb->resolve1);
		glTexImage2D(GL_TEXTURE_2D, 0, emissiveFormat, width, height, 0, emissiveLayout, GL_UNSIGNED_BYTE, NULL);

		// linear sampler for bloom downsample
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);

		// Set up our emissive fb texture
		if (pFb->samples)
		{
			glGenTextures(1, &pFb->tex1);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pFb->tex1);
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, pFb->samples, emissiveFormat, pFb->internalWidth, pFb->internalHeight, GL_TRUE);
			
			//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		else
		{
			pFb->tex1 = pFb->resolve1;
		}
    }
	else
	{
		std3D_framebufferFlags &= ~FBO_BLOOM;
	}

#ifdef MOTION_BLUR
	{
		glGenTextures(1, &pFb->resolve2);
		glBindTexture(GL_TEXTURE_2D, pFb->resolve2);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);

		if (pFb->samples != 1)
		{
			glGenTextures(1, &pFb->tex2);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pFb->tex2);
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, pFb->samples, GL_RGBA16F, pFb->internalWidth, pFb->internalHeight, GL_TRUE);
			glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_BASE_LEVEL, 0);
		}
		else
		{
			pFb->tex2 = pFb->resolve2;
		}		
	}
#endif

	// Set up our depth buffer texture
	glGenTextures(1, &pFb->resolveZ);
	glBindTexture(GL_TEXTURE_2D, pFb->resolveZ);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (pFb->samples)
	{
		glGenTextures(1, &pFb->ztex);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, pFb->ztex);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, pFb->samples, GL_DEPTH24_STENCIL8, pFb->internalWidth, pFb->internalHeight, GL_TRUE);
		
		//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//glTexParameteri(GL_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	else
	{
		pFb->ztex = pFb->resolveZ;
	}


	glGenFramebuffers(1, &pFb->resolveFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, pFb->resolveFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFb->resolve0, 0);
		
	if (std3D_framebufferFlags & FBO_BLOOM)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pFb->resolve1, 0);

#ifdef MOTION_BLUR
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, pFb->resolve2, 0);
#endif
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, pFb->resolveZ, 0);

	if (pFb->samples)
	{
		glGenFramebuffers(1, &pFb->fbo);			
		glBindFramebuffer(GL_FRAMEBUFFER, pFb->fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, pFb->tex0, 0);
		
		if (std3D_framebufferFlags & FBO_BLOOM)
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D_MULTISAMPLE, pFb->tex1, 0);
		
#ifdef MOTION_BLUR
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D_MULTISAMPLE, pFb->tex2, 0);
#endif
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, pFb->ztex, 0);

		if (pFb->downscale)
		{
			glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_PROGRAMMABLE_SAMPLE_LOCATIONS_ARB, 1);
			if (pFb->msaaMode == SAMPLE_2x1)
			{
				static const rdVector2 samples[2] =
				{
					{ 0.75f, 0.5f },
					{ 0.25f, 0.5f }
				};
				glFramebufferSampleLocationsfvARB(pFb->fbo, 0, 2, samples);
			}
			else if (pFb->msaaMode == SAMPLE_2x2)
			{
				static const rdVector2 samples[4] =
				{
					{0.25f, 0.25f},
					{0.75f, 0.25f},
					{0.25f, 0.75f},
					{0.75f, 0.75f},
				};
				glFramebufferSampleLocationsfvARB(pFb->fbo, 0, 4, samples);
			}
		}
	}
	else
	{
		pFb->fbo = pFb->resolveFbo;
	}

    // Set up our render buffer
   // glGenRenderbuffers(1, &pFb->rbo);
   // glBindRenderbuffer(GL_RENDERBUFFER, pFb->rbo);
   // glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
   // glBindRenderbuffer(GL_RENDERBUFFER, 0);
   // 
   // // Bind it to our framebuffer fb
   // glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pFb->rbo);
    
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

	//glGenFramebuffers(1, &pFb->zfbo);
	//glBindFramebuffer(GL_FRAMEBUFFER, pFb->zfbo);
	//
	//// Attach fbTex to our currently bound framebuffer fb
	////glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFb->ztex, 0);
	//
	//// Set up our normal texture
	////glGenTextures(1, &pFb->ntex);
	////glBindTexture(GL_TEXTURE_2D, pFb->ntex);
	////glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	////glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);//_MIPMAP_NEAREST);
	////glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);//_MIPMAP_NEAREST);
	////glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	////glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	////glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFb->ntex, 0);
	//
	////glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pFb->rbo);
	//glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, pFb->ztex, 0);
	//
	//if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	//	stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void std3D_generateExtraFramebuffers(int32_t width, int32_t height)
{
	glActiveTexture(GL_TEXTURE0);

	if (jkPlayer_enableSSAO)
	{
		int ssao_w = width > 320 ? width / 2 : width;
		int ssao_h = ssao_w * ((float)height / width);

		std3D_generateIntermediateFbo(width / 2, height / 2, &ssaoDepth, GL_R16F, 0, 0, 0, 0);
		std3D_generateIntermediateFbo(width, height, &ssao, GL_R8, 0, 0, 0, 0);//1, std3D_framebuffer.resolveZ);
		std3D_framebufferFlags |= FBO_SSAO;
	}
	else
		std3D_framebufferFlags &= ~FBO_SSAO;

	if (std3D_framebufferFlags & FBO_BLOOM)
	{
		int bloom_w = width > 320 ? 640 : 320;
		int bloom_h = bloom_w * ((float)height / width);

		std3D_generateIntermediateFbo(bloom_w, bloom_h, &bloomLayers[0], GL_R11F_G11F_B10F, 0, 1, 0, 0);
		for (int i = 1; i < NUM_BLOOM_LAYERS; ++i)
			std3D_generateIntermediateFbo(bloomLayers[i - 1].w / 2, bloomLayers[i - 1].h / 2, &bloomLayers[i], GL_R11F_G11F_B10F, 0, 1, 0, 0);
	}

	std3D_generateIntermediateFbo(width, height, &deferred, GL_R32F, 0, 0, 0, 0);
	//std3D_generateIntermediateFbo(width, height, &deferred, GL_RGBA8, 0, 0, 0);

	// the refraction buffers use the same depth-stencil as the main scene
#ifdef CHROMA_SUBSAMPLING
	GLuint refrFormat = GL_RG8_SNORM;
#else
	//GLuint refrFormat = GL_RGBA8;// GL_RGB565;
	GLuint refrFormat = GL_RGB5_A1;//jkPlayer_enable32Bit ? GL_RGB10_A2 : GL_RGB5_A1;
#endif

	std3D_generateIntermediateFbo(width >> 1, height >> 1, &refr, refrFormat, 0, 0, 0, 0);// 1, 0, 1, std3D_framebuffer.ztex);
	//std3D_generateIntermediateFbo(width, height, &refrZ, GL_R32F, 0, 0, 1, std3D_framebuffer.ztex);
	std3D_generateIntermediateFbo(width >> 1, height >> 1, &refrZ, refrFormat, 0, 0, 0, 0);// 1, 0, 1, std3D_framebuffer.ztex);

	std3D_mainFbo.fbo = std3D_framebuffer.resolveFbo;
	std3D_mainFbo.tex = std3D_framebuffer.resolve0;
	//std3D_mainFbo.rbo = std3D_framebuffer.rbo;
	std3D_mainFbo.w = std3D_framebuffer.w;
	std3D_mainFbo.h = std3D_framebuffer.h;
	std3D_mainFbo.iw = std3D_framebuffer.w;
	std3D_mainFbo.ih = std3D_framebuffer.h;

	window.fbo = std3D_windowFbo;
	window.w = Window_xSize;
	window.h = Window_ySize;
	window.iw = Window_xSize;
	window.ih = Window_ySize;
}

void std3D_deleteFramebuffer(std3DFramebuffer* pFb)
{
	if (pFb->fbo != pFb->resolveFbo)
		glDeleteFramebuffers(1, &pFb->fbo);

	if (pFb->tex0 != pFb->resolve0)
		glDeleteTextures(1, &pFb->tex0);

	if (pFb->tex1 != pFb->resolve1)
		glDeleteTextures(1, &pFb->tex1);
#ifdef MOTION_BLUR
	if (pFb->tex2 != pFb->resolve2)
		glDeleteTextures(1, &pFb->tex2);
#endif

	if (pFb->ztex != pFb->resolveZ)
		glDeleteTextures(1, &pFb->ztex);

    glDeleteFramebuffers(1, &pFb->resolveFbo);
    glDeleteTextures(1, &pFb->resolve0);
    glDeleteTextures(1, &pFb->resolve1);
#ifdef MOTION_BLUR
	glDeleteTextures(1, &pFb->resolve2);
#endif
    //glDeleteRenderbuffers(1, &pFb->rbo);
	//glDeleteTextures(1, &pFb->ntex);
	glDeleteTextures(1, &pFb->resolveZ);
	glDeleteFramebuffers(1, &pFb->zfbo);

	memset(pFb, 0, sizeof(std3DFramebuffer));
}

void std3D_deleteExtraFramebuffers()
{
	std3D_deleteIntermediateFbo(&ssao);
	std3D_deleteIntermediateFbo(&ssaoDepth);
	for (int i = 0; i < NUM_BLOOM_LAYERS; ++i)
		std3D_deleteIntermediateFbo(&bloomLayers[i]);

	std3D_deleteIntermediateFbo(&deferred);
	std3D_deleteIntermediateFbo(&refr);
	std3D_deleteIntermediateFbo(&refrZ);
}

#ifdef HW_VBUFFER
typedef struct std3D_DrawSurface
{
	stdVBuffer* parent;

	GLuint fbo;
	GLuint tex;

	GLuint rbo;
	int32_t w;
	int32_t h;

	int32_t iw;
	int32_t ih;

	void* data;

	std3D_DrawSurface* prev;
	std3D_DrawSurface* next;
} std3D_DrawSurface;

std3D_DrawSurface* drawSurfaces = NULL;

void BLAH()
{
	if (glGetError() == GL_INVALID_OPERATION)
		printf("fuckkk\n");
}

void std3D_SetupDrawSurface(std3D_DrawSurface* surface)
{
	glActiveTexture(GL_TEXTURE0);

	glGenFramebuffers(1, &surface->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, surface->fbo);
	BLAH();

	// Set up our framebuffer texture
	glGenTextures(1, &surface->tex);
	glBindTexture(GL_TEXTURE_2D, surface->tex);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, std3D_getImageFormat(GL_RGBA8), std3D_getUploadFormat(GL_RGBA8), NULL);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	BLAH();

	// Attach fbTex to our currently bound framebuffer fb
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->tex, 0);
	BLAH();

	int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");

		switch (status)
		{
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			stdPlatform_Printf("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
			stdPlatform_Printf("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			stdPlatform_Printf("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED:
			stdPlatform_Printf("GL_FRAMEBUFFER_UNSUPPORTED\n");
			break;
		default:
			break;
		}
	}
	BLAH();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void std3D_PurgeDrawSurface(std3D_DrawSurface* surface)
{
	if(surface->fbo)
		glDeleteFramebuffers(1, &surface->fbo);
	
	if(surface->tex)
		glDeleteTextures(1, &surface->tex);

	if (surface->rbo)
		glDeleteRenderbuffers(1, &surface->rbo);

	surface->fbo = surface->tex = surface->rbo = 0;
}

void std3D_PurgeDrawSurfaces()
{
	std3D_DrawSurface* surface = drawSurfaces;
	while(surface)
	{
		std3D_PurgeDrawSurface(surface);
		surface = surface->next;
	}
}

// todo: use the format...
int std3D_AllocDrawSurface(stdVBuffer* vbuffer, int32_t width, int32_t height)
{
	std3D_DrawSurface* surface = malloc(sizeof(std3D_DrawSurface));
	if(!surface)
		return 0;
	memset(surface, 0, sizeof(std3D_DrawSurface));

	surface->next = drawSurfaces;
	if (drawSurfaces)
		drawSurfaces->prev = surface;

	surface->prev = NULL;
	drawSurfaces = surface;
	
	//width = stdMath_NextPow2(width);
	//height = stdMath_NextPow2(height);

	surface->w = width;
	surface->h = height;
	surface->iw = width;
	surface->ih = height;
	surface->parent = vbuffer;

//	std3D_SetupDrawSurface(surface);

	vbuffer->device_surface = surface;
	return 1;
}

void std3D_FreeDrawSurface(stdVBuffer* vbuffer)
{
	std3D_DrawSurface* surface = vbuffer->device_surface;
	if(!surface)
		return;

	std3D_DrawSurface* prev = surface->prev;
	std3D_DrawSurface* next = surface->next;
	if (prev)
	{
		prev->next = next;
		if (next)
			next->prev = prev;
	}
	else
	{
		drawSurfaces = next;
		if (next)
		{
			next->prev = NULL;
		}
	}

	surface->prev = NULL;
	surface->next = NULL;

	if (surface->data)
	{
		free(surface->data);
		surface->data = 0;
	}

	std3D_PurgeDrawSurface(surface);

	free(surface);
}

void std3D_MakeDrawSurfaceResident(std3D_DrawSurface* surface)
{
	if (surface->fbo == 0)
	{
		std3D_SetupDrawSurface(surface);

		if (surface->data)
		{
			glBindTexture(GL_TEXTURE_2D, surface->tex);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->data);
			BLAH();
		}
	}
}

void std3D_UploadDrawSurface(std3D_DrawSurface* src, int width, int height, void* pixels, uint8_t* palette)
{
	std3D_MakeDrawSurfaceResident(src);

	glBindTexture(GL_TEXTURE_2D, src->tex);

	uint8_t* image_8bpp = pixels;
	uint16_t* image_16bpp = pixels;
	uint8_t* pal = palette;

	// temp, currently all RGBA8
	uint32_t* image_data = src->data ? src->data : malloc(width * height * 4);

	if (0)//src->fmt.format.colorMode)
	{
		for (int j = 0; j < height; j++)
		{
			for (int i = 0; i < width; i++)
			{
				uint32_t index = j * width + i;
				uint32_t val_rgba = 0x00000000;

				uint16_t val = image_16bpp[index];
				if (!src->parent->format.format.g_bits == 6) // RGB565
				{
					uint8_t val_a1 = 1;
					uint8_t val_r5 = (val >> 11) & 0x1F;
					uint8_t val_g6 = (val >> 5) & 0x3F;
					uint8_t val_b5 = (val >> 0) & 0x1F;

					uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
					uint8_t val_r8 = (val_r5 * 527 + 23) >> 6;
					uint8_t val_g8 = (val_g6 * 259 + 33) >> 6;
					uint8_t val_b8 = (val_b5 * 527 + 23) >> 6;

#ifdef __NOTDEF_TRANSPARENT_BLACK
					uint8_t transparent_r8 = (vbuf->transparent_color >> 16) & 0xFF;
					uint8_t transparent_g8 = (vbuf->transparent_color >> 8) & 0xFF;
					uint8_t transparent_b8 = (vbuf->transparent_color >> 0) & 0xFF;

					if (val_r8 == transparent_r8 && val_g8 == transparent_g8 && val_b8 == transparent_b8)
					{
						val_a1 = 0;
					}
#endif // __NOTDEF_TRANSPARENT_BLACK

					val_rgba |= (val_a8 << 24);
					val_rgba |= (val_b8 << 16);
					val_rgba |= (val_g8 << 8);
					val_rgba |= (val_r8 << 0);
				}
				else // RGB1555
				{
					uint8_t val_a1 = (val >> 15);
					uint8_t val_r5 = (val >> 10) & 0x1F;
					uint8_t val_g5 = (val >> 5) & 0x1F;
					uint8_t val_b5 = (val >> 0) & 0x1F;

					uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
					uint8_t val_r8 = (val_r5 * 527 + 23) >> 6;
					uint8_t val_g8 = (val_g5 * 527 + 23) >> 6;
					uint8_t val_b8 = (val_b5 * 527 + 23) >> 6;

					val_rgba |= (val_a8 << 24);
					val_rgba |= (val_b8 << 16);
					val_rgba |= (val_g8 << 8);
					val_rgba |= (val_r8 << 0);
				}

				*(uint32_t*)(image_data + index * 4) = val_rgba;
			}
		}

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, image_data);
	}
	else
	{
		for (int j = 0; j < height; j++)
		{
			for (int i = 0; i < width; i++)
			{
				uint32_t index = j * width + i;
				uint32_t val_rgba = image_8bpp[index] == 0 ? 0x00000000 : 0xFF000000;

				if (pal)
				{
					uint8_t val = image_8bpp[index];
					val_rgba |= (pal[(val * 3) + 2] << 0);
					val_rgba |= (pal[(val * 3) + 1] << 8);
					val_rgba |= (pal[(val * 3) + 0] << 16);
				}
				else
				{
					uint8_t val = image_8bpp[index];
					rdColor24* pal_master = (rdColor24*)stdDisplay_masterPalette;//stdDisplay_gammaPalette;
					rdColor24* color = &pal_master[val];
					val_rgba |= (color->r << 0);
					val_rgba |= (color->g << 8);
					val_rgba |= (color->b << 16);
				}

				image_data[index] = val_rgba;
			}
		}
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
		BLAH();
	}

	src->data = image_data;

	//free(image_data);
}

void std3D_BlitDrawSurface(std3D_DrawSurface* src, rdRect* srcRect, std3D_DrawSurface* dst, rdRect* dstRect)
{
	if(!src || !dst || !srcRect || !dstRect)
		return;

	std3D_PushDebugGroup("std3D_BlitDrawSurface");

	std3D_MakeDrawSurfaceResident(src);
	std3D_MakeDrawSurfaceResident(dst);
	
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	//
	//glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo);
	//BLAH();
	//
	//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->fbo);
	//BLAH();
	//
	//glDrawBuffer(GL_COLOR_ATTACHMENT0);
	//BLAH();
	//
	//glReadBuffer(GL_COLOR_ATTACHMENT0);
	//BLAH();
	//
	//
	//int srcX0 = srcRect->x;
	//int srcX1 = srcRect->x + srcRect->width;
	//int srcY0 = srcRect->y;
	//int srcY1 = srcRect->y + srcRect->height;
	//
	//int dstX0 = dstRect->x;
	//int dstX1 = dstRect->x + dstRect->width;
	//int dstY0 = dstRect->y;
	//int dstY1 = dstRect->y + dstRect->height;
	//
	//glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	//BLAH();

	glViewport(dstRect->x, dstRect->y, dstRect->width, dstRect->height);
	
	glBindFramebuffer(GL_FRAMEBUFFER, dst->fbo);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthFunc(GL_ALWAYS);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	
	glBlendEquation(GL_FUNC_ADD);
	glUseProgram(std3D_texFboStage.program);
	
	glBindVertexArray(vao);
	glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D, src->tex);
	
	glUniform1i(std3D_texFboStage.uniform_tex, 0);
	
	glUniform1f(std3D_texFboStage.uniform_param1, 1.0f);
	glUniform1f(std3D_texFboStage.uniform_param2, 1.0f);
	glUniform1f(std3D_texFboStage.uniform_param3, 1.0f);
	
	glUniform1i(std3D_texFboStage.uniform_tex, TEX_SLOT_DIFFUSE);
	
	glDrawArrays(GL_TRIANGLES, 0, 3);

	//glBindFramebuffer(GL_FRAMEBUFFER, src->fbo);
	//glReadBuffer(GL_COLOR_ATTACHMENT0);
	//BLAH();
	//
	//
	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, dst->tex);
	//BLAH();
	//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	//
	//glCopyTexSubImage2D(GL_TEXTURE_2D, 0, dstRect->x, dstRect->y, srcRect->x, srcRect->y, srcRect->width, srcRect->height);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	std3D_PopDebugGroup();
}

void std3D_ClearDrawSurface(std3D_DrawSurface* surface, int fillColor, rdRect* rect)
{
	std3D_MakeDrawSurfaceResident(surface);

	std3D_PushDebugGroup("std3D_ClearDrawSurface");


	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, surface->fbo);
	BLAH();

	// it's very unclear what vbuffer fill color format is... might match the format of the fb?
	float a = ((fillColor >> 24) & 0xFF) / 255.0f;
	float r = ((fillColor >> 16) & 0xFF) / 255.0f;
	float g = ((fillColor >> 8) & 0xFF) / 255.0f;
	float b = ((fillColor >> 0) & 0xFF) / 255.0f;

	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);
	BLAH();

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	
	std3D_PopDebugGroup();
}
#endif

void std3D_swapFramebuffers()
{
}

GLuint std3D_loadProgram(const char* fpath_base, const char* userDefines)
{
    GLuint out;
    GLint link_ok = GL_FALSE;
    
    char* tmp_vert = malloc(strlen(fpath_base) + 32);
    char* tmp_frag = malloc(strlen(fpath_base) + 32);
    
    strcpy(tmp_vert, fpath_base);
    strcat(tmp_vert, "_v.glsl");
    
    strcpy(tmp_frag, fpath_base);
    strcat(tmp_frag, "_f.glsl");
    
    GLuint vs, fs;
    if ((vs = load_shader_file(tmp_vert, GL_VERTEX_SHADER, userDefines))   == 0) return 0;
    if ((fs = load_shader_file(tmp_frag, GL_FRAGMENT_SHADER, userDefines)) == 0) return 0;
    
    free(tmp_vert);
    free(tmp_frag);
    
    out = glCreateProgram();
    glAttachShader(out, vs);
    glAttachShader(out, fs);
    glLinkProgram(out);
    glGetProgramiv(out, GL_LINK_STATUS, &link_ok);
    if (!link_ok) 
    {
        print_log(out);
        return 0;
    }
    
    return out;
}

GLint std3D_tryFindAttribute(GLuint program, const char* attribute_name)
{
    GLint out = glGetAttribLocation(program, attribute_name);
    if (out == -1) {
        stdPlatform_Printf("std3D: Could not bind attribute %s!\n", attribute_name);
    }
    return out;
}

GLint std3D_tryFindUniform(GLuint program, const char* uniform_name)
{
    GLint out = glGetUniformLocation(program, uniform_name);
    if (out == -1) {
        stdPlatform_Printf("std3D: Could not bind uniform %s!\n", uniform_name);
    }
    return out;
}

bool std3D_loadSimpleTexProgram(const char* fpath_base, const char* defines, std3DSimpleTexStage* pOut)
{
    if (!pOut) return false;
    if ((pOut->program = std3D_loadProgram(fpath_base, defines)) == 0) return false;
    
    pOut->attribute_coord3d = std3D_tryFindAttribute(pOut->program, "coord3d");
    pOut->attribute_v_color = std3D_tryFindAttribute(pOut->program, "v_color");
    pOut->attribute_v_uv = std3D_tryFindAttribute(pOut->program, "v_uv");
    pOut->uniform_mvp = std3D_tryFindUniform(pOut->program, "mvp");
	pOut->uniform_proj = std3D_tryFindUniform(pOut->program, "projMatrix");
    pOut->uniform_iResolution = std3D_tryFindUniform(pOut->program, "iResolution");
	pOut->uniform_iResolution2 = std3D_tryFindUniform(pOut->program, "iResolution2");
	pOut->uniform_iTime = std3D_tryFindUniform(pOut->program, "iTime");
    pOut->uniform_tex = std3D_tryFindUniform(pOut->program, "tex");
    pOut->uniform_tex2 = std3D_tryFindUniform(pOut->program, "tex2");
    pOut->uniform_tex3 = std3D_tryFindUniform(pOut->program, "tex3");
	pOut->uniform_tex4 = std3D_tryFindUniform(pOut->program, "tex4");

    pOut->uniform_param1 = std3D_tryFindUniform(pOut->program, "param1");
    pOut->uniform_param2 = std3D_tryFindUniform(pOut->program, "param2");
    pOut->uniform_param3 = std3D_tryFindUniform(pOut->program, "param3");

	pOut->uniform_rt = std3D_tryFindUniform(pOut->program, "cameraRT");
	pOut->uniform_lt = std3D_tryFindUniform(pOut->program, "cameraLT");
	pOut->uniform_rb = std3D_tryFindUniform(pOut->program, "cameraRB");
	pOut->uniform_lb = std3D_tryFindUniform(pOut->program, "cameraLB");

	pOut->uniform_tint = std3D_tryFindUniform(pOut->program, "colorEffects_tint");
	pOut->uniform_filter = std3D_tryFindUniform(pOut->program, "colorEffects_filter");
	pOut->uniform_fade = std3D_tryFindUniform(pOut->program, "colorEffects_fade");
	pOut->uniform_add = std3D_tryFindUniform(pOut->program, "colorEffects_add");
	
	pOut->uniform_lightbuf = std3D_tryFindUniform(pOut->program, "clusterBuffer");
	pOut->uniform_shared = glGetUniformBlockIndex(pOut->program, "sharedBlock");
	pOut->uniform_shader = glGetUniformBlockIndex(pOut->program, "shaderBlock");
	pOut->uniform_shaderConsts = glGetUniformBlockIndex(pOut->program, "shaderConstantsBlock");
	pOut->uniform_fog = glGetUniformBlockIndex(pOut->program, "fogBlock");
	pOut->uniform_tex_block = glGetUniformBlockIndex(pOut->program, "textureBlock");
	pOut->uniform_material = glGetUniformBlockIndex(pOut->program, "materialBlock");
	pOut->uniform_lights = glGetUniformBlockIndex(pOut->program, "lightBlock");
	pOut->uniform_occluders = glGetUniformBlockIndex(pOut->program, "occluderBlock");
	pOut->uniform_decals = glGetUniformBlockIndex(pOut->program, "decalBlock");

	//glUniformBlockBinding(pOut->program, pOut->uniform_lights, UBO_SLOT_LIGHTS);
	//glUniformBlockBinding(pOut->program, pOut->uniform_occluders, UBO_SLOT_OCCLUDERS);
	//glUniformBlockBinding(pOut->program, pOut->uniform_decals, UBO_SLOT_DECALS);
	//glUniformBlockBinding(pOut->program, pOut->uniform_shared, UBO_SLOT_SHARED);
	//glUniformBlockBinding(pOut->program, pOut->uniform_fog, UBO_SLOT_FOG);
	//glUniformBlockBinding(pOut->program, pOut->uniform_tex_block, UBO_SLOT_TEX);
	//glUniformBlockBinding(pOut->program, pOut->uniform_material, UBO_SLOT_MATERIAL);
	//glUniformBlockBinding(pOut->program, pOut->uniform_shader, UBO_SLOT_SHADER);
	//glUniformBlockBinding(pOut->program, pOut->uniform_shaderConsts, UBO_SLOT_SHADER_CONSTS);

    return true;
}

int std3D_loadWorldStage(std3D_worldStage* pStage, int isZPass, const char* defines)
{
	if ((pStage->program = std3D_loadProgram(isZPass ? "shaders/world/depth" : "shaders/world/world", defines)) == 0)
		return 0;

//	pStage->attribute_coord3d = std3D_tryFindAttribute(pStage->program, "coord3d");
//	pStage->attribute_v_color = std3D_tryFindAttribute(pStage->program, "v_color");
//	pStage->attribute_v_light = std3D_tryFindAttribute(pStage->program, "v_light");
//	pStage->attribute_v_uv    = std3D_tryFindAttribute(pStage->program, "v_uv");
//	pStage->attribute_v_norm  = std3D_tryFindAttribute(pStage->program, "v_normal");
//
//	pStage->uniform_projection = std3D_tryFindUniform(pStage->program, "projMatrix");
//	pStage->uniform_modelMatrix = std3D_tryFindUniform(pStage->program, "modelMatrix");
//	pStage->uniform_viewMatrix = std3D_tryFindUniform(pStage->program, "viewMatrix");
//	pStage->uniform_ambient_color = std3D_tryFindUniform(pStage->program, "ambientColor");
//	pStage->uniform_ambient_sg = std3D_tryFindUniform(pStage->program, "ambientSG");
//	pStage->uniform_ambient_sg_count = std3D_tryFindUniform(pStage->program, "ambientNumSG");
//	pStage->uniform_ambient_center = std3D_tryFindUniform(pStage->program, "ambientCenter");
//	pStage->uniform_fillColor = std3D_tryFindUniform(pStage->program, "fillColor");
//	pStage->uniform_textures = std3D_tryFindUniform(pStage->program, "textures");
//	pStage->uniform_tex = std3D_tryFindUniform(pStage->program, "tex");
//	pStage->uniform_texEmiss = std3D_tryFindUniform(pStage->program, "texEmiss");
//	pStage->uniform_worldPalette = std3D_tryFindUniform(pStage->program, "worldPalette");
//	pStage->uniform_worldPaletteLights = std3D_tryFindUniform(pStage->program, "worldPaletteLights");
//	pStage->uniform_displacement_map = std3D_tryFindUniform(pStage->program, "displacement_map");
//	pStage->uniform_texDecals = std3D_tryFindUniform(pStage->program, "decalAtlas");
//	pStage->uniform_texz = std3D_tryFindUniform(pStage->program, "ztex");
//	pStage->uniform_texssao = std3D_tryFindUniform(pStage->program, "ssaotex");
//	pStage->uniform_texrefraction = std3D_tryFindUniform(pStage->program, "refrtex");
//	pStage->uniform_texclip = std3D_tryFindUniform(pStage->program, "cliptex");
//	pStage->uniform_geo_mode = std3D_tryFindUniform(pStage->program, "geoMode");
//	pStage->uniform_dithertex = std3D_tryFindUniform(pStage->program, "dithertex");
//	pStage->uniform_diffuse_light = std3D_tryFindUniform(pStage->program, "diffuseLightTex");
//	pStage->uniform_blackbody_tex = std3D_tryFindUniform(pStage->program, "blackbodyTex");
//	pStage->uniform_light_mode = std3D_tryFindUniform(pStage->program, "lightMode");
//	pStage->uniform_ao_flags = std3D_tryFindUniform(pStage->program, "aoFlags");
//
//	pStage->uniform_lightbuf = std3D_tryFindUniform(pStage->program, "clusterBuffer");
	//pStage->uniform_shared = glGetUniformBlockIndex(pStage->program, "sharedBlock");
	//pStage->uniform_shader = glGetUniformBlockIndex(pStage->program, "shaderBlock");
	//pStage->uniform_shaderConsts = glGetUniformBlockIndex(pStage->program, "shaderConstantsBlock");
	//pStage->uniform_fog = glGetUniformBlockIndex(pStage->program, "fogBlock");
	//pStage->uniform_tex_block = glGetUniformBlockIndex(pStage->program, "textureBlock");
	//pStage->uniform_material = glGetUniformBlockIndex(pStage->program, "materialBlock");
	//pStage->uniform_lights = glGetUniformBlockIndex(pStage->program, "lightBlock");
	//pStage->uniform_occluders = glGetUniformBlockIndex(pStage->program, "occluderBlock");
	//pStage->uniform_decals = glGetUniformBlockIndex(pStage->program, "decalBlock");

//	pStage->uniform_rightTop = std3D_tryFindUniform(pStage->program, "rightTop");
//	pStage->uniform_rt = std3D_tryFindUniform(pStage->program, "cameraRT");
//	pStage->uniform_lt = std3D_tryFindUniform(pStage->program, "cameraLT");
//	pStage->uniform_rb = std3D_tryFindUniform(pStage->program, "cameraRB");
//	pStage->uniform_lb = std3D_tryFindUniform(pStage->program, "cameraLB");

	return 1;
}

void std3D_setupMenuVAO()
{
	glGenVertexArrays(1, &menu_vao);
	glBindVertexArray(menu_vao);

	glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
	glVertexAttribPointer(
		programMenu_attribute_coord3d, // attribute
		3,                 // number of elements per vertex, here (x,y,z)
		GL_FLOAT,          // the type of each element
		GL_FALSE,          // normalize fixed-point data?
		sizeof(D3DVERTEX),                 // data stride
		(GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
	);

	glVertexAttribPointer(
		programMenu_attribute_v_color, // attribute
		4,                 // number of elements per vertex, here (R,G,B,A)
		GL_UNSIGNED_BYTE,  // the type of each element
		GL_TRUE,          // normalize fixed-point data?
		sizeof(D3DVERTEX),                 // no extra data between each position
		(GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
	);

	glVertexAttribPointer(
		programMenu_attribute_v_uv,    // attribute
		2,                 // number of elements per vertex, here (U,V)
		GL_FLOAT,          // the type of each element
		GL_FALSE,          // take our values as-is
		sizeof(D3DVERTEX),                 // no extra data between each position
		(GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
	);

	glEnableVertexAttribArray(programMenu_attribute_coord3d);
	glEnableVertexAttribArray(programMenu_attribute_v_color);
	glEnableVertexAttribArray(programMenu_attribute_v_uv);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);

	glBindVertexArray(vao);
}

void std3D_setupUBOs()
{
	// shared buffer
	glGenBuffers(1, &shared_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, shared_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_SharedUniforms), NULL, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &shaderConstsUbo);
	glBindBuffer(GL_UNIFORM_BUFFER, shaderConstsUbo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(rdShaderConstants), NULL, GL_DYNAMIC_DRAW);

	// fog buffer
	glGenBuffers(1, &fog_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, fog_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_FogUniforms), NULL, GL_DYNAMIC_DRAW);

	// texture buffer
	glGenBuffers(1, &tex_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, tex_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_TextureUniforms), NULL, GL_DYNAMIC_DRAW);

	// material buffer	
	glGenBuffers(1, &material_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, material_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_MaterialUniforms), NULL, GL_DYNAMIC_DRAW);

	// light buffer
	glGenBuffers(1, &light_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, light_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_LightUniforms), NULL, GL_DYNAMIC_DRAW);

	// occluder buffer
	glGenBuffers(1, &occluder_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, occluder_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_OccluderUniforms), NULL, GL_DYNAMIC_DRAW);

	// decal buffer
	glGenBuffers(1, &decal_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, decal_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(std3D_DecalUniforms), NULL, GL_DYNAMIC_DRAW);

	// cluster buffer
	//int maxsize;
	//glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxsize);
	//printf("MAX TEX BUFFER %d\n",  maxsize);
	glGenBuffers(1, &cluster_buffer);
	glBindBuffer(GL_TEXTURE_BUFFER, cluster_buffer);
	glBufferData(GL_TEXTURE_BUFFER, sizeof(uint32_t) * STD3D_CLUSTER_GRID_TOTAL_SIZE, NULL, GL_DYNAMIC_DRAW);

	glGenTextures(1, &cluster_tbo);
	glBindTexture(GL_TEXTURE_BUFFER, cluster_tbo);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, cluster_buffer);

	glBindBuffer(GL_TEXTURE_BUFFER, 0);
	glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void std3D_setupLightingUBO(std3D_worldStage* pStage)
{
	//glUniformBlockBinding(pStage->program, pStage->uniform_lights, UBO_SLOT_LIGHTS);
	//glUniformBlockBinding(pStage->program, pStage->uniform_occluders, UBO_SLOT_OCCLUDERS);
	//glUniformBlockBinding(pStage->program, pStage->uniform_decals, UBO_SLOT_DECALS);
	//glUniformBlockBinding(pStage->program, pStage->uniform_shared, UBO_SLOT_SHARED);
	//glUniformBlockBinding(pStage->program, pStage->uniform_fog, UBO_SLOT_FOG);
	//glUniformBlockBinding(pStage->program, pStage->uniform_tex_block, UBO_SLOT_TEX);
	//glUniformBlockBinding(pStage->program, pStage->uniform_material, UBO_SLOT_MATERIAL);
	//glUniformBlockBinding(pStage->program, pStage->uniform_shader, UBO_SLOT_SHADER);
	//glUniformBlockBinding(pStage->program, pStage->uniform_shaderConsts, UBO_SLOT_SHADER_CONSTS);
}

void std3D_setupDrawCallVAO(std3D_worldStage* pStage)
{
	glGenVertexArrays(STD3D_STAGING_COUNT, pStage->vao);

	for (int i = 0; i < STD3D_STAGING_COUNT; ++i)
	{
		glBindVertexArray(pStage->vao[i]);

		glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all[i]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, world_ibo_triangle[i]);

		{
			glVertexAttribPointer(
				0,//pStage->attribute_coord3d, // attribute
				3,                 // number of elements per vertex, here (x,y,z)
				GL_FLOAT,          // the type of each element
				GL_FALSE,          // normalize fixed-point data?
				sizeof(rdVertex),                 // data stride
				(GLvoid*)offsetof(rdVertex, x)                  // offset of first element
			);
			glEnableVertexAttribArray(0);//pStage->attribute_coord3d);

			glVertexAttribPointer(
				1,//pStage->attribute_v_norm, // attribute
				GL_BGRA,                 // number of elements per vertex, here (x,y,z)
				GL_UNSIGNED_INT_2_10_10_10_REV,          // the type of each element
				GL_TRUE,          // normalize fixed-point data?
				sizeof(rdVertex), // data stride
				(GLvoid*)offsetof(rdVertex, norm10a2) // offset of first element
			);
			glEnableVertexAttribArray(1);//pStage->attribute_v_norm);

			for (int i = 0; i < RD_NUM_COLORS; ++i)
			{
				glVertexAttribPointer(
					2+i,//pStage->attribute_v_color + i, // attribute
					4,                 // number of elements per vertex, here (R,G,B,A)
					GL_UNSIGNED_BYTE,  // the type of each element
					GL_TRUE,          // normalize fixed-point data?
					sizeof(rdVertex),                 // no extra data between each position
					(GLvoid*)offsetof(rdVertex, colors[i]) // offset of first element
				);
				glEnableVertexAttribArray(2+i);//pStage->attribute_v_color + i);
			}

			for (int i = 0; i < RD_NUM_TEXCOORDS; ++i)
			{
				glVertexAttribPointer(
					4+i,				//pStage->attribute_v_uv + i,    // attribute
					3,                 // number of elements per vertex, here (U,V,W)
					GL_FLOAT,          // the type of each element
					GL_FALSE,          // take our values as-is
					sizeof(rdVertex),                 // no extra data between each position
					(GLvoid*)offsetof(rdVertex, texcoords[i])                  // offset of first element
				);
				glEnableVertexAttribArray(4+i);//pStage->attribute_v_uv + i);
			}
		}
	}
	glBindVertexArray(vao);
}

int init_resources()
{
    stdPlatform_Printf("std3D: OpenGL init...\n");

    std3D_bReinitHudElements = 1;

    memset(std3D_aUITextures, 0, sizeof(std3D_aUITextures));

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &std3D_windowFbo);

    int32_t tex_w = Window_xSize;
    int32_t tex_h = Window_ySize;

    std3D_generateFramebuffer(tex_w, tex_h, &std3D_framebuffer);
	std3D_generateExtraFramebuffers(tex_w, tex_h);

    if ((programMenu = std3D_loadProgram("shaders/menu", "")) == 0)
		return false;

	for (int j = 0; j < RD_NUM_TEXCOORDS; ++j)
	{
		for (int i = 0; i < WORLD_REG_COUNT; ++i)
		{
			char tmp[128];
			sprintf_s(tmp, 128, "WORLD;REG_COUNT %d;UV_SETS %d",
					  2 << i, j + 1);

			if (!std3D_loadWorldStage(&worldStages[WORLD_STAGE_COLOR][i][j], 0, tmp)) return false;

			char alphatmp[128];
			sprintf_s(alphatmp, 128, "ALPHA_DISCARD;WORLD;REG_COUNT %d;UV_SETS %d",
					  2 << i, j + 1);

			if (!std3D_loadWorldStage(&worldStages[WORLD_STAGE_COLOR_ALPHATEST][i][j], 0, alphatmp)) return false;
		}
	}

    if (!std3D_loadSimpleTexProgram("shaders/ui", "", & std3D_uiProgram)) return false;
    if (!std3D_loadSimpleTexProgram("shaders/texfbo", "", &std3D_texFboStage)) return false;
	if (!std3D_loadSimpleTexProgram("shaders/deferred/deferred", "", &std3D_deferredStage)) return false;
	if (!std3D_loadSimpleTexProgram("shaders/postfx/postfx", "COMPOSITE", &std3D_postfxStage)) return false;
#ifdef MOTION_BLUR
	if (!std3D_loadSimpleTexProgram("shaders/postfx/postfx", "MOTION_BLUR_PASS", &std3D_motionblurStage)) return false;	
#endif
	if (!std3D_loadSimpleTexProgram("shaders/postfx/postfx", "RESOLVE", &std3D_resolveStage)) return false;
	if (!std3D_loadSimpleTexProgram("shaders/postfx/bloom", "", &std3D_bloomStage)) return false;
	if (!std3D_loadSimpleTexProgram("shaders/ssao", "SAMPLING", &std3D_ssaoStage[0])) return false;
	if (!std3D_loadSimpleTexProgram("shaders/ssao", "COMPOSITE", &std3D_ssaoStage[1])) return false;
	if (!std3D_loadSimpleTexProgram("shaders/decal_insert", "", &std3D_decalAtlasStage)) return false;

	std3D_generateIntermediateFbo(DECAL_ATLAS_SIZE, DECAL_ATLAS_SIZE, &decalAtlasFBO, GL_RGBA8, 0, 9, 0, 0);

	decalRootNode.rect.x = 0.0f;
	decalRootNode.rect.y = 0.0f;
	decalRootNode.rect.width = DECAL_ATLAS_SIZE;
	decalRootNode.rect.height = DECAL_ATLAS_SIZE;
   
    programMenu_attribute_coord3d = std3D_tryFindAttribute(programMenu, "coord3d");
    programMenu_attribute_v_color = std3D_tryFindAttribute(programMenu, "v_color");
    programMenu_attribute_v_uv = std3D_tryFindAttribute(programMenu, "v_uv");
    programMenu_uniform_mvp = std3D_tryFindUniform(programMenu, "mvp");
    programMenu_uniform_tex = std3D_tryFindUniform(programMenu, "tex");
    programMenu_uniform_displayPalette = std3D_tryFindUniform(programMenu, "displayPalette");
   
	// samplers
	int wrap[2] = { GL_REPEAT, GL_CLAMP_TO_EDGE };
	glGenSamplers(4, linearSampler);
	glGenSamplers(4, nearestSampler);
	for (int i = 0; i < 4; ++i)
	{
		int wrapX = wrap[i % 2];
		int wrapY = wrap[i / 2];

		glSamplerParameteri(linearSampler[i], GL_TEXTURE_WRAP_S, wrapX);
		glSamplerParameteri(linearSampler[i], GL_TEXTURE_WRAP_T, wrapY);
		glSamplerParameteri(linearSampler[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(linearSampler[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		
		glSamplerParameteri(nearestSampler[i], GL_TEXTURE_WRAP_S, wrapX);
		glSamplerParameteri(nearestSampler[i], GL_TEXTURE_WRAP_T, wrapY);
		glSamplerParameteri(nearestSampler[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(nearestSampler[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	}

    // Blank texture
    glGenTextures(1, &blank_tex);
    blank_data = jkgm_alloc_aligned(0x400);
    memset(blank_data, 0x0, 0x400);
    
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, blank_data);

    // Blank texture
    glGenTextures(1, &blank_tex_white);
    blank_data_white = jkgm_alloc_aligned(0x400);
    memset(blank_data_white, 0xFF, 0x400);
    
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, blank_data_white);

    // World palette
    glGenTextures(1, &worldpal_texture);
    worldpal_data = jkgm_alloc_aligned(0x300);
    memset(worldpal_data, 0xFF, 0x300);
    
    glBindTexture(GL_TEXTURE_2D, worldpal_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);

    // World palette lights
    glGenTextures(1, &worldpal_lights_texture);
    worldpal_lights_data = jkgm_alloc_aligned(0x4000);
    memset(worldpal_lights_data, 0xFF, 0x4000);
    
    glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 0x40, 0, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
    
    
    // Display palette
    glGenTextures(1, &displaypal_texture);
    displaypal_data = jkgm_alloc_aligned(0x400);
    memset(displaypal_data, 0xFF, 0x300);
    
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, displaypal_data);

    // Tiled random
    glGenTextures(1, &tiledrand_texture);

    memset(tiledrand_data, 0, 4 * 4 * sizeof(rdVector3));
    for (int i = 0; i < 4*4; i++)
    {
        tiledrand_data[i].x = (_frand() * 2.0) - 1.0;
        tiledrand_data[i].y = (_frand() * 2.0) - 1.0;
		tiledrand_data[i].z = (_frand() * 2.0) - 1.0;
		rdVector_Normalize3Acc(&tiledrand_data[i]);
    }

    glBindTexture(GL_TEXTURE_2D, tiledrand_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, tiledrand_data);

	// phase texture
	glGenTextures(1, &phase_texture);

	glBindTexture(GL_TEXTURE_2D, phase_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	phase_data = jkgm_alloc_aligned(sizeof(uint16_t) * 256 * 256);
	for (int y = 0; y < 256; ++y)
	for (int x = 0; x < 256; ++x)
	{
		float u = (float)x / 256.0f;
		float v = (float)y / 256.0f;

		float g  = u * 2.0f - 1.0f;
		float mu = v * 2.0f - 1.0f;
	
		// Henyey-Greensteinphase function
		//float f = (1.0f - g * g) / (4.0f * M_PI * powf(1.0f + g * g - 2.0f * g * mu , 3.0f / 2.0f));

		// Cornette-Shanks
		const float k = 3.0 / (8.0 * M_PI);
		float gg = g * g;
		float num = (1.0 - gg) * (1.0 + mu * mu);
		float den = 1.0 + gg - 2.0 * g * mu;
		      den = (2.0 + gg) * sqrtf(den * den * den);
		float f = k * num / den;

		phase_data[y * 256 + x] = stdMath_FloatToHalf(4.0 * M_PI * f);
	}
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 256, 256, 0, GL_RED, GL_HALF_FLOAT, phase_data);

	// blackbody
	glGenTextures(1, &blackbody_texture);
	glBindTexture(GL_TEXTURE_1D, blackbody_texture);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	rdVector3 bbd[256];
	for (int i = 0; i < 256; ++i)
	{
		float t = (float)i / 256.0f;
		t *= 3000.0;

		float u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t)
			/ (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t);

		float v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t)
			/ (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t);

		float x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0);
		float y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0);
		float z = 1.0 - x - y;

		float Y = 1.0;
		float X = Y / y * x;
		float Z = Y / y * z;

		rdMatrix33 XYZtoRGB;
		rdVector_Set3(&XYZtoRGB.rvec, 3.2404542, -1.5371385, -0.4985314);
		rdVector_Set3(&XYZtoRGB.lvec, -0.9692660, 1.8760108, 0.0415560);
		rdVector_Set3(&XYZtoRGB.uvec, 0.0556434, -0.2040259, 1.0572252);

		rdVector3 XYZ;
		XYZ.x = X;
		XYZ.y = Y;
		XYZ.z = Z;

		float R = rdVector_Dot3(&XYZ, &XYZtoRGB.rvec);
		float G = rdVector_Dot3(&XYZ, &XYZtoRGB.lvec);
		float B = rdVector_Dot3(&XYZ, &XYZtoRGB.uvec);

		t = powf(t * 0.0004f, 4.0f);
		R = fmax(0.0f, R) * t;
		G = fmax(0.0f, G) * t;
		B = fmax(0.0f, B) * t;

		bbd[i].x = R;
		bbd[i].y = G;
		bbd[i].z = B;
	}

	glTexImage1D(GL_TEXTURE_1D, 0, GL_R11F_G11F_B10F, 256, 0, GL_RGB, GL_FLOAT, bbd);

    glGenVertexArrays( 1, &vao );
    glBindVertexArray( vao ); 

    menu_data_all = malloc(STD3D_MAX_UI_VERTICES * sizeof(D3DVERTEX));
    menu_data_elements = malloc(sizeof(GLushort) * 3 * STD3D_MAX_UI_TRIS);

    glGenBuffers(1, &menu_vbo_all);
    glGenBuffers(1, &menu_ibo_triangle);

	std3D_setupMenuVAO();

	// generate staging buffers
	bufferIdx = 0;
	glGenBuffers(STD3D_STAGING_COUNT, world_vbo_all);
	glGenBuffers(STD3D_STAGING_COUNT, world_ibo_triangle);
	for (int i = 0; i < STD3D_STAGING_COUNT; ++i)
	{
		glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all[i]);
		glBufferData(GL_ARRAY_BUFFER, RD_CACHE_MAX_DRAW_CALL_VERTS * sizeof(rdVertex), NULL, GL_STREAM_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, world_ibo_triangle[i]);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, RD_CACHE_MAX_DRAW_CALL_INDICES * sizeof(uint16_t), NULL, GL_STREAM_DRAW);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	std3D_setupUBOs();
	FOR_EACH_STAGE
	(
		std3D_setupDrawCallVAO(pStage);
		std3D_setupLightingUBO(pStage);
	);

	std3D_initDefaultShader();

    has_initted = true;
    return true;
}

int std3D_IsExtensionSupported(const char* name)
{
	// checking GL directly for extensions doesn't always work
	// ex. GL_EXT_shader_explicit_arithmetic_types may be supported but it doesn't get listed
	// so instead we try to compile a dummy shader with ext : require to see if it will work or not

	extern int Window_GL4;
	const char* version = Window_GL4 ? "#version 460\n" : "#version 330\n";

	char dummyCode[256];
	if (Window_GL4)
		sprintf_s(dummyCode, 256, "#extension %s : require\nvoid main() {}\n", name);
	else
		sprintf_s(dummyCode, 256, "#extension %s : require\nvoid main() {}\n", name);

	const GLchar* sources[] = {
		version,
		dummyCode
	};

	GLuint res = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(res, 2, sources, NULL);

	glCompileShader(res);

	GLint compile_ok = GL_FALSE;
	glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
	if (compile_ok == GL_FALSE)
	{
		glDeleteShader(res);
		return 0;
	}

	glDeleteShader(res);
	return 1;
}

void std3D_CheckExtensions()
{
	int major, minor;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
	stdPlatform_Printf("OpenGL Version %d.%d\n", major, minor);

	stdPlatform_Printf("Checking supported OpenGL extensions...\n");

	static const char* extList[] =
	{
		"GL_ARB_shading_language_packing",
		"GL_ARB_texture_gather",
		"GL_ARB_texture_query_lod",
		"GL_ARB_shading_language_420pack",
		"GL_ARB_explicit_uniform_location",
		"GL_ARB_texture_query_levels",
		"GL_ARB_gpu_shader5",
		"GL_KHR_shader_subgroup_ballot",
		"GL_KHR_shader_subgroup_arithmetic",
		"GL_KHR_shader_subgroup_vote",
		"GL_EXT_shader_explicit_arithmetic_types_float16",
		"GL_EXT_shader_explicit_arithmetic_types_int8",
		"GL_EXT_shader_explicit_arithmetic_types_float16",
		"GL_EXT_shader_explicit_arithmetic_types_int16",
		"GL_EXT_shader_explicit_arithmetic_types_int64",
		"GL_EXT_shader_subgroup_extended_types_float16",
		"GL_AMD_shader_trinary_minmax",
		"GL_AMD_gpu_shader_half_float_fetch",
		"GL_AMD_gcn_shader"
	};

	for (int i = 0; i < ARRAY_SIZE(extList); ++i)
	{
		if (std3D_IsExtensionSupported(extList[i]))
			stdPlatform_Printf("\tExtension %s supported\n", extList[i]);
		else
			stdPlatform_Printf("\tExtension %s not supported, using fallback\n", extList[i]);
	}
}

int std3D_Startup()
{
    if (std3D_bInitted) {
        return 1;
    }
	
#ifdef TARGET_CAN_JKGM
    jkgm_startup();
#endif

	std3D_CheckExtensions();

    memset(&std3D_ui_colormap, 0, sizeof(std3D_ui_colormap));
    rdColormap_LoadEntry("misc\\cmp\\UIColormap.cmp", &std3D_ui_colormap);

    std3D_bReinitHudElements = 1;

    std3D_bInitted = 1;
    return 1;
}

void std3D_Shutdown()
{
    if (!std3D_bInitted) {
        return;
    }

    std3D_bReinitHudElements = 0;

    rdColormap_FreeEntry(&std3D_ui_colormap);
    std3D_bInitted = 0;
}

void std3D_FreeResources()
{
    std3D_PurgeTextureCache();

    glDeleteProgram(programMenu);
	std3D_deleteExtraFramebuffers();
	std3D_deleteFramebuffer(&std3D_framebuffer);
    glDeleteTextures(1, &blank_tex);
    glDeleteTextures(1, &blank_tex_white);
    glDeleteTextures(1, &worldpal_texture);
    glDeleteTextures(1, &worldpal_lights_texture);
    glDeleteTextures(1, &displaypal_texture);
    if (blank_data)
        jkgm_aligned_free(blank_data);
    if (blank_data_white)
        jkgm_aligned_free(blank_data_white);
    if (worldpal_data)
        jkgm_aligned_free(worldpal_data);
    if (worldpal_lights_data)
        jkgm_aligned_free(worldpal_lights_data);
    if (displaypal_data)
        jkgm_aligned_free(displaypal_data);
	if (phase_data)
		jkgm_aligned_free(phase_data);

	glDeleteSamplers(4, linearSampler);
	glDeleteSamplers(4, nearestSampler);

    blank_data = NULL;
    blank_data_white = NULL;
    worldpal_data = NULL;
    worldpal_lights_data = NULL;
    displaypal_data = NULL;
	phase_data = NULL;

    if (menu_data_all)
        free(menu_data_all);
    menu_data_all = NULL;

    if (menu_data_elements)
        free(menu_data_elements);
    menu_data_elements = NULL;

    loaded_colormap = NULL;

	glDeleteTextures(1, &tiledrand_texture);
	glDeleteTextures(1, &phase_texture);
	glDeleteTextures(1, &blackbody_texture);

    glDeleteBuffers(STD3D_STAGING_COUNT, world_vbo_all);
    glDeleteBuffers(STD3D_STAGING_COUNT, world_ibo_triangle);

    glDeleteBuffers(1, &menu_vbo_all);

	FOR_EACH_STAGE
	(
		glDeleteProgram(pStage->program);
		glDeleteVertexArrays(3, pStage->vao);
	)

	glDeleteBuffers(1, &tex_ubo);
	glDeleteBuffers(1, &material_ubo);
	glDeleteBuffers(1, &shared_ubo);
	glDeleteBuffers(1, &shaderConstsUbo);
	glDeleteBuffers(1, &light_ubo);
	glDeleteBuffers(1, &occluder_ubo);
	glDeleteBuffers(1, &decal_ubo);
	glDeleteBuffers(1, &cluster_buffer);
	glDeleteTextures(1, &cluster_tbo);

	std3D_DeleteShader(defaultShaderUBO);

    std3D_bReinitHudElements = 1;

    has_initted = false;
}

int std3D_StartScene()
{
    if (Main_bHeadless) return 1;

    //printf("Begin draw\n");
    if (!has_initted)
    {
        if (!init_resources()) {
            stdPlatform_Printf("std3D: Failed to init resources, exiting...");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Failed to init resources, exiting...", NULL);
            exit(-1);
        }
    }

	std3D_PushDebugGroup("std3D_StartScene");
    
    rendered_tris = 0;
    
    std3D_swapFramebuffers();
    
    float supersample_level = stdMath_Clamp(jkPlayer_ssaaMultiple, 0.125f, 8.0f); // Can also be set lower
    int32_t tex_w = (int32_t)((float)Window_xSize * supersample_level);
    int32_t tex_h = (int32_t)((float)Window_ySize * supersample_level);
	tex_w = (tex_w < 320 ? 320 : tex_w);
	tex_h = tex_w * (float)Window_ySize / Window_xSize;

    if (tex_w != std3D_framebuffer.w || tex_h != std3D_framebuffer.h 
        || ((std3D_framebufferFlags & FBO_BLOOM) != jkPlayer_enableBloom)
		|| ((std3D_framebufferFlags & FBO_SSAO) != jkPlayer_enableSSAO)
		|| ((std3D_framebufferFlags & FBO_32_BIT) != jkPlayer_enable32Bit)
		|| std3D_framebuffer.msaaMode != jkPlayer_multiSample)
    {
		std3D_deleteExtraFramebuffers();
        std3D_deleteFramebuffer(&std3D_framebuffer);
        std3D_generateFramebuffer(tex_w, tex_h, &std3D_framebuffer);
		std3D_generateExtraFramebuffers(tex_w, tex_h);
    }

	glClearColor(0.0, 0.0, 0.0, 1.0);

	// clear the window buffer
	glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
	glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glCullFace(GL_FRONT);
    //glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);

	GLuint clearBits = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	if(jkGuiBuildMulti_bRendering)
		clearBits |= GL_COLOR_BUFFER_BIT;

#ifdef STENCIL_BUFFER
	glClearStencil(0);
	glStencilMask(0xFF);
#endif
	glClear(clearBits);

    if (jkGuiBuildMulti_bRendering && rdColormap_pCurMap && loaded_colormap != rdColormap_pCurMap)
    {
        glBindTexture(GL_TEXTURE_2D, worldpal_texture);
        memcpy(worldpal_data, rdColormap_pCurMap->colors, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);
    
        if (rdColormap_pCurMap->lightlevel)
        {
            glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
            memcpy(worldpal_lights_data, rdColormap_pCurMap->lightlevel, 0x4000);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 0x40, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
        }

        loaded_colormap = rdColormap_pCurMap;
    }
    else if (sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps && loaded_colormap != sithWorld_pCurrentWorld->colormaps)
    {
        glBindTexture(GL_TEXTURE_2D, worldpal_texture);
        memcpy(worldpal_data, sithWorld_pCurrentWorld->colormaps->colors, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);
    
        if (sithWorld_pCurrentWorld->colormaps->lightlevel)
        {
            glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
            memcpy(worldpal_lights_data, sithWorld_pCurrentWorld->colormaps->lightlevel, 0x4000);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 0x40, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
        }

        loaded_colormap = sithWorld_pCurrentWorld->colormaps;
    }

    if (memcmp(displaypal_data, stdDisplay_masterPalette, 0x300))
    {
        glBindTexture(GL_TEXTURE_2D, displaypal_texture);
        memcpy(displaypal_data, stdDisplay_masterPalette, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, displaypal_data);
    }

	//std3D_ResetDrawCalls();

	void std3D_UpdateSharedUniforms();
	std3D_UpdateSharedUniforms();

	void std3D_setupWorldTextures();
	std3D_setupWorldTextures();
	std3D_bindTexture(GL_TEXTURE_2D, refrZ.tex, TEX_SLOT_CLIP);

	glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
	GLenum bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1
	#ifdef MOTION_BLUR
		, GL_COLOR_ATTACHMENT2
	#endif
	};
	glDrawBuffers(ARRAYSIZE(bufs), bufs);

	std3D_bindTexture(GL_TEXTURE_2D, ssao.tex, TEX_SLOT_AO);

	glViewport(0, 0, std3D_framebuffer.w, std3D_framebuffer.h);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glCullFace(GL_BACK);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_SCISSOR_TEST);

	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_LIGHTS, light_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_OCCLUDERS, occluder_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_DECALS, decal_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_SHARED, shared_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_FOG, fog_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_TEX, tex_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_MATERIAL, material_ubo);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_SHADER, defaultShaderUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_SHADER_CONSTS, shaderConstsUbo);
}

int std3D_EndScene()
{
    if (Main_bHeadless) {
        last_tex = NULL;
        last_flags = 0;
        return 1;
    }

	std3D_PopDebugGroup();

	std3D_ResetState();

    last_tex = NULL;
    last_flags = 0;
    return 1;
}

void std3D_ResetState()
{
	glBindVertexArray(vao);

	glBindTexture(GL_TEXTURE_2D, worldpal_texture);

	//glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glDepthFunc(GL_ALWAYS);
	glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_SCISSOR_TEST);
	glCullFace(GL_FRONT);
	glBindSampler(0, 0);
	glBindSampler(TEX_SLOT_DIFFUSE, 0);
	glBindSampler(TEX_SLOT_EMISSIVE, 0);
	glBindSampler(TEX_SLOT_DISPLACEMENT, 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

}

void std3D_ResetUIRenderList()
{
    rendered_tris += GL_tmpUITrisAmt;

    GL_tmpUIVerticesAmt = 0;
    GL_tmpUITrisAmt = 0;
    //GL_tmpLinesAmt = 0;
    
    //memset(GL_tmpTris, 0, sizeof(GL_tmpTris));
    //memset(GL_tmpVertices, 0, sizeof(GL_tmpVertices));
}


int std3D_RenderListVerticesFinish()
{
    return 1;
}

void std3D_DrawMenuSubrect(float x, float y, float w, float h, float dstX, float dstY, float scale)
{
    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = Video_menuBuffer.format.width;
    double tex_h = Video_menuBuffer.format.height;

    float w_dst = w;
    float h_dst = h;

    if (scale == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scale = 1.0;
    }

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    GL_tmpVertices[GL_tmpVerticesAmt+0].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+0].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+0].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+1].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+1].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+1].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+2].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+3].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+3].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+3].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nz = 0;
    
    GL_tmpTris[GL_tmpTrisAmt+0].v1 = GL_tmpVerticesAmt+1;
    GL_tmpTris[GL_tmpTrisAmt+0].v2 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+0].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpTris[GL_tmpTrisAmt+1].v1 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+1].v2 = GL_tmpVerticesAmt+3;
    GL_tmpTris[GL_tmpTrisAmt+1].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpVerticesAmt += 4;
    GL_tmpTrisAmt += 2;
}

void std3D_DrawMenuSubrect2(float x, float y, float w, float h, float dstX, float dstY, float scale)
{
    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = Video_menuBuffer.format.width;
    double tex_h = Video_menuBuffer.format.height;

    float w_dst = w;
    float h_dst = h;

    if (scale == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scale = 1.0;
    }

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    GL_tmpVertices[GL_tmpVerticesAmt+0].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+0].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+0].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+1].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+1].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+1].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+2].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+3].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+3].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+3].z = 0.5f;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nz = 0;
    
    GL_tmpTris[GL_tmpTrisAmt+0].v1 = GL_tmpVerticesAmt+1;
    GL_tmpTris[GL_tmpTrisAmt+0].v2 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+0].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpTris[GL_tmpTrisAmt+1].v1 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+1].v2 = GL_tmpVerticesAmt+3;
    GL_tmpTris[GL_tmpTrisAmt+1].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpVerticesAmt += 4;
    GL_tmpTrisAmt += 2;
}

static rdDDrawSurface* test_idk = NULL;
void std3D_DrawSimpleTex(std3DSimpleTexStage* pStage, std3DIntermediateFbo* pFbo, GLuint texId, GLuint texId2, GLuint texId3, float param1, float param2, float param3, int gen_mips, const char* debugName);
void std3D_DrawMapOverlay();
void std3D_DrawUIRenderList();

void std3D_ResolveMSAA()
{
	if (std3D_framebuffer.samples)
	{
		std3D_PushDebugGroup("std3D_ResolveMSAA");


		if (std3D_framebuffer.downscale)
		{
			//std3D_DrawSimpleTex(&std3D_resolveStage, &std3D_mainFbo, std3D_framebuffer.tex0, std3D_framebuffer.tex1, 0, 0.0, 0.0, 0.0, 0, "MSAA Resolve");
		
			glViewport(0, 0, std3D_framebuffer.w, std3D_framebuffer.h);
			glDisable(GL_SCISSOR_TEST);
			
			glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.resolveFbo);
			glDepthFunc(GL_ALWAYS);
			glDisable(GL_CULL_FACE);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glUseProgram(std3D_resolveStage.program);

			GLenum bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1
#ifdef MOTION_BLUR
	, GL_COLOR_ATTACHMENT2
#endif
			};
			glDrawBuffers(ARRAYSIZE(bufs), bufs);

			glBindVertexArray(vao);

			glActiveTexture(GL_TEXTURE0 + 0);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, std3D_framebuffer.tex0);

			glActiveTexture(GL_TEXTURE0 + 1);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, std3D_framebuffer.tex1 ? std3D_framebuffer.tex1 : blank_tex);

			glUniform1i(std3D_resolveStage.uniform_tex, 0);
			glUniform1i(std3D_resolveStage.uniform_tex2, 1);

			glUniform1i(std3D_resolveStage.uniform_param1, (std3D_framebuffer.msaaMode == SAMPLE_2x1) ?    0 :    2);
			glUniform1f(std3D_resolveStage.uniform_param2, (std3D_framebuffer.msaaMode == SAMPLE_2x1) ? 1.0f : 0.5f);

			glDrawArrays(GL_TRIANGLES, 0, 3);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		else
		{
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, std3D_framebuffer.resolveFbo);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, std3D_framebuffer.fbo);

#ifdef MOTION_BLUR
			const uint8_t attachments = 3;
#else
			const uint8_t attachments = (std3D_framebufferFlags & FBO_BLOOM) ? 2 : 1;
#endif
			for (uint8_t i = 0; i < attachments; ++i)
			{
				glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
				glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
				glBlitFramebuffer(0, 0, std3D_framebuffer.w, std3D_framebuffer.h, 0, 0, std3D_framebuffer.w, std3D_framebuffer.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			}

			glDrawBuffer(GL_DEPTH_STENCIL_ATTACHMENT);
			glReadBuffer(GL_DEPTH_STENCIL_ATTACHMENT);
			glBlitFramebuffer(0, 0, std3D_framebuffer.w, std3D_framebuffer.h, 0, 0, std3D_framebuffer.w, std3D_framebuffer.h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
		std3D_PopDebugGroup();
	}
}

void std3D_DrawMenu()
{
    if (Main_bHeadless) return;

	std3D_PushDebugGroup("std3D_DrawMenu");

    //printf("Draw menu\n");
 //   std3D_DrawSceneFbo();
    //glFlush();

	glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
	if (!jkGame_isDDraw)// || jkGuiBuildMulti_bRendering)
	{
		//glDisable(GL_DEPTH_TEST);
		//glClear(GL_COLOR_BUFFER_BIT);
	}

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
	glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
	glClear(GL_DEPTH_BUFFER_BIT);
	glUseProgram(programMenu);
    
    float menu_w, menu_h, menu_u, menu_v, menu_x;
    menu_w = (double)Window_xSize;
    menu_h = (double)Window_ySize;
    menu_u = 1.0;
    menu_v = 1.0;
    menu_x = 0.0;
    
    int bFixHudScale = 0;

    double fake_windowW = (double)Window_xSize;
    double fake_windowH = (double)Window_ySize;

    if (!jkGame_isDDraw && !jkGuiBuildMulti_bRendering && !jkCutscene_isRendering)
    {
        //menu_w = 640.0;
        //menu_h = 480.0;

        // Stretch screen
        menu_u = (1.0 / Video_menuBuffer.format.width) * 640.0;
        menu_v = (1.0 / Video_menuBuffer.format.height) * 480.0;

        // Keep 4:3 aspect
        menu_x = (menu_w - (menu_h * (640.0 / 480.0))) / 2.0;
        menu_w = (menu_h * (640.0 / 480.0));
    }
    else if (jkCutscene_isRendering) {
        bFixHudScale = 1;

        //menu_w = 640.0;
        //menu_h = 480.0;

        menu_w = Video_menuBuffer.format.width;
        menu_h = Video_menuBuffer.format.height;

        // For ultrawide screens, limit the width to 16:9
        if (Window_xSize > Window_ySize && ((double)Window_xSize / (double)Window_ySize) > (Main_bMotsCompat ? (16.0/9.0) : (21.0/9.0))) {
            fake_windowW = fake_windowH * (16.0/9.0);
        }

        // Keep 4:3 aspect
        menu_x = (menu_w - (menu_h * (640.0 / 480.0))) / 2.0;

    }
    else if (jkGuiBuildMulti_bRendering)
    {
        bFixHudScale = 1;

        // Stretch screen
        menu_u = (1.0 / Video_menuBuffer.format.width) * 640.0;
        menu_v = (1.0 / Video_menuBuffer.format.height) * 480.0;

        // Keep 4:3 aspect
        menu_x = (menu_w - (menu_h * (640.0 / 480.0))) / 2.0;
        menu_w = (menu_h * (640.0 / 480.0));
    }
    else
    {
        bFixHudScale = 0;

        menu_w = Video_menuBuffer.format.width;
        menu_h = Video_menuBuffer.format.height;
    }

    if (!bFixHudScale)
    {
//		float padScale = menu_x / Window_xSize ;// * 0.5;
//
//		// left
//		GL_tmpVertices[0].x = 0.0;
//		GL_tmpVertices[0].y = 0.0;
//		GL_tmpVertices[0].z = 0.0;
//		GL_tmpVertices[0].tu = menu_u * padScale;
//		GL_tmpVertices[0].tv = 0.0;
//		*(uint32_t*)&GL_tmpVertices[0].nx = 0;
//		GL_tmpVertices[0].color = 0xFF3F3F3F;
//		*(uint32_t*)&GL_tmpVertices[0].nz = 0;
//		GL_tmpVertices[0].lightLevel = 1.0f;
//
//		GL_tmpVertices[1].x = 0.0;
//		GL_tmpVertices[1].y = menu_h;
//		GL_tmpVertices[1].z = 0.0;
//		GL_tmpVertices[1].tu = menu_u * padScale;
//		GL_tmpVertices[1].tv = menu_v;
//		*(uint32_t*)&GL_tmpVertices[1].nx = 0;
//		GL_tmpVertices[1].color = 0xFF3F3F3F;
//		*(uint32_t*)&GL_tmpVertices[1].nz = 0;
//		GL_tmpVertices[1].lightLevel = 1.0f;
//
//		GL_tmpVertices[2].x = menu_x;
//		GL_tmpVertices[2].y = menu_h;
//		GL_tmpVertices[2].z = 0.0;
//		GL_tmpVertices[2].tu = 0.0;
//		GL_tmpVertices[2].tv = menu_v;
//		*(uint32_t*)&GL_tmpVertices[2].nx = 0;
//		GL_tmpVertices[2].color = 0xFFFFFFFF;
//		*(uint32_t*)&GL_tmpVertices[2].nz = 0;
//		GL_tmpVertices[2].lightLevel = 0.0f;
//
//		GL_tmpVertices[3].x = menu_x;
//		GL_tmpVertices[3].y = 0.0;
//		GL_tmpVertices[3].z = 0.0;
//		GL_tmpVertices[3].tu = 0.0;
//		GL_tmpVertices[3].tv = 0.0;
//		*(uint32_t*)&GL_tmpVertices[3].nx = 0;
//		GL_tmpVertices[3].color = 0xFFFFFFFF;
//		*(uint32_t*)&GL_tmpVertices[3].nz = 0;
//		GL_tmpVertices[3].lightLevel = 0.0f;
//
//		GL_tmpTris[0].v1 = 1;
//		GL_tmpTris[0].v2 = 0;
//		GL_tmpTris[0].v3 = 2;
//
//		GL_tmpTris[1].v1 = 0;
//		GL_tmpTris[1].v2 = 3;
//		GL_tmpTris[1].v3 = 2;
//
//		// center
//
//        GL_tmpVertices[4].x = menu_x;
//        GL_tmpVertices[4].y = 0.0;
//        GL_tmpVertices[4].z = 0.0;
//        GL_tmpVertices[4].tu = 0.0;
//        GL_tmpVertices[4].tv = 0.0;
//        *(uint32_t*)&GL_tmpVertices[4].nx = 0;
//        GL_tmpVertices[4].color = 0xFFFFFFFF;
//        *(uint32_t*)&GL_tmpVertices[4].nz = 0;
//		GL_tmpVertices[4].lightLevel = 0.0f;
//
//        GL_tmpVertices[5].x = menu_x;
//        GL_tmpVertices[5].y = menu_h;
//        GL_tmpVertices[5].z = 0.0;
//        GL_tmpVertices[5].tu = 0.0;
//        GL_tmpVertices[5].tv = menu_v;
//        *(uint32_t*)&GL_tmpVertices[5].nx = 0;
//        GL_tmpVertices[5].color = 0xFFFFFFFF;
//        *(uint32_t*)&GL_tmpVertices[5].nz = 0;
//		GL_tmpVertices[5].lightLevel = 0.0f;
//
//        GL_tmpVertices[6].x = menu_x + menu_w;
//        GL_tmpVertices[6].y = menu_h;
//        GL_tmpVertices[6].z = 0.0;
//        GL_tmpVertices[6].tu = menu_u;
//        GL_tmpVertices[6].tv = menu_v;
//        *(uint32_t*)&GL_tmpVertices[6].nx = 0;
//        GL_tmpVertices[6].color = 0xFFFFFFFF;
//        *(uint32_t*)&GL_tmpVertices[6].nz = 0;
//		GL_tmpVertices[6].lightLevel = 0.0f;
//
//        GL_tmpVertices[7].x = menu_x + menu_w;
//        GL_tmpVertices[7].y = 0.0;
//        GL_tmpVertices[7].z = 0.0;
//        GL_tmpVertices[7].tu = menu_u;
//        GL_tmpVertices[7].tv = 0.0;
//        *(uint32_t*)&GL_tmpVertices[7].nx = 0;
//        GL_tmpVertices[7].color = 0xFFFFFFFF;
//        *(uint32_t*)&GL_tmpVertices[7].nz = 0;
//		GL_tmpVertices[7].lightLevel = 0.0f;
//
//        GL_tmpTris[2].v1 = 5;
//        GL_tmpTris[2].v2 = 4;
//        GL_tmpTris[2].v3 = 6;
//        
//        GL_tmpTris[3].v1 = 4;
//        GL_tmpTris[3].v2 = 7;
//        GL_tmpTris[3].v3 = 6;
//
//		// right
//		GL_tmpVertices[8].x = menu_x + menu_w;
//		GL_tmpVertices[8].y = 0.0;
//		GL_tmpVertices[8].z = 0.0;
//		GL_tmpVertices[8].tu = menu_u;
//		GL_tmpVertices[8].tv = 0.0;
//		*(uint32_t*)&GL_tmpVertices[8].nx = 0;
//		GL_tmpVertices[8].color = 0xFFFFFFFF;
//		*(uint32_t*)&GL_tmpVertices[8].nz = 0;
//		GL_tmpVertices[8].lightLevel = 0.0f;
//
//		GL_tmpVertices[9].x = menu_x + menu_w;
//		GL_tmpVertices[9].y = menu_h;
//		GL_tmpVertices[9].z = 0.0;
//		GL_tmpVertices[9].tu = menu_u;
//		GL_tmpVertices[9].tv = menu_v;
//		*(uint32_t*)&GL_tmpVertices[9].nx = 0;
//		GL_tmpVertices[9].color = 0xFFFFFFFF;
//		*(uint32_t*)&GL_tmpVertices[9].nz = 0;
//		GL_tmpVertices[9].lightLevel = 0.0f;
//
//		GL_tmpVertices[10].x = Window_xSize;
//		GL_tmpVertices[10].y = menu_h;
//		GL_tmpVertices[10].z = 0.0;
//		GL_tmpVertices[10].tu = menu_u - menu_u * padScale;
//		GL_tmpVertices[10].tv = menu_v;
//		*(uint32_t*)&GL_tmpVertices[10].nx = 0;
//		GL_tmpVertices[10].color = 0xFF3F3F3F;
//		*(uint32_t*)&GL_tmpVertices[10].nz = 0;
//		GL_tmpVertices[10].lightLevel = 1.0f;
//
//		GL_tmpVertices[11].x = Window_xSize;
//		GL_tmpVertices[11].y = 0.0;
//		GL_tmpVertices[11].z = 0.0;
//		GL_tmpVertices[11].tu = menu_u - menu_u * padScale;
//		GL_tmpVertices[11].tv = 0.0;
//		*(uint32_t*)&GL_tmpVertices[11].nx = 0;
//		GL_tmpVertices[11].color = 0xFF3F3F3F;
//		*(uint32_t*)&GL_tmpVertices[11].nz = 0;
//		GL_tmpVertices[11].lightLevel = 1.0f;
//
//		GL_tmpTris[4].v1 = 9;
//		GL_tmpTris[4].v2 = 8;
//		GL_tmpTris[4].v3 = 10;
//
//		GL_tmpTris[5].v1 = 8;
//		GL_tmpTris[5].v2 = 11;
//		GL_tmpTris[5].v3 = 10;
//        
//        GL_tmpVerticesAmt = 12;
//        GL_tmpTrisAmt = 6;

		 GL_tmpVertices[0].x = menu_x;
        GL_tmpVertices[0].y = 0.0;
        GL_tmpVertices[0].z = 0.5f;
        GL_tmpVertices[0].tu = 0.0;
        GL_tmpVertices[0].tv = 0.0;
        *(uint32_t*)&GL_tmpVertices[0].nx = 0;
        GL_tmpVertices[0].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[0].nz = 0;
        
        GL_tmpVertices[1].x = menu_x;
        GL_tmpVertices[1].y = menu_h;
        GL_tmpVertices[1].z = 0.5f;
        GL_tmpVertices[1].tu = 0.0;
        GL_tmpVertices[1].tv = menu_v;
        *(uint32_t*)&GL_tmpVertices[1].nx = 0;
        GL_tmpVertices[1].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[1].nz = 0;
        
        GL_tmpVertices[2].x = menu_x + menu_w;
        GL_tmpVertices[2].y = menu_h;
        GL_tmpVertices[2].z = 0.5f;
        GL_tmpVertices[2].tu = menu_u;
        GL_tmpVertices[2].tv = menu_v;
        *(uint32_t*)&GL_tmpVertices[2].nx = 0;
        GL_tmpVertices[2].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[2].nz = 0;
        
        GL_tmpVertices[3].x = menu_x + menu_w;
        GL_tmpVertices[3].y = 0.0;
        GL_tmpVertices[3].z = 0.5f;
        GL_tmpVertices[3].tu = menu_u;
        GL_tmpVertices[3].tv = 0.0;
        *(uint32_t*)&GL_tmpVertices[3].nx = 0;
        GL_tmpVertices[3].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[3].nz = 0;
        
        GL_tmpTris[0].v1 = 1;
        GL_tmpTris[0].v2 = 0;
        GL_tmpTris[0].v3 = 2;
        
        GL_tmpTris[1].v1 = 0;
        GL_tmpTris[1].v2 = 3;
        GL_tmpTris[1].v3 = 2;
        
        GL_tmpVerticesAmt = 4;
        GL_tmpTrisAmt = 2;
    }
    else if (jkGuiBuildMulti_bRendering)
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        // Main View
        std3D_DrawMenuSubrect(0, 0, 640, 480, menu_x, 0, menu_w/640.0);
    }
    else if (jkCutscene_isRendering)
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);

        int video_height = Main_bMotsCompat ? 350 : 300;
        int subs_y = 350;
        int subs_h = 130;
        if (Main_bMotsCompat) {
            subs_y = 400;
            subs_h = 80;
        }

        float partial_menu_w = (menu_h * (640.0 / 480.0));
        float upscale = fake_windowW/640.0;
        float upscale2 = (fake_windowH - (50 + video_height * upscale))/((double)subs_h);
        float upscale3 = 1.0;//Window_ySize/480.0;

        if (upscale2 < 1.0) {
            upscale2 = 1.0;
            if (fake_windowH > 480.0) {
                upscale2 = 2.0;
            }
        }
        if (upscale2 > upscale) {
            upscale2 = upscale;
        }

        float shift_y = ((double)Window_ySize - fake_windowH) / 2.0;
        float shift_x = ((double)Window_xSize - fake_windowW) / 2.0;

        float sub_width = 640*upscale2;
        float sub_x = (fake_windowW - sub_width) / 2.0;

        float pause_width = 640*upscale3;
        float pause_x = (fake_windowW - pause_width) / 2.0;

        //printf("%f %f, %f %f %f, %d %d\n", sub_x, pause_x, upscale, upscale2, upscale3, Window_xSize, Window_ySize);

        // Main View
        std3D_DrawMenuSubrect(0, 50, 640, video_height, shift_x + 0, shift_y + 50, upscale);

        // Subtitles
        if (jkCutscene_dword_55B750) {
            

            // Some monitors might not have a bottom black bar, so draw an outline
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2) - 2, upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2) + 2, upscale2);

            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2) -2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2) +2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2) - 2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2) + 2, upscale2);

            std3D_DrawMenuSubrect(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
        }

        // Paused
        std3D_DrawMenuSubrect(0, 10, 640, 40, shift_x + pause_x, shift_y + 0*upscale, upscale3);
    }
    else
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        // Main View
        std3D_DrawMenuSubrect(0, 128, menu_w, menu_h-256, 0, 128, 0.0);

        float hudScale = Window_ySize / 480.0;

        /*if (menu_w >= 3600)
            hudScale = 4;
        else if (menu_w >= 1800)
            hudScale = 3;
        else if (menu_w >= 1200)
            hudScale = 2;*/

        // Left and Right HUD
        std3D_DrawMenuSubrect(0, menu_h - 64, 64, 64, 0, Window_ySize - 64*hudScale, hudScale);
        std3D_DrawMenuSubrect(menu_w - 64, menu_h - 64, 64, 64, Window_xSize - 64*hudScale, Window_ySize - 64*hudScale, hudScale);

        // Items
        std3D_DrawMenuSubrect((menu_w / 2) - 128, menu_h - 64, 256, 64, (Window_xSize / 2) - (128*hudScale), Window_ySize - 64*hudScale, hudScale);

        // Text
        float textScale = hudScale;
        if (jkDev_BMFontHeight > 11) {
            textScale *= 11.0 / (float)jkDev_BMFontHeight;
        }
        float textWidth = menu_w - (48*2);
        float textHeight = jkDev_BMFontHeight * 5.5;
        float destTextWidth = textWidth * textScale;
        std3D_DrawMenuSubrect(48, 0, menu_w - (48*2), textHeight, (Window_xSize / 2) - (destTextWidth / 2), 0, textScale);

        // Active forcepowers/items
        std3D_DrawMenuSubrect(menu_w - 48, 0, 48, 128, Window_xSize - (48*hudScale), 0, hudScale);
    }

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    glActiveTexture(GL_TEXTURE0 + 0);
#ifdef HW_VBUFFER
    glBindTexture(GL_TEXTURE_2D, Video_menuBuffer.device_surface->tex);
#else   
	glBindTexture(GL_TEXTURE_2D,  Video_menuTexId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Video_menuBuffer.format.width, Video_menuBuffer.format.height, GL_RED, GL_UNSIGNED_BYTE, Video_menuBuffer.sdlSurface->pixels);
#endif

    //GLushort data_elements[32 * 3];
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);

    glActiveTexture(GL_TEXTURE0 + 0);
    glUniform1i(programMenu_uniform_tex, 0);
    glUniform1i(programMenu_uniform_displayPalette, 1);

    D3DVERTEX* vertexes = GL_tmpVertices;

	glBindVertexArray(menu_vao);
	glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
	glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);

    {

    float maxX, maxY, scaleX, scaleY, width, height;

    scaleX = 1.0/((double)Window_xSize / 2.0);
    scaleY = 1.0/((double)Window_ySize / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = Window_xSize;
    height = Window_ySize;
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(programMenu_uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);

    }
    
    rdTri* tris = GL_tmpTris;
    
    rdDDrawSurface* last_tex = (void*)-1;
    int last_tex_idx = 0;
    //GLushort* data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int tris_size = 0;  
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

    std3D_DrawMapOverlay();
    std3D_DrawUIRenderList();

	glBindVertexArray(vao);

    last_flags = 0;

	std3D_PopDebugGroup();
}

void std3D_DrawMapOverlay()
{
    if (Main_bHeadless) return;

    //glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
	glUseProgram(programMenu);
    
    float menu_w = (double)Window_xSize;
    float menu_h = (double)Window_ySize;

    if (!jkGame_isDDraw)
    {
        return;
    }
	
	std3D_PushDebugGroup("std3D_DrawMapOverlay");

    menu_w = Video_menuBuffer.format.width;
    menu_h = Video_menuBuffer.format.height;

    GL_tmpVerticesAmt = 0;
    GL_tmpTrisAmt = 0;

    // Main View
    std3D_DrawMenuSubrect(0, 0, menu_w, menu_h, 0, 0, 0.0);

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, Video_overlayTexId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Video_overlayMapBuffer.format.width, Video_overlayMapBuffer.format.height, GL_RED, GL_UNSIGNED_BYTE, Video_overlayMapBuffer.sdlSurface->pixels);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);

    glActiveTexture(GL_TEXTURE0 + 0);
    glUniform1i(programMenu_uniform_tex, 0);
    glUniform1i(programMenu_uniform_displayPalette, 1);

    D3DVERTEX* vertexes = GL_tmpVertices;
	glBindVertexArray(menu_vao);
	glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
	glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);
    
	{

    float maxX, maxY, scaleX, scaleY, width, height;

    scaleX = 1.0/((double)Window_xSize / 2.0);
    scaleY = 1.0/((double)Window_ySize / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = Window_xSize;
    height = Window_ySize;
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(programMenu_uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);

    }
    
    rdTri* tris = GL_tmpTris;
    
    rdDDrawSurface* last_tex = (void*)-1;
    int last_tex_idx = 0;
    //GLushort* data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int tris_size = 0;  
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

	glBindVertexArray(vao);

	std3D_PopDebugGroup();
}

void std3D_DrawUIBitmapRGBAZ(stdBitmap* pBmp, int mipIdx, float dstX, float dstY, rdRect* srcRect, float scaleX, float scaleY, int bAlphaOverwrite, uint8_t color_r, uint8_t color_g, uint8_t color_b, uint8_t color_a, float depth)
{
    float internalWidth = Video_menuBuffer.format.width;
    float internalHeight = Video_menuBuffer.format.height;

    if (!pBmp) return;
    if (!pBmp->abLoadedToGPU[mipIdx]) {
        std3D_AddBitmapToTextureCache(pBmp, mipIdx, !(pBmp->palFmt & 1), 0);
    }

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    double scaleX_ = (double)Window_xSize/(double)internalWidth;
    double scaleY_ = (double)Window_ySize/(double)internalHeight;

    dstX *= scaleX_;
    dstY *= scaleY_;

    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = pBmp->mipSurfaces[0]->format.width;
    double tex_h = pBmp->mipSurfaces[0]->format.height;

    double w = tex_w;
    double h = tex_h;
    double x = 0;
    double y = 0;

    if (srcRect) {
        x = srcRect->x;
        y = srcRect->y;
        w = srcRect->width;
        h = srcRect->height;
    }

    float w_dst = w;
    float h_dst = h;

    if (scaleX == 0.0 && scaleY == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scaleX = 1.0;
        scaleY = 1.0;
    }

    double dstScaleX = scaleX;
    double dstScaleY = scaleY;
    dstScaleX *= scaleX_;
    dstScaleY *= scaleY_;

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    uint32_t color = 0;

    color |= (color_r << 0);
    color |= (color_g << 8);
    color |= (color_b << 16);
    color |= (color_a << 24);

    if (GL_tmpUIVerticesAmt + 4 > STD3D_MAX_UI_VERTICES) {
        return;
    }
    if (GL_tmpUITrisAmt + 2 > STD3D_MAX_UI_TRIS) {
        return;
    }

    if (dstY + (dstScaleY * h_dst) < 0.0 || dstX + (dstScaleX * w_dst) < 0.0) {
        return;
    }
    if (dstY > Window_ySize || dstX > Window_xSize) {
        return;
    }

    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].z = depth;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].z = depth;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].z = depth;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].z = depth;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nz = 0;
    
    GL_tmpUITris[GL_tmpUITrisAmt+0].v1 = GL_tmpUIVerticesAmt+1;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v2 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+0].flags = bAlphaOverwrite;
    GL_tmpUITris[GL_tmpUITrisAmt+0].texture = pBmp->aTextureIds[mipIdx];
    GL_tmpUITris[GL_tmpUITrisAmt+0].bm = pBmp;

    GL_tmpUITris[GL_tmpUITrisAmt+1].v1 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v2 = GL_tmpUIVerticesAmt+3;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+1].flags = bAlphaOverwrite;
    GL_tmpUITris[GL_tmpUITrisAmt+1].texture = pBmp->aTextureIds[mipIdx];
  	GL_tmpUITris[GL_tmpUITrisAmt+1].bm = pBmp;
  
    GL_tmpUIVerticesAmt += 4;
    GL_tmpUITrisAmt += 2;
}

void std3D_DrawUIBitmapRGBA(stdBitmap* pBmp, int mipIdx, float dstX, float dstY, rdRect* srcRect, float scaleX, float scaleY, int bAlphaOverwrite, uint8_t color_r, uint8_t color_g, uint8_t color_b, uint8_t color_a)
{
	std3D_DrawUIBitmapRGBAZ(pBmp, mipIdx, dstX, dstY, srcRect, scaleX, scaleY, bAlphaOverwrite, color_r, color_g, color_b, color_a, 0.5f);
}

void std3D_DrawUIBitmap(stdBitmap* pBmp, int mipIdx, float dstX, float dstY, rdRect* srcRect, float scale, int bAlphaOverwrite)
{
    std3D_DrawUIBitmapRGBA(pBmp, mipIdx, dstX, dstY, srcRect, scale, scale, bAlphaOverwrite, 0xFF, 0xFF, 0xFF, 0xFF);
}

void std3D_DrawUIBitmapZ(stdBitmap* pBmp, int mipIdx, float dstX, float dstY, rdRect* srcRect, float scaleX, float scaleY, int bAlphaOverwrite, float depth)
{
	std3D_DrawUIBitmapRGBAZ(pBmp, mipIdx, dstX, dstY, srcRect, scaleX, scaleY, bAlphaOverwrite, 0xFF, 0xFF, 0xFF, 0xFF, depth);
}

void std3D_DrawUIClearedRect(uint8_t palIdx, rdRect* dstRect)
{
    if (!displaypal_data) return;
    uint32_t color = 0;
    uint8_t color_r = ((uint8_t*)displaypal_data)[(palIdx*3) + 0];
    uint8_t color_g = ((uint8_t*)displaypal_data)[(palIdx*3) + 1];
    uint8_t color_b = ((uint8_t*)displaypal_data)[(palIdx*3) + 2];

    std3D_DrawUIClearedRectRGBA(color_r, color_g, color_b, 0xFF, dstRect);
}

void std3D_DrawUIClearedRectRGBA(uint8_t color_r, uint8_t color_g, uint8_t color_b, uint8_t color_a, rdRect* dstRect)
{
    if (!has_initted) return;
    if (!dstRect) return;
    double dstX = dstRect->x;
    double dstY = dstRect->y;

    float internalWidth = Video_menuBuffer.format.width;
    float internalHeight = Video_menuBuffer.format.height;
    if (!internalWidth || !internalHeight) return;

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    double scaleX = (double)Window_xSize/(double)internalWidth;
    double scaleY = (double)Window_ySize/(double)internalHeight;

    dstX *= scaleX;
    dstY *= scaleY;

    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = dstRect->width;
    double tex_h = dstRect->height;
    if (!tex_w || !tex_h) return;

    double w = tex_w;
    double h = tex_h;
    double x = 0;
    double y = 0;

    float w_dst = w;
    float h_dst = h;
    double scale = 1.0;

    double dstScaleX = scale;
    double dstScaleY = scale;
    dstScaleX *= scaleX;
    dstScaleY *= scaleY;

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    uint32_t color = 0;

    color |= (color_r << 0);
    color |= (color_g << 8);
    color |= (color_b << 16);
    color |= (color_a << 24);
    if (GL_tmpUIVerticesAmt + 4 > STD3D_MAX_UI_VERTICES) {
        return;
    }
    if (GL_tmpUITrisAmt + 2 > STD3D_MAX_UI_TRIS) {
        return;
        return;
    }

    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nz = 0;
    
    GL_tmpUITris[GL_tmpUITrisAmt+0].v1 = GL_tmpUIVerticesAmt+1;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v2 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+0].flags = 0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].texture = blank_tex_white;
	GL_tmpUITris[GL_tmpUITrisAmt+0].bm = 0;

    GL_tmpUITris[GL_tmpUITrisAmt+1].v1 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v2 = GL_tmpUIVerticesAmt+3;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+1].flags = 0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].texture = blank_tex_white;
	GL_tmpUITris[GL_tmpUITrisAmt+1].bm = 0;

    GL_tmpUIVerticesAmt += 4;
    GL_tmpUITrisAmt += 2;
}

void std3D_DrawUIRenderList()
{
    if (Main_bHeadless) return;
    if (!GL_tmpUITrisAmt) return;

    //glFlush();

    //printf("Draw render list\n");
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);//ALWAYS);
	glUseProgram(std3D_uiProgram.program); // TODO: simpler shader
    
    last_ui_tex = 0;
    last_ui_flags = -1;

    // Generate vertices list
    D3DVERTEX* vertexes = GL_tmpUIVertices;

    float maxX, maxY, scaleX, scaleY, width, height;

    float internalWidth = Window_xSize;//Video_menuBuffer.format.width;
    float internalHeight = Window_ySize;//Video_menuBuffer.format.height;

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    maxX = 1.0;
    maxY = 1.0;
    scaleX = 1.0/((double)internalWidth / 2.0);
    scaleY = 1.0/((double)internalHeight / 2.0);
    width = Window_xSize;
    height = Window_ySize;

    if (jkGuiBuildMulti_bRendering) {
        width = 640;
        height = 480;
    }

    // JKDF2's vertical FOV is fixed with their projection, for whatever reason. 
    // This ends up resulting in the view looking squished vertically at wide/ultrawide aspect ratios.
    // To compensate, we zoom the y axis here.
    // I also went ahead and fixed vertical displays in the same way because it seems to look better.
    float zoom_yaspect = 1.0;
    float zoom_xaspect = 1.0;
    
    float shift_add_x = 0;
    float shift_add_y = 0;
    
    glUniform1i(std3D_uiProgram.uniform_tex, 0);
    glUniform1i(std3D_uiProgram.uniform_tex2, 1);
    glUniform1i(std3D_uiProgram.uniform_tex3, 2);

    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    
    {
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(std3D_uiProgram.uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);
    glUniform2f(std3D_uiProgram.uniform_iResolution, internalWidth, internalHeight);

	glUniform1f(std3D_uiProgram.uniform_iTime, (float)stdPlatform_GetTimeSinceStart() / 1000.0f);

	float param1 = 1.0;
    glUniform1f(std3D_uiProgram.uniform_param1, param1);
    glUniform1f(std3D_uiProgram.uniform_param3, 1.0f/jkPlayer_gamma);
    
    }

    rdUITri* tris = GL_tmpUITris;
    
	glBindVertexArray(menu_vao);
	glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
	glBufferData(GL_ARRAY_BUFFER, GL_tmpUIVerticesAmt * sizeof(D3DVERTEX), GL_tmpUIVertices, GL_STREAM_DRAW);
  
    int last_flags = 0;
    int last_tex_idx = 0;
    //GLushort* menu_data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpUITrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpUITrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);
    
    int do_batch = 0;

    int tex_id = tris[0].texture;
    glActiveTexture(GL_TEXTURE0 + 0);
    if (tex_id == 0)
        glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    else
        glBindTexture(GL_TEXTURE_2D, tex_id);

	float param2 = tris[0].bm && tris[0].bm->useLights ? 1.0 : 0.0;
	glUniform1f(std3D_uiProgram.uniform_param2, param2);

    if (tris[0].flags) {
        glUniform1f(std3D_uiProgram.uniform_param1, 1.0);
    }
    else {
        glUniform1f(std3D_uiProgram.uniform_param1, 0.0);
    }
    
    for (int j = 0; j < GL_tmpUITrisAmt; j++)
    {
        if (tris[j].texture != last_ui_tex || tris[j].flags != last_ui_flags)
        {
            do_batch = 1;
        }
        
        if (do_batch)
        {
            int num_tris_batch = j - last_tex_idx;

            if (num_tris_batch)
            {
                //printf("batch %u~%u\n", last_tex_idx, j);
                glDrawElements(GL_TRIANGLES, num_tris_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
            }

            int tex_id = tris[j].texture;
            glActiveTexture(GL_TEXTURE0 + 0);
            if (tex_id == 0)
                glBindTexture(GL_TEXTURE_2D, blank_tex_white);
            else
                glBindTexture(GL_TEXTURE_2D, tex_id);

			float param2 = tris[j].bm && tris[j].bm->useLights ? 1.0 : 0.0;
			glUniform1f(std3D_uiProgram.uniform_param2, param2);

            if (tris[j].flags) {
                glUniform1f(std3D_uiProgram.uniform_param1, 1.0);
            }
            else {
                glUniform1f(std3D_uiProgram.uniform_param1, 0.0);
            }
            
            last_ui_tex = tris[j].texture;
            last_ui_flags = tris[j].flags;
            last_tex_idx = j;

            do_batch = 0;
        }
        //printf("tri %u: %u,%u,%u\n", j, tris[j].v1, tris[j].v2, tris[j].v3);
        
        
        /*int vert = tris[j].v1;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v2;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v3;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);*/
    }
    
    int remaining_batch = GL_tmpUITrisAmt - last_tex_idx;

    if (remaining_batch)
    {
        glDrawElements(GL_TRIANGLES, remaining_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
    }

    // Done drawing    
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);

	glBindVertexArray(vao);

    std3D_ResetUIRenderList();
}

// todo: clean this shit up
void std3D_DrawSimpleTex(std3DSimpleTexStage* pStage, std3DIntermediateFbo* pFbo, GLuint texId, GLuint texId2, GLuint texId3, float param1, float param2, float param3, int gen_mips, const char* debugName)
{
	std3D_PushDebugGroup(debugName);

    glBindFramebuffer(GL_FRAMEBUFFER, pFbo->fbo);
    glDepthFunc(GL_ALWAYS);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(pStage->program);
    
	glBindVertexArray(vao);

    float menu_w, menu_h, menu_u, menu_v, menu_x;
    menu_w = (double)pFbo->w;
    menu_h = (double)pFbo->h;
    menu_u = 1.0;
    menu_v = 1.0;
    menu_x = 0.0;
	    
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, texId);
    if (gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texId2 ? texId2 : blank_tex);
    if (texId2 && gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, texId3 ? texId3 : blank_tex);
    if (texId3 && gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);

    GLushort data_elements[32 * 3];

    D3DVERTEX* vertexes = GL_tmpVertices;
    
    glUniform1i(pStage->uniform_tex, 0);
    glUniform1i(pStage->uniform_tex2, 1);
    glUniform1i(pStage->uniform_tex3, 2);
	glUniform1i(pStage->uniform_tex4, 3);

	glUniform3f(pStage->uniform_tint, rdroid_curColorEffects.tint.x, rdroid_curColorEffects.tint.y, rdroid_curColorEffects.tint.z);
	if (rdroid_curColorEffects.filter.x || rdroid_curColorEffects.filter.y || rdroid_curColorEffects.filter.z)
		glUniform3f(pStage->uniform_filter, rdroid_curColorEffects.filter.x ? 1.0 : 0.25, rdroid_curColorEffects.filter.y ? 1.0 : 0.25, rdroid_curColorEffects.filter.z ? 1.0 : 0.25);
	else
		glUniform3f(pStage->uniform_filter, 1.0, 1.0, 1.0);
	glUniform1f(pStage->uniform_fade, rdroid_curColorEffects.fade);
	glUniform3f(pStage->uniform_add, (float)rdroid_curColorEffects.add.x / 255.0f, (float)rdroid_curColorEffects.add.y / 255.0f, (float)rdroid_curColorEffects.add.z / 255.0f);

    {

    float maxX, maxY, scaleX, scaleY, width, height;

    scaleX = 1.0/((double)pFbo->w / 2.0);
    scaleY = 1.0/((double)pFbo->h / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = pFbo->w;
    height = pFbo->h;
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(pStage->uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);
    glUniform2f(pStage->uniform_iResolution, pFbo->iw, pFbo->ih);

    glUniform1f(pStage->uniform_param1, param1);
    glUniform1f(pStage->uniform_param2, param2);
    glUniform1f(pStage->uniform_param3, param3);

	glUniform1i(pStage->uniform_tex, TEX_SLOT_DIFFUSE);
	glUniform1i(pStage->uniform_lightbuf, TEX_SLOT_CLUSTER_BUFFER);

	glUniform2f(pStage->uniform_iResolution2, std3D_framebuffer.internalWidth, std3D_framebuffer.internalHeight);

	//if(renderPassProj)
		//glUniformMatrix4fv(pStage->uniform_proj, 1, GL_FALSE, (float*)renderPassProj);

	//glUniform3fv(pStage->uniform_rt, 1, (float*)&rdCamera_pCurCamera->pClipFrustum->rt);
	//glUniform3fv(pStage->uniform_lt, 1, (float*)&rdCamera_pCurCamera->pClipFrustum->lt);
	//glUniform3fv(pStage->uniform_rb, 1, (float*)&rdCamera_pCurCamera->pClipFrustum->rb);
	//glUniform3fv(pStage->uniform_lb, 1, (float*)&rdCamera_pCurCamera->pClipFrustum->lb);
    }
    
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	std3D_PopDebugGroup();
}

int std3D_SetCurrentPalette(rdColor24 *a1, int a2)
{
    return 1;
}

void std3D_GetValidDimension(unsigned int inW, unsigned int inH, unsigned int *outW, unsigned int *outH)
{
    // TODO hack for JKE? I don't know what they're doing
    *outW = inW > 256 ? 256 : inW;
    *outH = inH > 256 ? 256 : inH;
}

int std3D_DrawOverlay()
{
    return 1;
}

void std3D_UnloadAllTextures()
{
#ifndef SDL2_RENDER
    if (!Main_bHeadless) {
        glDeleteTextures(std3D_loadedTexturesAmt, std3D_aLoadedTextures);
    }
    std3D_loadedTexturesAmt = 0;
#else
    std3D_UpdateSettings();
#endif
}

void std3D_AddRenderListTris(rdTri *tris, unsigned int num_tris)
{
    if (Main_bHeadless) return;

    if (GL_tmpTrisAmt + num_tris > STD3D_MAX_TRIS)
    {
        return;
    }
    
    memcpy(&GL_tmpTris[GL_tmpTrisAmt], tris, sizeof(rdTri) * num_tris);
    
    GL_tmpTrisAmt += num_tris;
}

void std3D_AddRenderListLines(rdLine* lines, uint32_t num_lines)
{
    if (Main_bHeadless) return;

    if (GL_tmpLinesAmt + num_lines > STD3D_MAX_VERTICES)
    {
        return;
    }
    
    memcpy(&GL_tmpLines[GL_tmpLinesAmt], lines, sizeof(rdLine) * num_lines);
    GL_tmpLinesAmt += num_lines;
}

int std3D_AddRenderListVertices(D3DVERTEX *vertices, int count)
{
    if (Main_bHeadless) return 1;

    if (GL_tmpVerticesAmt + count >= STD3D_MAX_VERTICES)
    {
        return 0;
    }
    
    memcpy(&GL_tmpVertices[GL_tmpVerticesAmt], vertices, sizeof(D3DVERTEX) * count);
    
    GL_tmpVerticesAmt += count;
    
    return 1;
}

void std3D_AddRenderListUITris(rdUITri *tris, unsigned int num_tris)
{
    if (Main_bHeadless) return;

    if (GL_tmpUITrisAmt + num_tris > STD3D_MAX_TRIS)
    {
        return;
    }
    
    memcpy(&GL_tmpUITris[GL_tmpUITrisAmt], tris, sizeof(rdUITri) * num_tris);
    
    GL_tmpUITrisAmt += num_tris;
}

int std3D_ClearZBuffer()
{
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
    //glClear(GL_DEPTH_BUFFER_BIT);
	glClearColor(1, 1, 1, 1);
	glClear(/*GL_COLOR_BUFFER_BIT | */GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    return 1;
}

void rgb_to_hsv(float r, float g, float b, float* h, float* s, float* v)
{
	float min, max, delta;

	min = fminf(fminf(r, g), b);
	max = fmaxf(fmaxf(r, g), b);
	*v = max;
	delta = max - min;

	if (max != 0.0f)
	{
		*s = delta / max;
	}
	else
	{
		*s = 0.0f;
	}

	if (delta == 0.0f)
	{
		*h = 0.0f;
	}
	else
	{
		if (r == max)
		{
			*h = (g - b) / delta;  // Red is max
		}
		else if (g == max)
		{
			*h = (b - r) / delta + 2.0f;  // Green is max
		}
		else
		{
			*h = (r - g) / delta + 4.0f;  // Blue is max
		}
		*h *= 60.0f;
		if (*h < 0.0f)
		{
			*h += 360.0f;
		}
	}
}

float color_to_roughness(uint8_t ir, uint8_t ig, uint8_t ib)
{
	float r = (float)ir / 255.0f;
	float g = (float)ig / 255.0f;
	float b = (float)ib / 255.0f;

	float h, s, v;
	rgb_to_hsv(r, g, b, &h, &s, &v);

	float roughness = 1.0f - s;  // Inverse relation to saturation
	roughness *= (1.0f - v); // Brighter colors result in less roughness

	return roughness;
}

float calculate_saturation(float r, float g, float b)
{
	float min_val = fminf(fminf(r, g), b) / 255.0;
	float max_val = fmaxf(fmaxf(r, g), b) / 255.0;
	float delta = max_val - min_val;

	if (max_val == 0.0f) return 0.0f;
	return delta / max_val;
}

float calculate_luminance(float r, float g, float b)
{
	return 0.299f * r + 0.587f * g + 0.114f * b;
}

float generate_specular(float r, float g, float b, float specular_scale)
{
	float luminance = calculate_luminance(r/255.0, g/255.0, b/255.0);

	//luminance = stdMath_Clamp((luminance - 0.8f) * 4.0 + 0.8f, 0.0f, 1.0f);

	float maxl = 0.8;
	float minl = 0.4;
	//luminance = (luminance - minl) / (maxl - minl);
	//luminance = stdMath_Clamp(luminance, 0.0f, 1.0f);

	// sigmoid contrast
//	luminance = 1.0f / (1.0f + (expf(-(luminance - 0.5f) * 12.0f)));

	//luminance = (1.0f - luminance * luminance) * powf(_frand(), 16.0f);

	//luminance = 1.0f / (luminance * 100.0f + 1.0f);

	luminance = powf(luminance, 64.0) * 64.0;

	luminance = stdMath_Clamp(luminance, 0.0f, 1.0f);

	return luminance;// 1.-powf(luminance, 8.0f) * specular_scale;
}

int std3D_AddToTextureCache(stdVBuffer** vbuf, int numMips, rdDDrawSurface *texture, int is_alpha_tex, int no_alpha)
{
    if (Main_bHeadless) return 1;
    if (!vbuf || !*vbuf || !texture) return 1;
    if (texture->texture_loaded) return 1;

    if (std3D_loadedTexturesAmt >= STD3D_MAX_TEXTURES) {
        stdPlatform_Printf("ERROR: Texture cache exhausted!! Ask ShinyQuagsire to increase the size.\n");
        return 1;
    }
    //printf("Add to texture cache\n");
    
	GLuint displacement_texture = 0;
	GLuint specular_texture = 0;
	GLuint emissive_texture = 0;

    GLuint image_texture;
    glGenTextures(1, &image_texture);
    uint8_t* image_8bpp = (*vbuf)->sdlSurface->pixels;
    uint16_t* image_16bpp = (*vbuf)->sdlSurface->pixels;
    uint8_t* pal = (*vbuf)->palette;
    
    uint32_t width, height;
    width = (*vbuf)->format.width;
    height = (*vbuf)->format.height;

	glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips-1);

	if (jkPlayer_enableTextureFilter && texture->is_16bit)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	}

	stdVBuffer** vbufIter = vbuf;

    if ((*vbufIter)->format.format.colorMode)
    {
        texture->is_16bit = 1;
		if ((*vbufIter)->format.format.bpp == 32)
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			//if (!is_alpha_tex)
			//	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_8bpp);
			//else
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_8bpp);
		}
		else if ((*vbufIter)->format.format.bpp == 24)
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_8bpp);
		}
		else
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
			if (!is_alpha_tex)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, width, height, 0,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_8bpp);
			}
			else
			{
				if((*vbufIter)->format.format.g_bits == 4) // todo: make this an alpha check or something
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, image_8bpp);
				else
				{
					glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, width, height, 0,  GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, image_8bpp);
				}
			}
		}

		for(int mip = 1; mip < numMips; ++mip)
		{
			++vbufIter;

			image_8bpp = (*vbufIter)->sdlSurface->pixels;
			width = (*vbufIter)->format.width;
			height = (*vbufIter)->format.height;

			if ((*vbufIter)->format.format.bpp == 32)
			{
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				//if (!is_alpha_tex)
				//	glTexImage2D(GL_TEXTURE_2D, mip, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_8bpp);
				//else
					glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_8bpp);
			}
			else if ((*vbufIter)->format.format.bpp == 24)
			{
				glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
				glTexImage2D(GL_TEXTURE_2D, mip, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_8bpp);
			}
			else
			{
				glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
				if (!is_alpha_tex)
				{
					glTexImage2D(GL_TEXTURE_2D, mip, GL_RGB565, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_8bpp);
				}
				else
				{
					if ((*vbufIter)->format.format.g_bits == 4) // todo: make this an alpha check or something
						glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA4, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, image_8bpp);
					else
					{
						glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
						glTexImage2D(GL_TEXTURE_2D, mip, GL_RGB5_A1, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, image_8bpp);
					}
				}
			}
		}

		// generate the remaining mips
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, numMips - 1);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    }
    else {
		rdColor24* pal_master = sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps ? (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors : stdDisplay_gammaPalette;

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		
		//texture->is_16bit = 0;
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);

		texture->is_16bit = 0;

		// it's much faster on GPU side to simply convert to rgba instead of manually bilinear filtering
		uint16_t* image_data = (uint16_t*)malloc(width * height * 2);
		texture->pDataDepthConverted = (void*)image_data;

		float avg = 0.0;
		float max_lum = 0.0;
		float min_lum = 1.0;

		for (int j = 0; j < height; j++)
		{
			for (int i = 0; i < width; i++)
			{
				uint32_t index = (i * height) + j;
				uint16_t val_rgba = 0x0000;

				uint8_t val = image_8bpp[index];
				
				rdColor24 color;
				if (pal)
				{
					color.r = pal[(val * 3) + 2];
					color.g = pal[(val * 3) + 1];
					color.b = pal[(val * 3) + 0];
				}
				else
				{
					color = pal_master[val];
				}

				float lum = calculate_luminance(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
				avg += lum;
				max_lum = fmax(lum, max_lum);
				min_lum = fmin(lum, min_lum);

				if(is_alpha_tex)
				{
					val_rgba |= (val != (*vbufIter)->transparent_color) ? 1 : 0x0000;

					val_rgba |= ((((uint32_t)color.r >> 3) & 0x1F) << 11);
					val_rgba |= ((((uint32_t)color.g >> 3) & 0x1F) << 6);
					val_rgba |= ((((uint32_t)color.b >> 3) & 0x1F) << 1);
				}
				else
				{
					val_rgba |= ((((uint32_t)color.r >> 3) & 0x1F) << 11);
					val_rgba |= ((((uint32_t)color.g >> 2) & 0x3F) << 5);
					val_rgba |= ((((uint32_t)color.b >> 3) & 0x1F) << 0);
				}

				image_data[index] = val_rgba;
			}
		}

		avg /= width * height;

		glTexImage2D(GL_TEXTURE_2D, 0, is_alpha_tex ? GL_RGB5_A1 : GL_RGB565, width, height, 0, is_alpha_tex ? GL_RGBA : GL_RGB, is_alpha_tex ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5, image_data);

	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips - 1);
		for (int mip = 1; mip < numMips; ++mip)
		{
			++vbufIter;

			image_8bpp = (*vbufIter)->sdlSurface->pixels;
			pal = (*vbufIter)->palette;

			width = (*vbufIter)->format.width;
			height = (*vbufIter)->format.height;

			for (int j = 0; j < height; j++)
			{
				for (int i = 0; i < width; i++)
				{
					uint32_t index = (i * height) + j;
					uint16_t val_rgba = 0x0000;

					uint8_t val = image_8bpp[index];

					rdColor24 color;
					if (pal)
					{
						color.r = pal[(val * 3) + 2];
						color.g = pal[(val * 3) + 1];
						color.b = pal[(val * 3) + 0];
					}
					else
					{
						color = pal_master[val];
					}

					if (is_alpha_tex)
					{
						val_rgba |= (val != (*vbufIter)->transparent_color) ? 1 : 0x0000;
						val_rgba |= ((((uint32_t)color.r >> 3) & 0x1F) << 11);
						val_rgba |= ((((uint32_t)color.g >> 3) & 0x1F) << 6);
						val_rgba |= ((((uint32_t)color.b >> 3) & 0x1F) << 1);
					}
					else
					{
						val_rgba |= ((((uint32_t)color.r >> 3) & 0x1F) << 11);
						val_rgba |= ((((uint32_t)color.g >> 2) & 0x3F) << 5);
						val_rgba |= ((((uint32_t)color.b >> 3) & 0x1F) << 0);
					}

					image_data[index] = val_rgba;
				}
			}

			glTexImage2D(GL_TEXTURE_2D, mip, is_alpha_tex ? GL_RGB5_A1 : GL_RGB565, width, height, 0, is_alpha_tex ? GL_RGBA : GL_RGB, is_alpha_tex ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_5_6_5, image_data);
			//glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
		}

		// generate the remaining mips
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, numMips - 1);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);

		// auto generate a specular texture from the indices
		if(0)//1)
		{
			glGenTextures(1, &specular_texture);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, specular_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			
			GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

			if (jkPlayer_enableTextureFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			}

			vbufIter = vbuf;

			image_8bpp = (*vbufIter)->sdlSurface->pixels;
			pal = (*vbufIter)->palette;

			width = (*vbufIter)->format.width;
			height = (*vbufIter)->format.height;

			const uint32_t DITHER_LUT[16] = {
				0, 4, 1, 5,
				6, 2, 7, 3,
				1, 5, 0, 4,
				7, 3, 6, 2
			};

			for (int j = 0; j < height; j++)
			{
				for (int i = 0; i < width; i++)
				{
					uint32_t index = (i * height) + j;
					uint8_t val = image_8bpp[index];

					//uint32_t state = val * 747796405u + 2891336453u;
					//uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
					//val = ((word >> 22u) ^ word) & 0xFFu;


					int wrap_x = (i) & 3;
					int wrap_y = (j) & 3;
					int wrap_index = wrap_x + wrap_y * 4;

					//uint32_t state = val * 747796405u + 2891336453u;
					//val = (state >> 24u) & 0xFFu;

					//val = powf((float)val / 255.0, 0.5f) * 255;

					rdColor24 color;
					if (pal)
					{
						color.r = pal[(val * 3) + 2];
						color.g = pal[(val * 3) + 1];
						color.b = pal[(val * 3) + 0];
					}
					else
					{
						color = pal_master[val];
					}

					//val = color_to_roughness(color.r, color.g, color.b) * 255;
					//val *= (DITHER_LUT[wrap_index] / 8.0f)*0.5+0.5;
					//val = calculate_saturation(color.r, color.g, color.b) * 255;
				//	val = generate_specular(color.r, color.g, color.b, 1.0f) * 255;

					float l = calculate_luminance(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
				//	l /= avg;
				
				//	l = 1.0f / (1.0f + (expf(-(l - 0.5f) * 14.0f)));
				//	l = powf(l / 0.5, 4.0f) * 0.5;

					//if(avg < 0.1)
					//	l = max_lum - l;

					l = (l - min_lum) / (max_lum - min_lum);

					//float mid = (max_lum + min_lum) * 0.5;
					//if (max_lum < 0.3)
						//l = 1.0f - l;

					//l = stdMath_Sqrt(l);
					//l = powf(l, 0.25f);
					//l = powf(l, 1.0f) * avg * 2.0f;
					val = l * 255;

					//uint32_t state = val * 747796405u + 2891336453u;
					//val = (state >> 24u) & 0xFFu;

					((uint8_t*)image_data)[index] = val;
				}
			}

			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_data);

		//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips - 1);
			for (int mip = 1; mip < numMips; ++mip)
			{
				++vbufIter;

				image_8bpp = (*vbufIter)->sdlSurface->pixels;
				pal = (*vbufIter)->palette;

				width = (*vbufIter)->format.width;
				height = (*vbufIter)->format.height;

				for (int j = 0; j < height; j++)
				{
					for (int i = 0; i < width; i++)
					{
						uint32_t index = (i * height) + j;
						uint8_t val = image_8bpp[index];

						//uint32_t state = val * 747796405u + 2891336453u;
						//uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
						//val = ((word >> 22u) ^ word) & 0xFFu;

						int wrap_x = (i) & 3;
						int wrap_y = (j) & 3;
						int wrap_index = wrap_x + wrap_y * 4;

						//uint32_t state = val * 747796405u + 2891336453u;
						//val = (state >> 24u) & 0xFFu;

						//val = powf((float)val / 255.0, 0.5f) * 255;

						rdColor24 color;
						if (pal)
						{
							color.r = pal[(val * 3) + 2];
							color.g = pal[(val * 3) + 1];
							color.b = pal[(val * 3) + 0];
						}
						else
						{
							color = pal_master[val];
						}

						//val = color_to_roughness(color.r, color.g, color.b) * 255;
						//val *= (DITHER_LUT[wrap_index] / 8.0f) * 0.5 + 0.5;
						//val = calculate_saturation(color.r, color.g, color.b) * 255;
					//	val = generate_specular(color.r, color.g, color.b, 1.0f) * 255;

						//uint32_t state = val * 747796405u + 2891336453u;
					//	val = (state >> 24u) & 0xFFu;

						float l = calculate_luminance(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
					//	l /= avg;
						//if (avg < 0.1)
							//l = max_lum - l;

					//	l = 1.0f / (1.0f + (expf(-(l - 0.5f) * 14.0f)));

					//	l = powf(l / 0.5, 4.0f) * 0.5;
						l = (l - min_lum) / (max_lum - min_lum);

						//float mid = (max_lum + min_lum) * 0.5;
						//if (max_lum < 0.3)
						//	l = 1.0f - l;

						//l = powf(l, 1.0f) * avg * 2.0f;
						//l = powf(l, 0.25f);
						val = l * 255;

						((uint8_t*)image_data)[index] = val;
					}
				}

				glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_data);
				//glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
			}

			// generate the remaining mips
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, numMips - 1);
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		}

		// auto generate a displacement texture from the indices
		if (0)
		{
			glGenTextures(1, &displacement_texture);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, displacement_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);

			GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
			glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

			if (jkPlayer_enableTextureFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			}

			vbufIter = vbuf;

			image_8bpp = (*vbufIter)->sdlSurface->pixels;
			pal = (*vbufIter)->palette;

			width = (*vbufIter)->format.width;
			height = (*vbufIter)->format.height;

			for (int j = 0; j < height; j++)
			{
				for (int i = 0; i < width; i++)
				{
					uint32_t index = (i * height) + j;
					uint8_t val = image_8bpp[index];

					rdColor24 color;
					if (pal)
					{
						color.r = pal[(val * 3) + 2];
						color.g = pal[(val * 3) + 1];
						color.b = pal[(val * 3) + 0];
					}
					else
					{
						color = pal_master[val];
					}

					float l = calculate_luminance(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
					l = (l - min_lum) / (max_lum - min_lum);
					l = 1.0f - powf(l, 2.0f);

					val = l * 255;

					//uint32_t state = val * 747796405u + 2891336453u;
					//val = (state >> 24u) & 0xFFu;

					((uint8_t*)image_data)[index] = val;
				}
			}

			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_data);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips - 1);
			for (int mip = 1; mip < numMips; ++mip)
			{
				++vbufIter;

				image_8bpp = (*vbufIter)->sdlSurface->pixels;
				pal = (*vbufIter)->palette;

				width = (*vbufIter)->format.width;
				height = (*vbufIter)->format.height;

				for (int j = 0; j < height; j++)
				{
					for (int i = 0; i < width; i++)
					{
						uint32_t index = (i * height) + j;
						uint8_t val = image_8bpp[index];

						rdColor24 color;
						if (pal)
						{
							color.r = pal[(val * 3) + 2];
							color.g = pal[(val * 3) + 1];
							color.b = pal[(val * 3) + 0];
						}
						else
						{
							color = pal_master[val];
						}

					
						float l = calculate_luminance(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);
						l = (l - min_lum) / (max_lum - min_lum);
						l = 1.0f - powf(l, 2.0f);

						val = l * 255;

						((uint8_t*)image_data)[index] = val;
					}
				}

				glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_data);
				//glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
			}

			// generate the remaining mips
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, numMips - 1);
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		}
	
		// auto-generate an emissive texture from the light palette
		//uint8_t* light = sithWorld_pCurrentWorld ? (uint8_t*)sithWorld_pCurrentWorld->colormaps->lightlevel : NULL;
		uint8_t* light = sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps ? (uint8_t*)sithWorld_pCurrentWorld->colormaps->lightlevel : NULL;

		if(light && !pal)
		{
			glGenTextures(1, &emissive_texture);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, emissive_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);

			if (jkPlayer_enableTextureFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			}

			vbufIter = vbuf;

			image_8bpp = (*vbufIter)->sdlSurface->pixels;
			pal = (*vbufIter)->palette;

			width = (*vbufIter)->format.width;
			height = (*vbufIter)->format.height;

			for (int j = 0; j < height; j++)
			{
				for (int i = 0; i < width; i++)
				{
					uint32_t index = (i * height) + j;
					uint16_t val_rgba = 0x0000;

					//val_rgba |= (index != (*vbufIter)->transparent_color) ? 0xFF000000 : 0x00000000;

					uint8_t val = light[image_8bpp[index]];
						
					rdColor24* pal_master = sithWorld_pCurrentWorld ? (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors : stdDisplay_gammaPalette;
					rdColor24 color = pal_master[val];

					val_rgba |= (((uint32_t)color.r >> 3) << 11);
					val_rgba |= (((uint32_t)color.g >> 2) << 5);
					val_rgba |= (((uint32_t)color.b >> 3) << 0);

					image_data[index] = val_rgba;
				}
			}

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_data);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMips - 1);
			for (int mip = 1; mip < numMips; ++mip)
			{
				++vbufIter;

				image_8bpp = (*vbufIter)->sdlSurface->pixels;
				pal = (*vbufIter)->palette;

				width = (*vbufIter)->format.width;
				height = (*vbufIter)->format.height;

				for (int j = 0; j < height; j++)
				{
					for (int i = 0; i < width; i++)
					{
						uint32_t index = (i * height) + j;
						uint16_t val_rgba = 0x0000;

						//val_rgba |= (index != (*vbufIter)->transparent_color) ? 0xFF000000 : 0x00000000;

						uint8_t val = light[image_8bpp[index]];
						rdColor24* pal_master = sithWorld_pCurrentWorld ? (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors : stdDisplay_gammaPalette;
						rdColor24 color = pal_master[val];

						val_rgba |= (((uint32_t)color.r >> 3u) << 11);
						val_rgba |= (((uint32_t)color.g >> 2u) << 5);
						val_rgba |= (((uint32_t)color.b >> 3u) << 0);

						image_data[index] = val_rgba;
					}
				}

				glTexImage2D(GL_TEXTURE_2D, mip, GL_RGB565, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_data);
				//glTexImage2D(GL_TEXTURE_2D, mip, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
			}
		
			// generate the remaining mips
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, numMips - 1);
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		}

        //texture->pDataDepthConverted = NULL;
    }
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    std3D_aLoadedSurfaces[std3D_loadedTexturesAmt] = texture;
    std3D_aLoadedTextures[std3D_loadedTexturesAmt++] = image_texture;
    /*ext->surfacebuf = image_data;
    ext->surfacetex = image_texture;
    ext->surfacepaltex = pal_texture;*/
    
    texture->texture_id = image_texture;
	texture->specular_texture_id = specular_texture;
    texture->emissive_texture_id = emissive_texture;
    texture->displacement_texture_id = displacement_texture;
    texture->texture_loaded = 1;
    texture->emissive_factor[0] = 1.0;
    texture->emissive_factor[1] = 1.0;
    texture->emissive_factor[2] = 1.0;
    texture->albedo_factor[0] = 1.0;
    texture->albedo_factor[1] = 1.0;
    texture->albedo_factor[2] = 1.0;
    texture->albedo_factor[3] = 1.0;
    texture->displacement_factor = 0.0;// 0.05;
    texture->albedo_data = NULL;
    texture->displacement_data = NULL;
    texture->emissive_data = NULL;

    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    return 1;
}

int std3D_GetBitmapCacheIdx()
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        if (!std3D_aUIBitmaps[i]) {
            return i;
        }
    }
    return -1;
}

int std3D_AddBitmapToTextureCache(stdBitmap *texture, int mipIdx, int is_alpha_tex, int no_alpha)
{
    if (Main_bHeadless) return 1;
    if (!has_initted) return 0;
    if (!texture) return 1;
    if (mipIdx >= texture->numMips) return 1;
    if (!texture->abLoadedToGPU || texture->abLoadedToGPU[mipIdx]) return 1;

    stdVBuffer *vbuf = texture->mipSurfaces[mipIdx];
     if (!vbuf) return 1;

    int cacheIdx = std3D_GetBitmapCacheIdx();
    if (cacheIdx < 0) {
        stdPlatform_Printf("ERROR: Texture cache exhausted!! Ask ShinyQuagsire to increase the size.\n");
        return 1;
    }
    //printf("Add to texture cache\n");
    
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    uint8_t* image_8bpp = vbuf->sdlSurface->pixels;
    uint16_t* image_16bpp = vbuf->sdlSurface->pixels;
    uint8_t* pal = vbuf->palette;
    
    uint32_t width, height;
    width = vbuf->format.width;
    height = vbuf->format.height;

    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    /*if (jkPlayer_enableTextureFilter && texture->is_16bit)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    else*/
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    if (vbuf->format.format.colorMode || texture->format.bpp == 16)
    {
        texture->is_16bit = 1;

#if 0
        if (!is_alpha_tex)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_8bpp);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,  GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, image_8bpp);
#endif
        uint32_t tex_width, tex_height, tex_row_stride;
        uint32_t row_stride = vbuf->format.width_in_bytes / 2;
        tex_width = width;//vbuf->format.width_in_bytes / 2;
        tex_height = height;
        tex_row_stride = width;

        void* image_data = malloc(tex_width*tex_height*4);
        memset(image_data, 0, tex_width*tex_height*4);
    
        //uint32_t index = 0;
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (j*row_stride) + i;
                uint32_t tex_index = (j*tex_row_stride) + i;
                uint32_t val_rgba = 0x00000000;
                
                uint16_t val = image_16bpp[index];
                if (vbuf->format.format.r_bits == 5 && vbuf->format.format.g_bits == 6 && vbuf->format.format.b_bits == 5) // RGB565
                {
                    uint8_t val_a1 = 1;
                    uint8_t val_r5 = (val >> 11) & 0x1F;
                    uint8_t val_g6 = (val >> 5) & 0x3F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g6 * 259 + 33 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

                    //uint8_t transparent_r8 = (vbuf->transparent_color >> 16) & 0xFF;
                    //uint8_t transparent_g8 = (vbuf->transparent_color >> 8) & 0xFF;
                    //uint8_t transparent_b8 = (vbuf->transparent_color >> 0) & 0xFF;

                    uint8_t transparent_r5 = (vbuf->transparent_color >> 11) & 0x1F;
                    uint8_t transparent_g6 = (vbuf->transparent_color >> 5) & 0x3F;
                    uint8_t transparent_b5 = (vbuf->transparent_color >> 0) & 0x1F;

                    uint8_t transparent_r8 = ( transparent_r5 * 527 + 23 ) >> 6;
                    uint8_t transparent_g8 = ( transparent_g6 * 259 + 33 ) >> 6;
                    uint8_t transparent_b8 = ( transparent_b5 * 527 + 23 ) >> 6;

                    //
                    if (vbuf->transparent_color && val_r5 == transparent_r5 && val_g6 == transparent_g6 && val_b5 == transparent_b5) {
                        val_a8 = 0;
                        //val_r8 = 0;
                        //val_g8 = 0;
                        //val_b8 = 0;
                    }

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);

#if 0
                    val_rgba = 0xFF000000;
                    val_rgba |= (transparent_b8 << 16);
                    val_rgba |= (transparent_g8 << 8);
                    val_rgba |= (transparent_r8 << 0);
#endif
                }
                else if (vbuf->format.format.r_bits == 5 && vbuf->format.format.g_bits == 5 && vbuf->format.format.b_bits == 5) // RGB1555
                {
                    uint8_t val_a1 = (val >> 15);
                    uint8_t val_r5 = (val >> 10) & 0x1F;
                    uint8_t val_g5 = (val >> 5) & 0x1F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g5 * 527 + 23 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

                    uint8_t transparent_a1 = (vbuf->transparent_color >> 15) & 0x1;
                    uint8_t transparent_r5 = (vbuf->transparent_color >> 10) & 0x1F;
                    uint8_t transparent_g5 = (vbuf->transparent_color >> 5) & 0x1F;
                    uint8_t transparent_b5 = (vbuf->transparent_color >> 0) & 0x1F;

                    uint8_t transparent_r8 = ( transparent_r5 * 527 + 23 ) >> 6;
                    uint8_t transparent_g8 = ( transparent_g5 * 527 + 23 ) >> 6;
                    uint8_t transparent_b8 = ( transparent_b5 * 527 + 23 ) >> 6;

#if 0
                    //vbuf->transparent_color && 
                    if (val_a1 == transparent_a1 && val_r5 == transparent_r5 && val_g5 == transparent_g5 && val_b5 == transparent_b5) {
                        val_a8 = 0;
                        //val_r8 = 0;
                        //val_g8 = 0;
                        //val_b8 = 0;
                    }
#endif

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);
#if 0
                    val_rgba = 0xFF000000;
                    val_rgba |= (transparent_b8 << 16);
                    val_rgba |= (transparent_g8 << 8);
                    val_rgba |= (transparent_r8 << 0);
#endif
                }
                else {
                    stdPlatform_Printf("wtf is this %u %u %u %u\n", vbuf->format.format.unk_40, vbuf->format.format.r_bits, vbuf->format.format.g_bits, vbuf->format.format.b_bits);
                }
                    
                ((uint32_t*)image_data)[tex_index] = val_rgba;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        texture->paDataDepthConverted[mipIdx] = image_data;
    }
    else {
        texture->is_16bit = 0;
#if 1
        uint32_t tex_width, tex_height, tex_row_stride;
        uint32_t row_stride = vbuf->format.width_in_bytes;
        tex_width = width;//vbuf->format.width_in_bytes / 2;
        tex_height = height;
        tex_row_stride = width;

        void* image_data = malloc(tex_width*tex_height*4);
        memset(image_data, 0, tex_width*tex_height*4);

        void* palette_data = texture->palette;//displaypal_data;

        if (!palette_data) 
        {
            palette_data = std3D_ui_colormap.colors;//jkGui_stdBitmaps[2]->palette;
            pal = NULL;//palette_data;
        }
        else {
            pal = NULL;//texture->palette;
        }
    
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (j*row_stride) + i;
                uint32_t tex_index = (j*tex_row_stride) + i;
                uint32_t val_rgba = 0x00000000;
                
                if (pal)
                {
                    uint8_t val = image_8bpp[index];
                    val_rgba |= (pal[(val * 3) + 2] << 16);
                    val_rgba |= (pal[(val * 3) + 1] << 8);
                    val_rgba |= (pal[(val * 3) + 0] << 0);
                    val_rgba |= (0xFF << 24);

                    if (!val) {
                        val_rgba = 0;
                    }
                }
                else
                {
                    uint8_t val = image_8bpp[index];
#if 0
                    if (sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps && sithWorld_pCurrentWorld->colormaps->colors)
                    {
                        rdColor24* pal_master = (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors;//stdDisplay_gammaPalette;
                        rdColor24* color = &pal_master[val];
                        val_rgba |= (color->r << 16);
                        val_rgba |= (color->g << 8);
                        val_rgba |= (color->b << 0);
                        val_rgba |= (0xFF << 24);
                        stdPlatform_Printf("%x %x\n", val_rgba, val);
                    }
                    else {
                        val_rgba = 0xFFFFFFFF; // HACK
                    }
#endif

                    if (palette_data)
                    {
                        uint8_t color_r = ((uint8_t*)palette_data)[(val*3) + 0];
                        uint8_t color_g = ((uint8_t*)palette_data)[(val*3) + 1];
                        uint8_t color_b = ((uint8_t*)palette_data)[(val*3) + 2];

                        val_rgba |= (0xFF << 24);
                        val_rgba |= (color_b << 16);
                        val_rgba |= (color_g << 8);
                        val_rgba |= (color_r << 0);
                    }
                    else {
                        val_rgba = 0xFFFFFF00; // HACK
                    }
                    

                    if (!val) {
                        val_rgba = 0;
                    }
                }
                
                ((uint32_t*)image_data)[tex_index] = val_rgba;
            }
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        texture->paDataDepthConverted[mipIdx] = image_data;
#endif
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
    }
    
    std3D_aUIBitmaps[cacheIdx] = texture;
    std3D_aUITextures[cacheIdx] = image_texture;
    std3D_loadedUITexturesAmt++;
    /*ext->surfacebuf = image_data;
    ext->surfacetex = image_texture;
    ext->surfacepaltex = pal_texture;*/
    
    texture->aTextureIds[mipIdx] = image_texture;
    texture->abLoadedToGPU[mipIdx] = 1;

    glBindTexture(GL_TEXTURE_2D, blank_tex);

    return 1;
}

void std3D_UpdateFrameCount(rdDDrawSurface *surface)
{
}

// Added helpers
void std3D_UpdateSettings()
{
    jk_printf("Updating texture cache...\n");
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
        if (!tex) continue;

        if (!std3D_aLoadedTextures[i]) continue;
        glBindTexture(GL_TEXTURE_2D, std3D_aLoadedTextures[i]);

		if (jkPlayer_enableTextureFilter && tex->is_16bit)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		}
		else
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		}

        if (tex->emissive_texture_id != 0) {
            glBindTexture(GL_TEXTURE_2D, tex->emissive_texture_id);
            
            if (jkPlayer_enableTextureFilter)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            }
        }

        if (tex->displacement_texture_id != 0) {
            glBindTexture(GL_TEXTURE_2D, tex->displacement_texture_id);
            
            if (jkPlayer_enableTextureFilter)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            }
        }

		if (tex->specular_texture_id != 0)
		{
			glBindTexture(GL_TEXTURE_2D, tex->specular_texture_id);

			if (jkPlayer_enableTextureFilter)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			}
		}
    }

    glBindTexture(GL_TEXTURE_2D, blank_tex);
}

// Added
void std3D_Screenshot(const char* pFpath)
{
#ifdef TARGET_CAN_JKGM
    //if (!std3D_pFb) return;

    uint8_t* data = malloc(window.w * window.h * 3 * sizeof(uint8_t));
    glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
    glReadPixels(0, 0, window.w, window.h, GL_RGB, GL_UNSIGNED_BYTE, data);
    jkgm_write_png(pFpath, window.w, window.h, data);
    free(data);
#endif
}

int std3D_HasAlpha()
{
    return 1;
}

int std3D_HasModulateAlpha()
{
    return 1;
}

int std3D_HasAlphaFlatStippled()
{
    return 1;
}

void std3D_PurgeBitmapRefs(stdBitmap *pBitmap)
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        int texId = std3D_aUITextures[i];
        stdBitmap* tex = std3D_aUIBitmaps[i];
        if (!tex) continue;
        if (tex != pBitmap) continue;

        for (int j = 0; j < tex->numMips; j++)
        {
            if (tex->aTextureIds[j] == texId) {
                std3D_PurgeUIEntry(i, j);
                break;
            }
        }
    }
}

void std3D_PurgeSurfaceRefs(rdDDrawSurface *texture)
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
        if (!tex) continue;
        if (tex != texture) continue;

        std3D_PurgeTextureEntry(i);
    }
}

void std3D_PurgeTextureEntry(int i) {
    if (std3D_aLoadedTextures[i]) {
        glDeleteTextures(1, &std3D_aLoadedTextures[i]);
        std3D_aLoadedTextures[i] = 0;
    }

    rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
    if (!tex) return;

    if (tex->pDataDepthConverted != NULL) {
        free(tex->pDataDepthConverted);
        tex->pDataDepthConverted = NULL;
    }

    if (tex->albedo_data != NULL) {
        //jkgm_aligned_free(tex->albedo_data);
        tex->albedo_data = NULL;
    }

    if (tex->emissive_data != NULL) {
        //jkgm_aligned_free(tex->emissive_data);
        tex->emissive_data = NULL;
    }

    if (tex->displacement_data != NULL) {
        //jkgm_aligned_free(tex->displacement_data);
        tex->displacement_data = NULL;
    }

    if (tex->emissive_texture_id != 0) {
        glDeleteTextures(1, &tex->emissive_texture_id);
        tex->emissive_texture_id = 0;
    }

	if (tex->specular_texture_id != 0) {
		glDeleteTextures(1, &tex->specular_texture_id);
		tex->specular_texture_id = 0;
	}

    if (tex->displacement_texture_id != 0) {
        glDeleteTextures(1, &tex->displacement_texture_id);
        tex->displacement_texture_id = 0;
    }

    tex->emissive_factor[0] = 0.0;
    tex->emissive_factor[1] = 0.0;
    tex->emissive_factor[2] = 0.0;
    tex->albedo_factor[0] = 1.0;
    tex->albedo_factor[1] = 1.0;
    tex->albedo_factor[2] = 1.0;
    tex->albedo_factor[3] = 1.0;
    tex->displacement_factor = 0.0;

    tex->texture_loaded = 0;
    tex->texture_id = 0;

    std3D_aLoadedSurfaces[i] = NULL;
}

void std3D_PurgeUIEntry(int i, int idx) {
    if (std3D_aUITextures[i]) {
        glDeleteTextures(1, &std3D_aUITextures[i]);
        std3D_aUITextures[i] = 0;
    }

    stdBitmap* tex = std3D_aUIBitmaps[i];
    if (!tex) return;

    tex->abLoadedToGPU[idx] = 0;
    tex->aTextureIds[idx] = 0;
    free(tex->paDataDepthConverted[idx]);
    tex->paDataDepthConverted[idx] = NULL;

    std3D_aUIBitmaps[i] = NULL;
    std3D_loadedUITexturesAmt--;
}

void std3D_PurgeTextureCache()
{
    if (Main_bHeadless) {
        std3D_loadedTexturesAmt = 0;
        return;
    }

#ifdef HW_VBUFFER
	std3D_PurgeDrawSurfaces();
#endif
	std3D_PurgeDecals();

	jk_printf("Purging bitmap cache... %x\n", std3D_loadedUITexturesAmt);
	for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
	{
		int texId = std3D_aUITextures[i];
		stdBitmap* tex = std3D_aUIBitmaps[i];
		if (!tex) continue;

		for (int j = 0; j < tex->numMips; j++)
		{
			if (tex->aTextureIds[j] == texId)
			{
				std3D_PurgeUIEntry(i, j);
				break;
			}
		}
	}
	std3D_loadedUITexturesAmt = 0;
	

    if (!std3D_loadedTexturesAmt) {
        jk_printf("Skipping texture cache purge, nothing loaded.\n");
        return;
    }

    jk_printf("Purging texture cache... %x\n", std3D_loadedTexturesAmt);
    for (int i = 0; i < std3D_loadedTexturesAmt; i++)
    {
        std3D_PurgeTextureEntry(i);
    }
    std3D_loadedTexturesAmt = 0;
}


void std3D_PurgeEntireTextureCache()
{
	if (Main_bHeadless)
	{
		std3D_loadedTexturesAmt = 0;
		return;
	}

	if (!std3D_loadedTexturesAmt)
	{
		jk_printf("Skipping texture cache purge, nothing loaded.\n");
		return;
	}

	jk_printf("Purging texture cache... %x\n", std3D_loadedTexturesAmt);
	for (int i = 0; i < std3D_loadedTexturesAmt; i++)
	{
		std3D_PurgeTextureEntry(i);
	}
	std3D_loadedTexturesAmt = 0;
}


void std3D_InitializeViewport(rdRect *viewRect)
{
    std3D_rectViewIdk.x = viewRect->x;
    std3D_rectViewIdk.y = viewRect->y;
    std3D_rectViewIdk.width = viewRect->width;
	std3D_rectViewIdk.height = viewRect->height;

	// this looks like some kind of viewport matrix?
    memset(std3D_aViewIdk, 0, sizeof(std3D_aViewIdk));
    std3D_aViewIdk[0] = (float)std3D_rectViewIdk.x;
    std3D_aViewIdk[1] = (float)std3D_rectViewIdk.y;
	std3D_aViewIdk[8] = std3D_aViewIdk[16] = (float)(viewRect->width + std3D_rectViewIdk.x);
	std3D_aViewIdk[9] = std3D_aViewIdk[1];
	std3D_aViewIdk[17] = std3D_aViewIdk[25] = (float)(viewRect->height + std3D_rectViewIdk.y);
	std3D_aViewIdk[24] = std3D_aViewIdk[0];

	// this looks like some kind of screen quad?
	std3D_aViewTris[0].v1 = 0;
    std3D_aViewTris[0].v2 = 1;
    std3D_aViewTris[0].texture = 0;
    std3D_aViewTris[0].v3 = 2;
    std3D_aViewTris[0].flags = 0x8200;
    std3D_aViewTris[1].v1 = 0;
    std3D_aViewTris[1].v2 = 2;
    std3D_aViewTris[1].v3 = 3;
    std3D_aViewTris[1].texture = 0;
    std3D_aViewTris[1].flags = 0x8200;
}

int std3D_GetValidDimensions(int a1, int a2, int a3, int a4)
{
    int result; // eax

    std3D_gpuMaxTexSizeMaybe = a1;
    result = a4;
    std3D_dword_53D66C = a2;
    std3D_dword_53D670 = a3;
    std3D_dword_53D674 = a4;
    return result;
}

int std3D_FindClosestDevice(uint32_t index, int a2)
{
    return 0;
}

int std3D_SetRenderList(intptr_t a1)
{
    std3D_renderList = a1;
    return std3D_CreateExecuteBuffer();
}

intptr_t std3D_GetRenderList()
{
    return std3D_renderList;
}

int std3D_CreateExecuteBuffer()
{
    return 1;
}

int std3D_IsReady()
{
    return has_initted;
}

void std3D_BlitFramebuffer(int x, int y, int width, int height, void* pixels)
{
	glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

int std3D_HasDepthWrites(std3D_DrawCallState* pState)
{
	return pState->stateBits.zMethod == RD_ZBUFFER_READ_WRITE || pState->stateBits.zMethod == RD_ZBUFFER_NOREAD_WRITE;
}

GLuint std3D_PrimitiveForGeoMode(rdGeoMode_t geoMode)
{
	switch (geoMode)
	{
	case RD_GEOMODE_VERTICES:
		return GL_POINTS;
	case RD_GEOMODE_WIREFRAME:
		return GL_LINES;
	case RD_GEOMODE_SOLIDCOLOR:
	case RD_GEOMODE_TEXTURED:
	case RD_GEOMODE_NOTRENDERED:
	default:
		return GL_TRIANGLES;
	}
}

int std3D_GetStageIndex(std3D_DrawCallState* pState)
{
	int alphaTest = pState->stateBits.alphaTest & 1;
	int blending  = pState->stateBits.blend & 1;

	if (alphaTest || blending)
		return WORLD_STAGE_COLOR_ALPHATEST;

	return WORLD_STAGE_COLOR;
}

void std3D_UpdateSharedUniforms()
{
	// uniforms shared across draw lists during flush
	sharedUniforms.timeSeconds = sithTime_curSeconds;
	sharedUniforms.deltaTime = sithTime_deltaSeconds;

	//sharedUniforms.lightMult = 1.0f / rdroid_overbright;//jkGuiBuildMulti_bRendering ? 0.85 : (jkPlayer_enableBloom ? 0.9 : 0.85);
	//sharedUniforms.invlightMult = rdroid_overbright;

	uint8_t bpp = std3D_framebufferFlags & 0x4 ? 10 : 5;
	sharedUniforms.ditherScaleAlways = 1.0f / (float)((1 << bpp) - 1);

	if (jkPlayer_enableDithering)
		sharedUniforms.ditherScale = sharedUniforms.ditherScaleAlways;
	else
		sharedUniforms.ditherScale = 0.0f;

	rdVector_Set2(&sharedUniforms.resolution, std3D_framebuffer.internalWidth, std3D_framebuffer.internalHeight);
	//rdVector_Set2(&sharedUniforms.resolution2, std3D_framebuffer.w, std3D_framebuffer.h);

	extern rdVector4 rdroid_sgBasis[RD_AMBIENT_LOBES]; //eww
	memcpy(sharedUniforms.sgBasis, rdroid_sgBasis, sizeof(rdVector4) * RD_AMBIENT_LOBES);

	float mipScale = 1.0 / rdCamera_GetMipmapScalar();
	rdVector_Set4(&sharedUniforms.mipDistances, mipScale * rdroid_aMipDistances.x, mipScale * rdroid_aMipDistances.y, mipScale * rdroid_aMipDistances.z, mipScale * rdroid_aMipDistances.w);

	// this one isn't really used so let's store the bias in it
	sharedUniforms.mipDistances.w = (float)jkPlayer_mipBias;
	if (std3D_framebuffer.samples == SAMPLE_2x2 && std3D_framebuffer.downscale)
		sharedUniforms.mipDistances.w -= 2.0; // bias

	// todo: move me
	sharedUniforms.scale_bias[0] = (rdVector4){ 1.0f, 0.0f,0,0};
	sharedUniforms.scale_bias[1] = (rdVector4){ 2.0f, 0.0f,0,0 };
	sharedUniforms.scale_bias[2] = (rdVector4){ 4.0f, 0.0f,0,0 };
	sharedUniforms.scale_bias[3] = (rdVector4){ 0.5f, 0.0f,0,0 };
	sharedUniforms.scale_bias[4] = (rdVector4){ 0.25f, 0.0f,0,0 };
	sharedUniforms.scale_bias[5] = (rdVector4){ 1.0f, -0.5f,0,0 };
	sharedUniforms.scale_bias[6] = (rdVector4){ 2.0f, -1.0f,0,0 };

	glBindBuffer(GL_UNIFORM_BUFFER, shared_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_SharedUniforms), &sharedUniforms);
}


// todo: track state bits and only apply necessary changes
void std3D_SetRasterState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	std3D_RasterState* pRasterState = &pState->rasterState;

	// todo: this should be in depth stencil
	if (pState->rasterState.stencilMode == 0)
	{
		glDisable(GL_STENCIL_TEST);
	}
	else
	{
		glEnable(GL_STENCIL_TEST);
		glStencilMask(0xFF);
		if (pState->rasterState.stencilMode == 2) // write
		{
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilFunc(GL_ALWAYS, pState->rasterState.stencilBit, 0xFF);
		}
		else // test
		{
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glStencilFunc(GL_EQUAL, pState->rasterState.stencilBit, 0xFF);
		}
	}

	GLboolean r = (pState->rasterState.colorMask & 0x000000FF) != 0;
	GLboolean g = (pState->rasterState.colorMask & 0x0000FF00) != 0;
	GLboolean b = (pState->rasterState.colorMask & 0x00FF0000) != 0;
	GLboolean a = (pState->rasterState.colorMask & 0xFF000000) != 0;
	glColorMask(r, g, b, a);

	rdViewportRect rect = pRasterState->viewport;
	if (std3D_framebuffer.downscale)
	{
		if (std3D_framebuffer.samples == 2)
		{
			// 2:1 horizontal
			// 1:1 vertical
			rect.x /= 2;
			rect.width /= 2;
		}
		else if (std3D_framebuffer.samples == 4)
		{
			// 2:1 horizontal
			// 2:1 vertical
			rect.x /= 2;
			rect.width /= 2;
			rect.y /= 2;
			rect.height /= 2;
		}
	}

	glViewport(rect.x, rect.y, rect.width, rect.height);
	if(pState->stateBits.scissorMode == RD_SCISSOR_ENABLED)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
	
	glScissor(
		pRasterState->scissor.x * pRasterState->viewport.width,
		(1.0f - pRasterState->scissor.height - pRasterState->scissor.y) * pRasterState->viewport.height,
		pRasterState->scissor.width * pRasterState->viewport.width,
		pRasterState->scissor.height * pRasterState->viewport.height
	);

	if(pState->stateBits.cullMode == RD_CULL_MODE_NONE)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);

	//glFrontFace(pState->stateBits.cullMode == RD_CULL_MODE_CW_ONLY ? GL_CW : GL_CCW);
	glFrontFace(GL_CCW);
	glCullFace(pState->stateBits.cullMode == RD_CULL_MODE_BACK ? GL_BACK : GL_FRONT);

	//glUniform1i(pStage->uniform_geo_mode, pState->stateBits.geoMode + 1);
	glUniform1i(U_GEO_MODE, pState->stateBits.geoMode + 1);

	std3D_bindTexture(GL_TEXTURE_2D, phase_texture, TEX_SLOT_PHASE);
	std3D_bindTexture(GL_TEXTURE_1D, blackbody_texture, TEX_SLOT_BLACKBODY);

	std3D_bindTexture(GL_TEXTURE_2D, deferred.tex, TEX_SLOT_DIFFUSE_LIGHT);
}

void std3D_SetFogState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	std3D_FogUniforms fog;
	fog.fogEnabled = pState->stateBits.fogMode;
	fog.fogStartDepth = pState->fogState.startDepth;
	fog.fogEndDepth = pState->fogState.endDepth;
	fog.fogAnisotropy = pState->fogState.anisotropy * 0.5f + 0.5f;
	fog.fogLightDir.x = pState->fogState.lightDir.x;
	fog.fogLightDir.y = pState->fogState.lightDir.y;
	fog.fogLightDir.z = pState->fogState.lightDir.z;
	fog.fogLightDir.w = rdVector_Normalize3Acc((rdVector3*)&fog.fogLightDir) > 0.0;

	float a = ((pState->fogState.color >> 24) & 0xFF) / 255.0f;
	float r = ((pState->fogState.color >> 16) & 0xFF) / 255.0f;
	float g = ((pState->fogState.color >> 8) & 0xFF) / 255.0f;
	float b = ((pState->fogState.color >> 0) & 0xFF) / 255.0f;
	rdVector_Set4(&fog.fogColor, r, g, b, a);

	glBindBuffer(GL_UNIFORM_BUFFER, fog_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_FogUniforms), &fog);
}

int std3D_SetBlendState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	if (pState->stateBits.blend == 0)
	{
		glDisable(GL_BLEND);
		//std3D_bindTexture(GL_TEXTURE_2D, blank_tex, TEX_SLOT_REFRACTION);
	}
	else
	{
		glEnable(GL_BLEND);
		//std3D_bindTexture(GL_TEXTURE_2D, refr.tex, TEX_SLOT_REFRACTION);
	}


	static GLuint glBlendForRdBlend[] =
	{
		GL_ZERO,                // RD_BLEND_ZERO
		GL_ONE,                 // RD_BLEND_ONE
		GL_DST_COLOR,           // RD_BLEND_DSTCOLOR
		GL_ONE_MINUS_DST_COLOR, // RD_BLEND_INVDSTCOLOR
		GL_SRC_ALPHA,           // RD_BLEND_SRCALPHA
		GL_ONE_MINUS_SRC_ALPHA, // RD_BLEND_INVSRCALPHA
		GL_SRC_COLOR,			// RD_BLEND_SRCCOLOR
		GL_ONE_MINUS_SRC_COLOR,	// RD_BLEND_INVSRCCOLOR
		//GL_DST_ALPHA,           // RD_BLEND_DSTALPHA
		//GL_ONE_MINUS_DST_ALPHA, // RD_BLEND_INVDSTALPHA
	};
	glBlendFunc(glBlendForRdBlend[pState->stateBits.srdBlend], glBlendForRdBlend[pState->stateBits.dstBlend]);
}

void std3D_SetDepthStencilState(std3D_DrawCallState* pState)
{
	if (pState->stateBits.zMethod == RD_ZBUFFER_NOREAD_NOWRITE)
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}
	else if (pState->stateBits.zMethod == RD_ZBUFFER_READ_NOWRITE)
	{
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}
	else if (pState->stateBits.zMethod == RD_ZBUFFER_NOREAD_WRITE)
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}
	else
	{
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
	}

	// todo: add to draw call state
	extern float rdroid_curZNear;
	extern float rdroid_curZFar;
	glDepthRange(rdroid_curZNear, rdroid_curZFar);

	static const GLuint gl_compares[] =
	{
		GL_ALWAYS,
		GL_LESS,
		GL_LEQUAL,
		GL_GREATER,
		GL_GEQUAL,
		GL_EQUAL,
		GL_NOTEQUAL,
		GL_NEVER
	};
	glDepthFunc(gl_compares[pState->stateBits.zCompare]);

//	if (pState->stateBits.zMethod == RD_ZBUFFER_NOREAD_NOWRITE)
//	{
//		glDisable(GL_DEPTH_TEST);
//		glDepthMask(GL_FALSE);
//	}
//	else if (pState->stateBits.zMethod == RD_ZBUFFER_READ_NOWRITE)
//	{
//		glEnable(GL_DEPTH_TEST);
//		glDepthMask(GL_FALSE);
//	}
//	else if (pState->stateBits.zMethod == RD_ZBUFFER_NOREAD_WRITE)
//	{
//		glDisable(GL_DEPTH_TEST);
//		glDepthMask(GL_TRUE);
//	}
//	else
//	{
//		glEnable(GL_DEPTH_TEST);
//		glDepthMask(GL_TRUE);
//	}
//
//	static const GLuint gl_compares[] =
//	{
//		GL_ALWAYS,
//		GL_LESS,
//		GL_LEQUAL,
//		GL_GREATER,
//		GL_GEQUAL,
//		GL_EQUAL,
//		GL_NOTEQUAL,
//		GL_NEVER
//	};
//	glDepthFunc(gl_compares[pState->stateBits.zCompare]);
}
rdMatrix44* renderPassProj = NULL;
void std3D_SetTransformState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	renderPassProj = &pState->transformState.proj;

	float fov = 2.0 * atanf(1.0 / renderPassProj->vC.y);
	float aspect = renderPassProj->vC.y / renderPassProj->vA.x;
	float znear = -renderPassProj->vD.z / (renderPassProj->vB.z + 1.0f);
	float zfar = -renderPassProj->vD.z / (renderPassProj->vB.z - 1.0f);

	float T = znear * tanf(0.5 * fov);
	float R = aspect * T;

	glUniformMatrix4fv(U_PROJ_MATRIX, 1, GL_FALSE, (float*)renderPassProj);
	//glUniform2f(pStage->uniform_rightTop, R, T);

	glUniformMatrix4fv(U_VIEW_MATRIX, 1, GL_FALSE, (float*)&pState->transformState.view);
	glUniformMatrix4fv(U_MODEL_MATRIX, 1, GL_FALSE, (float*)&pState->transformState.modelView);
	
	// todo: move to rdroid?
	rdMatrix44 viewMatrixInv;
	rdMatrix_Invert44(&viewMatrixInv, &pState->transformState.view);
	glUniformMatrix4fv(U_VIEW_MATRIX_INV, 1, GL_FALSE, (float*)&viewMatrixInv);

#ifdef MOTION_BLUR
	glUniformMatrix4fv(U_MODEL_MATRIX_PREV, 1, GL_FALSE, (float*)&pState->transformState.modelPrev);
	glUniformMatrix4fv(U_MODEL_VIEW_PREV, 1, GL_FALSE, (float*)&pState->transformState.viewPrev);
#endif

//	glUniformMatrix4fv(pStage->uniform_projection, 1, GL_FALSE, (float*)renderPassProj);
//	glUniform2f(pStage->uniform_rightTop, R, T);
//
//	glUniformMatrix4fv(pStage->uniform_viewMatrix, 1, GL_FALSE, (float*)&pState->transformState.view);
//	glUniformMatrix4fv(pStage->uniform_modelMatrix, 1, GL_FALSE, (float*)&pState->transformState.modelView);
}

void std3D_SetTextureState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	std3D_TextureState* pTexState = &pState->textureState;
	rdDDrawSurface* pTexture = (pState->stateBits.geoMode + 1 >= RD_GEOMODE_TEXTURED) ? pState->textureState.pTexture : NULL;

	std3D_TextureUniforms tex;
	tex.uv_mode = pState->stateBits.texMode;
	tex.texgen = pState->stateBits.texGen;
	tex.numMips = pTexState->numMips;
	rdVector_Set4(&tex.uv_offset[0], pTexState->texOffset[0].x, pTexState->texOffset[0].y, 0, 0);
	rdVector_Set4(&tex.uv_offset[1], pTexState->texOffset[1].x, pTexState->texOffset[1].y, 0, 0);
	rdVector_Set4(&tex.uv_offset[2], pTexState->texOffset[2].x, pTexState->texOffset[2].y, 0, 0);
	rdVector_Set4(&tex.uv_offset[3], pTexState->texOffset[3].x, pTexState->texOffset[3].y, 0, 0);
	rdVector_Set4(&tex.texgen_params, pTexState->texGenParams.x, pTexState->texGenParams.y, pTexState->texGenParams.z, pTexState->texGenParams.w);

	extern int jkPlayer_showLight;
	if(pTexture && !jkPlayer_showLight)
	{
		int tex_id = pTexture->texture_id ? pTexture->texture_id : blank_tex_white;
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_DIFFUSE);
		glBindTexture(GL_TEXTURE_2D, tex_id);

		int clampX = (pTexState->flags & RD_FF_TEX_CLAMP_X) ? 1 : 0;
		int clampY = (pTexState->flags & RD_FF_TEX_CLAMP_Y) ? 1 : 0;

		if ((pState->stateBits.texFilter == RD_TEXFILTER_BILINEAR))// && pTexture->is_16bit)
			glBindSampler(TEX_SLOT_DIFFUSE, linearSampler[clampX + clampY * 2]);
		else
			glBindSampler(TEX_SLOT_DIFFUSE, nearestSampler[clampX + clampY * 2]);

		int emiss_tex_id = pTexture->emissive_texture_id;
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_EMISSIVE);
		glBindTexture(GL_TEXTURE_2D, emiss_tex_id ? emiss_tex_id : blank_tex);
		
		if ((pState->stateBits.texFilter == RD_TEXFILTER_BILINEAR))// && pTexture->is_16bit)
			glBindSampler(TEX_SLOT_EMISSIVE, linearSampler[clampX + clampY * 2]);
		else
			glBindSampler(TEX_SLOT_EMISSIVE, nearestSampler[clampX + clampY * 2]);

		int displace_tex_id = pTexture->displacement_texture_id;
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_DISPLACEMENT);
		glBindTexture(GL_TEXTURE_2D, displace_tex_id ? displace_tex_id : blank_tex);
		
		//if ((pState->stateBits.texFilter == RD_TEXFILTER_BILINEAR))// && pTexture->is_16bit)
			glBindSampler(TEX_SLOT_DISPLACEMENT, linearSampler[clampX + clampY * 2]);
		//else
		//	glBindSampler(TEX_SLOT_DISPLACEMENT, nearestSampler[clampX + clampY * 2]);

		int spec_tex_id = pTexture->specular_texture_id;
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_TEX3);
		glBindTexture(GL_TEXTURE_2D, spec_tex_id ? spec_tex_id : blank_tex);

		if ((pState->stateBits.texFilter == RD_TEXFILTER_BILINEAR))
			glBindSampler(TEX_SLOT_EMISSIVE, linearSampler[clampX + clampY * 2]);
		else
			glBindSampler(TEX_SLOT_EMISSIVE, nearestSampler[clampX + clampY * 2]);


		glActiveTexture(GL_TEXTURE0 + 0);



		if (tex_id == 0)
		{
			tex.tex_mode = TEX_MODE_TEST;
		}
		else
		{
			if (/*!jkPlayer_enableTextureFilter || */ (pState->stateBits.texFilter == RD_TEXFILTER_NEAREST))
				tex.tex_mode = pTexture->is_16bit ? TEX_MODE_16BPP : TEX_MODE_WORLDPAL;
			else
				tex.tex_mode = pTexture->is_16bit ? TEX_MODE_BILINEAR_16BPP : TEX_MODE_BILINEAR;
		}

		rdVector_Set2(&tex.texsize, pTexture->width, pTexture->height);
		tex.texwidth = pTexture->width;
		tex.texheight = pTexture->height;
	}
	else
	{
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_EMISSIVE);
		glBindTexture(GL_TEXTURE_2D, blank_tex); // emissive
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_DISPLACEMENT);
		glBindTexture(GL_TEXTURE_2D, blank_tex); // displace
		glActiveTexture(GL_TEXTURE0 + TEX_SLOT_DIFFUSE);
		glBindTexture(GL_TEXTURE_2D, blank_tex_white);
		glActiveTexture(GL_TEXTURE0);

		tex.tex_mode = TEX_MODE_TEST;
		rdVector_Set2(&tex.texsize, 1, 1);

		tex.texwidth = 1;
		tex.texheight = 1;
	}

	glBindBuffer(GL_UNIFORM_BUFFER, tex_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_TextureUniforms), &tex);
}

#define RD_SET_COLOR4(target, color) \
	rdVector_Set4(target, ((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 0) & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f);

void std3D_SetMaterialState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	std3D_MaterialState* pMaterialState = &pState->materialState;
	rdDDrawSurface* pTexture = (pState->stateBits.geoMode + 1 == RD_GEOMODE_TEXTURED) ? pState->textureState.pTexture : NULL;

	std3D_MaterialUniforms tex;
	RD_SET_COLOR4(&tex.fillColor, pMaterialState->fillColor);

	//if (pTexture)
	{
		RD_SET_COLOR4(&tex.emissive_factor, pMaterialState->emissive);
		RD_SET_COLOR4(&tex.albedo_factor, pMaterialState->albedo);
		tex.displacement_factor = pMaterialState->displacement;
	}
	//else
	//{
	//	RD_SET_COLOR4(&tex.emissive_factor, 0xFF000000);
	//	RD_SET_COLOR4(&tex.albedo_factor, 0xFFFFFFFF);
	//	tex.displacement_factor = 0.0;
	//}

	// todo: expose
	float spec = 0.2209f;//0.02;//0.2209f; / we're working in srgb
	rdVector_Set4(&tex.specular_factor, spec, spec, spec, spec);
	tex.roughnessFactor = (pState->shaderState.shader && pState->shaderState.shader->hasReadback) ? stdMath_Sqrt(2.0f / 64.0f) : stdMath_Sqrt(2.0f / 32.0f);

	glBindBuffer(GL_UNIFORM_BUFFER, material_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_MaterialUniforms), &tex);
}

void std3D_SetLightingState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	glUniform1i(U_LIGHT_MODE, pState->stateBits.lightMode);
	glUniform1ui(U_FLAGS, pState->lightingState.ambientFlags);

//	glUniform1i(pStage->uniform_light_mode, pState->stateBits.lightMode);
//	glUniform1i(pStage->uniform_ao_flags, pState->lightingState.ambientFlags);

	float a = ((pState->lightingState.ambientColor >> 24) & 0xFF) / 255.0f;
	float r = ((pState->lightingState.ambientColor >> 16) & 0xFF) / 255.0f;
	float g = ((pState->lightingState.ambientColor >>  8) & 0xFF) / 255.0f;
	float b = ((pState->lightingState.ambientColor >>  0) & 0xFF) / 255.0f;
//	glUniform3f(pStage->uniform_ambient_color, r, g, b);
	glUniform4f(U_AMB_COLOR, r, g, b, a);

	rdVector4 sgs[RD_AMBIENT_LOBES];
	for (int i = 0; i < RD_AMBIENT_LOBES; ++i)
	{
		float r = ((pState->lightingState.ambientLobes[i] >> 20) & 0x3FF) / 255.0f;
		float g = ((pState->lightingState.ambientLobes[i] >> 10) & 0x3FF) / 255.0f;
		float b = ((pState->lightingState.ambientLobes[i] >> 0) & 0x3FF) / 255.0f;
		float a = ((pState->lightingState.ambientLobes[i] >> 30) & 0x2);
		rdVector_Set4(&sgs[i], r, g, b, a);
	}
	glUniform4fv(U_AMB_SGS, RD_AMBIENT_LOBES, &sgs[0].x);
	glUniform4fv(U_AMB_CENTER, 1, &pState->lightingState.ambientCenter);
	glUniform1ui(U_AMB_NUM_SG, RD_AMBIENT_LOBES);

	glUniform1f(U_OVERBRIGHT, pState->lightingState.overbright);
	glUniform1f(U_INV_OVERBRIGHT, 1.0f / pState->lightingState.overbright);

	//glUniform4fv(pStage->uniform_ambient_sg, RD_AMBIENT_LOBES, &sgs[0].x);
	//glUniform4fv(pStage->uniform_ambient_center, 1, &pState->lightingState.ambientCenter);

	//glUniform1uiv(pStage->uniform_ambient_sg, 8, &pState->lightingState.ambientLobes[0]);
	//glUniform1ui(pStage->uniform_ambient_sg_count, RD_AMBIENT_LOBES);
}

void std3D_SetShaderState(std3D_worldStage* pStage, std3D_DrawCallState* pState)
{
	if (!pState->shaderState.shader || (pState->shaderState.shader->shaderid == 0))
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_SHADER, defaultShaderUBO);
	}
	else
	{
		glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SLOT_SHADER, pState->shaderState.shader->shaderid);
	
		glBindBuffer(GL_UNIFORM_BUFFER, shaderConstsUbo);
		rdShaderConstants constants;
		for (int i = 0; i < 8; ++i)
		{
			uint8_t bits = pState->shaderState.shader->overrideConstantBits;
			if (bits & (1 << i))
				constants.constants[i] = pState->shaderState.shader->overrideConstants.constants[i];
			else
				constants.constants[i] = pState->shaderState.constants.constants[i];
		}
		glBufferData(GL_UNIFORM_BUFFER, sizeof(rdShaderConstants), &constants, GL_DYNAMIC_DRAW);
	}
}

void std3D_BindStage(std3D_worldStage* pStage)
{
	glUseProgram(pStage->program);

	glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);

	glBindVertexArray(pStage->vao[bufferIdx]);
	glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all[bufferIdx]);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, world_ibo_triangle[bufferIdx]);

	GLuint texids[] = {
		TEX_SLOT_TEX0,
		TEX_SLOT_TEX1,
		TEX_SLOT_TEX2,
		TEX_SLOT_TEX3,
	};

	//glUniform1iv(pStage->uniform_textures, 4, texids);

	//glUniform1i(pStage->uniform_tex,                TEX_SLOT_DIFFUSE);
	//glUniform1i(pStage->uniform_worldPalette,       TEX_SLOT_WORLD_PAL);
	//glUniform1i(pStage->uniform_worldPaletteLights, TEX_SLOT_WORLD_LIGHT_PAL);
	//glUniform1i(pStage->uniform_texEmiss,           TEX_SLOT_EMISSIVE);
	//glUniform1i(pStage->uniform_displacement_map,   TEX_SLOT_DISPLACEMENT);
	//glUniform1i(pStage->uniform_lightbuf,           TEX_SLOT_CLUSTER_BUFFER);
	//glUniform1i(pStage->uniform_texDecals,          TEX_SLOT_DECAL_ATLAS);
	//glUniform1i(pStage->uniform_texz,               TEX_SLOT_DEPTH);
	//glUniform1i(pStage->uniform_texssao,            TEX_SLOT_AO);
	//glUniform1i(pStage->uniform_texrefraction,      TEX_SLOT_REFRACTION);
	//glUniform1i(pStage->uniform_texclip,            TEX_SLOT_CLIP);
	//glUniform1i(pStage->uniform_dithertex,          TEX_SLOT_DITHER);
	//glUniform1i(pStage->uniform_diffuse_light,      TEX_SLOT_DIFFUSE_LIGHT);
	//glUniform1i(pStage->uniform_blackbody_tex,      TEX_SLOT_BLACKBODY);
}

void std3D_BlitFrame()
{
	std3D_PushDebugGroup("std3D_BlitFrame");

	//glDepthMask(GL_FALSE);
	//std3D_DrawSimpleTex(&std3D_texFboStage, &refr, std3D_framebuffer.tex0, 0, 0, 1.0, 1.0, 1.0, 0, "Frmebuffer Blit");

	std3D_ResolveMSAA();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, std3D_framebuffer.resolveFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, refr.fbo);
	
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	glBlitFramebuffer(0, 0, std3D_framebuffer.w, std3D_framebuffer.h, 0, 0, refr.w, refr.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
	//glBindTexture(GL_TEXTURE_2D, refr.tex);
	//glGenerateMipmap(GL_TEXTURE_2D);
	std3D_PopDebugGroup();
}

void std3D_SendVerticesToHardware(void* vertices, uint32_t count, uint32_t stride)
{
	glBufferSubData(GL_ARRAY_BUFFER, 0, count * stride, vertices);
}

void std3D_SendIndicesToHardware(void* indices, uint32_t count, uint32_t stride)
{
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, count * stride, indices);
}

void std3D_AdvanceFrame()
{
	std3D_ResetState();
	bufferIdx = (bufferIdx + 1) % STD3D_STAGING_COUNT;
}

void std3D_SetState(std3D_DrawCallState* pState, uint32_t updateBits)
{
	uint32_t stage = std3D_GetStageIndex(pState);

	uint32_t reg_index = pState->shaderState.shader ? pState->shaderState.shader->regcount : 2;
	reg_index = (reg_index > 4) ? 2 : (reg_index > 2) ? 1 : 0;

	uint32_t uvindex = pState->textureState.maxTexcoord;

	std3D_worldStage* pStage = &worldStages[stage][reg_index][uvindex]; // todo: fixme

	//if(updateBits & RD_CACHE_SHADER)
	//if (updateBits & RD_CACHE_STATEBITS)
		std3D_BindStage(pStage);

	int last_tex = pState->textureState.pTexture ? pState->textureState.pTexture->texture_id : blank_tex_white;
//	if ((updateBits & RD_CACHE_RASTER) || (updateBits & RD_CACHE_STATEBITS))
		std3D_SetRasterState(pStage, pState);
	
//	if ((updateBits & RD_CACHE_FOG) || (updateBits & RD_CACHE_STATEBITS))
		std3D_SetFogState(pStage, pState);

//	if (updateBits & RD_CACHE_STATEBITS)
	{
		std3D_SetBlendState(pStage, pState);
		std3D_SetDepthStencilState(pState);
	}

//	if ((updateBits & RD_CACHE_TEXTURE) || (updateBits & RD_CACHE_STATEBITS))
	{
		std3D_SetTextureState(pStage, pState);
		std3D_SetMaterialState(pStage, pState);
	}

//	if ((updateBits & RD_CACHE_LIGHTING) || (updateBits & RD_CACHE_STATEBITS))
		std3D_SetLightingState(pStage, pState);

	//if ((updateBits & RD_CACHE_TRANSFORM))
		std3D_SetTransformState(pStage, pState);

//	if ((updateBits & RD_CACHE_SHADER_ID))
		std3D_SetShaderState(pStage, pState);
}

void std3D_DrawElements(rdGeoMode_t geoMode, uint32_t count, uint32_t offset, uint32_t stride)
{
	GLuint type = (stride == 2) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	glDrawElements(std3D_PrimitiveForGeoMode(geoMode + 1), count, type, (void*)(offset * stride));
}

// todo: get some half decent resampling/bilateral blurring going
void std3D_DoSSAO()
{
	std3D_PushDebugGroup("SSAO");

	// downscale the depth buffer with lower precision and resolution for sampling
	std3D_DrawSimpleTex(&std3D_texFboStage, &ssaoDepth, deferred.tex, 0, 0, 1.0, 1.0, 1.0, 0, "Z Downscale");

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// sampling
	{
		std3D_PushDebugGroup("Sampling");

		glBindFramebuffer(GL_FRAMEBUFFER, ssao.fbo);
		glUseProgram(std3D_ssaoStage[0].program);

		std3D_bindTexture(std3D_framebuffer.samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, std3D_framebuffer.ztex, 0);
		std3D_bindTexture(GL_TEXTURE_2D, ssaoDepth.tex, 1);
		std3D_bindTexture(GL_TEXTURE_2D, tiledrand_texture, 2);
		std3D_bindTexture(std3D_framebuffer.samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, std3D_framebuffer.tex0, 0);

		glUniform1i(std3D_ssaoStage[0].uniform_tex,  0);
		glUniform1i(std3D_ssaoStage[0].uniform_tex2, 1);
		glUniform1i(std3D_ssaoStage[0].uniform_tex3, 2);

		glUniform1i(std3D_ssaoStage[0].uniform_param1, (int)std3D_framebuffer.samples);

		glUniformMatrix4fv(std3D_ssaoStage[0].uniform_proj, 1, GL_FALSE, (float*)renderPassProj);

		glViewport(0, 0, ssao.w, ssao.h);
		glUniform2f(std3D_ssaoStage[0].uniform_iResolution, ssao.iw, ssao.ih);
	
		glDrawArrays(GL_TRIANGLES, 0, 3);

		std3D_PopDebugGroup();
	}
	
	// composite
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_SRC_COLOR);

		std3D_PushDebugGroup("Composite");

		glBindFramebuffer(GL_FRAMEBUFFER, std3D_framebuffer.fbo);
		glUseProgram(std3D_ssaoStage[1].program);

		std3D_bindTexture(std3D_framebuffer.samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, std3D_framebuffer.ztex, 0);
		std3D_bindTexture(GL_TEXTURE_2D, ssao.tex, 1);
		std3D_bindTexture(GL_TEXTURE_2D, tiledrand_texture, 2);

		glUniform1i(std3D_ssaoStage[1].uniform_tex, 0);
		glUniform1i(std3D_ssaoStage[1].uniform_tex2, 1);
		glUniform1i(std3D_ssaoStage[1].uniform_tex3, 2);

		glUniform1i(std3D_ssaoStage[1].uniform_param1, (int)std3D_framebuffer.samples);

		glUniformMatrix4fv(std3D_ssaoStage[1].uniform_proj, 1, GL_FALSE, (float*)renderPassProj);

		glViewport(0, 0, ssao.w, ssao.h);
		glUniform2f(std3D_ssaoStage[0].uniform_iResolution, ssao.iw, ssao.ih);

		glDrawArrays(GL_TRIANGLES, 0, 3);

		std3D_PopDebugGroup();
	}

	std3D_bindTexture(GL_TEXTURE_2D, 0, 0);
	std3D_bindTexture(GL_TEXTURE_2D, 0, 1);
	std3D_bindTexture(GL_TEXTURE_2D, 0, 2);

	std3D_PopDebugGroup();
}

void std3D_DoDeferredLighting()
{
	std3D_PushDebugGroup("Deferred Lighting");

	// enable depth testing to prevent running on empty pixels
	//glEnable(GL_DEPTH_TEST);
	//glDepthFunc(GL_GREATER);
	//glDepthMask(GL_FALSE);
	//glDisable(GL_CULL_FACE);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	glBindFramebuffer(GL_FRAMEBUFFER, deferred.fbo);

	//glClearColor(1, 1, 1, 1);
	//glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(std3D_deferredStage.program);

	std3D_bindTexture(std3D_framebuffer.samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, std3D_framebuffer.ztex, 0);
	//std3D_bindTexture(GL_TEXTURE_2D, std3D_framebuffer.ntex, 1);
	std3D_bindTexture(GL_TEXTURE_2D, tiledrand_texture, 2);

	glUniform1i(std3D_deferredStage.uniform_tex, 0);
	glUniform1i(std3D_deferredStage.uniform_tex2, 1);
	glUniform1i(std3D_deferredStage.uniform_tex3, 2);
	glUniform1i(std3D_deferredStage.uniform_tex4, 3);
	glUniform1i(std3D_deferredStage.uniform_lightbuf, TEX_SLOT_CLUSTER_BUFFER);

	glViewport(0, 0, deferred.w, deferred.h);
	glUniform2f(std3D_deferredStage.uniform_iResolution, deferred.iw, deferred.ih);

	glUniform1i(std3D_deferredStage.uniform_param1, (int)std3D_framebuffer.samples);

	//if (renderPassProj)
		glUniformMatrix4fv(std3D_deferredStage.uniform_proj, 1, GL_FALSE, (float*)renderPassProj);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	std3D_bindTexture(GL_TEXTURE_2D, 0, 0);
	std3D_bindTexture(GL_TEXTURE_2D, 0, 1);
	std3D_bindTexture(GL_TEXTURE_2D, 0, 2);

	std3D_PopDebugGroup();
}

void std3D_setupWorldTextures()
{
	std3D_bindTexture(GL_TEXTURE_2D, blank_tex_white, TEX_SLOT_TEX0);
	std3D_bindTexture(GL_TEXTURE_2D, blank_tex_white, TEX_SLOT_TEX1);
	std3D_bindTexture(GL_TEXTURE_2D, blank_tex_white, TEX_SLOT_TEX2);
	std3D_bindTexture(GL_TEXTURE_2D, blank_tex_white, TEX_SLOT_TEX3);
		
	//std3D_bindTexture(    GL_TEXTURE_2D,            blank_tex_white,         TEX_SLOT_DIFFUSE);
	//std3D_bindTexture(    GL_TEXTURE_2D,                  blank_tex,        TEX_SLOT_EMISSIVE);
	//std3D_bindTexture(    GL_TEXTURE_2D,                  blank_tex,    TEX_SLOT_DISPLACEMENT);
	std3D_bindTexture(    GL_TEXTURE_2D,           worldpal_texture,       TEX_SLOT_WORLD_PAL);
	std3D_bindTexture(    GL_TEXTURE_2D,    worldpal_lights_texture, TEX_SLOT_WORLD_LIGHT_PAL);
	std3D_bindTexture(GL_TEXTURE_BUFFER,                cluster_tbo,  TEX_SLOT_CLUSTER_BUFFER);
	std3D_bindTexture(    GL_TEXTURE_2D,          decalAtlasFBO.tex,     TEX_SLOT_DECAL_ATLAS);
	std3D_bindTexture(    GL_TEXTURE_2D, std3D_framebuffer.resolveZ,           TEX_SLOT_DEPTH);
	std3D_bindTexture(    GL_TEXTURE_2D,           blank_tex_white,               TEX_SLOT_AO);
	std3D_bindTexture(    GL_TEXTURE_2D,                  refr.tex,       TEX_SLOT_REFRACTION);
	std3D_bindTexture(    GL_TEXTURE_2D,                 blank_tex,             TEX_SLOT_CLIP);
	std3D_bindTexture(    GL_TEXTURE_2D,             phase_texture,            TEX_SLOT_PHASE);
	std3D_bindTexture(    GL_TEXTURE_1D,         blackbody_texture,        TEX_SLOT_BLACKBODY);
}

void std3D_FlushDeferred()
{
	if (std3D_framebufferFlags & FBO_SSAO)
	{
		STD_BEGIN_PROFILER_LABEL();
		std3D_PushDebugGroup("std3D_FlushDeferred");

		std3D_DoDeferredLighting();
		std3D_DoSSAO();

		std3D_PopDebugGroup();
		STD_END_PROFILER_LABEL();
	}
}


// writes directly to the final window framebuffer
void std3D_DoBloom()
{
	if (!(std3D_framebufferFlags & FBO_BLOOM))
		return;

	STD_BEGIN_PROFILER_LABEL();

	// todo: cvars
	const float bloom_intensity = 1.0f;// 1.0f;
	const float bloom_gamma = 1.0f;// 1.5f;
	const float blendLerp = 0.6f;
	const float uvScale = 1.0f; // debug for the kernel radius

	std3D_PushDebugGroup("std3D_DoBloom");

	// downscale layers using a simple gaussian filter
	std3D_DrawSimpleTex(&std3D_bloomStage, &bloomLayers[0], std3D_framebuffer.resolve1, 0, 0, uvScale, 1.0, 1.0, 0, "Bloom Downscale");
	for (int i = 1; i < NUM_BLOOM_LAYERS; ++i)
		std3D_DrawSimpleTex(&std3D_bloomStage, &bloomLayers[i], bloomLayers[i - 1].tex, 0, 0, uvScale, 1.0, 1.0, 0, "Bloom Downscale");

	// upscale layers and blend upward
	glEnable(GL_BLEND);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendFunc(GL_ONE, GL_ONE);

	for (int i = NUM_BLOOM_LAYERS - 2; i >= 0; --i)
		std3D_DrawSimpleTex(&std3D_bloomStage, &bloomLayers[i], bloomLayers[i + 1].tex, 0, 0, uvScale, blendLerp, 1.0, 0, "Bloom Upscale");

	std3D_PopDebugGroup();
	
	STD_END_PROFILER_LABEL();
}

void std3D_FlushPostFX()
{
	STD_BEGIN_PROFILER_LABEL();

	std3D_PushDebugGroup("std3D_FlushPostFX");

	glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
	//glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glViewport(0, 0, window.w, window.h);

	//if (!jkGame_isDDraw && !jkGuiBuildMulti_bRendering)
		//return;

//	glBindFramebuffer(GL_READ_FRAMEBUFFER, std3D_framebuffer.fbo);
//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, window.fbo);
//
//	glDrawBuffer(GL_COLOR_ATTACHMENT0);
//	glReadBuffer(GL_COLOR_ATTACHMENT0);
//
//	glBlitFramebuffer(0, 0, std3D_framebuffer.w, std3D_framebuffer.h, 0, 0, window.w, window.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

//	std3D_FlushDeferred();

	std3D_ResolveMSAA();

	std3D_DoBloom();

	glDisable(GL_BLEND);

#ifdef MOTION_BLUR
	std3D_DrawSimpleTex(&std3D_motionblurStage, &refr, std3D_framebuffer.resolve0, std3D_framebuffer.resolve2, 0, 1.0, 0, 1, 0, "Motion Blur 1st");
	std3D_DrawSimpleTex(&std3D_motionblurStage, &refrZ, refr.tex, std3D_framebuffer.resolve2, 0, 0.5, 1.0, 1.0, 0, "Motion Blur 2nd");
	std3D_DrawSimpleTex(&std3D_postfxStage, &window, refrZ.tex, bloomLayers[0].tex, 0, (rdCamera_pCurCamera->flags & 0x1) ? sithTime_curSeconds : -1.0, jkPlayer_enableDithering, jkPlayer_gamma, 0, "Final Output");
#else
	std3D_DrawSimpleTex(&std3D_postfxStage, &window, std3D_framebuffer.resolve0, (std3D_framebufferFlags & FBO_BLOOM) ? bloomLayers[0].tex : blank_tex, 0, (rdCamera_pCurCamera->flags & 0x1) ? sithTime_curSeconds : -1.0, jkPlayer_enableDithering, 1.0f/jkPlayer_gamma, 0, "Final Output");
#endif


	std3D_PopDebugGroup();

	STD_END_PROFILER_LABEL();
}

void std3D_SendLightsToHardware(rdClusterLight* lights, uint32_t lightOffset, uint32_t numLights)
{	
	glBindBuffer(GL_UNIFORM_BUFFER, light_ubo);

	std3D_LightUniformHeader header;
	header.firstLight = lightOffset;
	header.numLights = numLights;
	header.lightPad0 = header.lightPad1 = 0;

	// write the header
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_LightUniformHeader), &header);

	// write the array of lights
	glBufferSubData(GL_UNIFORM_BUFFER, sizeof(std3D_LightUniformHeader), sizeof(rdClusterLight) * STD3D_CLUSTER_MAX_LIGHTS, lights);
	
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void std3D_SendOccludersToHardware(rdClusterOccluder* occluders, uint32_t occluderOffset, uint32_t numOccluders)
{
	glBindBuffer(GL_UNIFORM_BUFFER, occluder_ubo);

	std3D_OccluderHeader header;
	header.firstOccluder = occluderOffset;
	header.numOccluders = numOccluders;
	header.occluderPad0 = header.occluderPad1 = 0;

	// write the header
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_OccluderHeader), &header);

	// write the array of occluders
	glBufferSubData(GL_UNIFORM_BUFFER, sizeof(std3D_OccluderHeader), sizeof(rdClusterOccluder) * STD3D_CLUSTER_MAX_OCCLUDERS, occluders);

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void std3D_SendDecalsToHardware(rdClusterDecal* decals, uint32_t decalOffset, uint32_t numDecals)
{
	glBindBuffer(GL_UNIFORM_BUFFER, decal_ubo);

	std3D_DecalHeader header;
	header.firstDecal = decalOffset;
	header.numDecals = numDecals;
	header.decalPad0 = header.decalPad1 = 0;

	// write the header
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std3D_DecalHeader), &header);

	// write the array of occluders
	glBufferSubData(GL_UNIFORM_BUFFER, sizeof(std3D_DecalHeader), sizeof(rdClusterDecal) * STD3D_CLUSTER_MAX_DECALS, decals);

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void std3D_SendClusterBitsToHardware(uint32_t* clusterBits, float znear, float zfar, uint32_t tileSizeX, uint32_t tileSizeY)
{
	float sliceScalingFactor = (float)STD3D_CLUSTER_GRID_SIZE_Z / logf(zfar / znear);
	float sliceBiasFactor = -((float)STD3D_CLUSTER_GRID_SIZE_Z * logf(znear) / logf(zfar / znear));

	// adjust tile sizes for msaa upscaling
	if (std3D_framebuffer.msaaMode == SAMPLE_2x1)
	{
		tileSizeX >>= 1;
	}
	else if (std3D_framebuffer.msaaMode == SAMPLE_2x2)
	{
		tileSizeX >>= 1;
		tileSizeY >>= 1;
	}

	rdVector_Set2(&sharedUniforms.clusterTileSizes, (float)tileSizeX, (float)tileSizeY);
	rdVector_Set2(&sharedUniforms.clusterScaleBias, sliceScalingFactor, sliceBiasFactor);

	glBindBuffer(GL_TEXTURE_BUFFER, cluster_buffer);
	glBufferSubData(GL_TEXTURE_BUFFER, 0, sizeof(uint32_t) * STD3D_CLUSTER_GRID_TOTAL_SIZE, (void*)clusterBits);
	glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void std3D_PurgeDecals()
{
	decalRootNode.children[0] = decalRootNode.children[1] = NULL;
	numAllocNodes = 0;
	if(decalHashTable)
	{
		stdHashTable_Free(decalHashTable);
		decalHashTable = 0;
	}
}

std3D_decalAtlasNode* std3D_AllocDecalNode()
{
	if (numAllocNodes >= (DECAL_ATLAS_SIZE/4)*(DECAL_ATLAS_SIZE/4))
	{
		stdPlatform_Printf("std3D: ERROR, Decal node pool is exhausted!\n");
		return NULL;
	}
	std3D_decalAtlasNode* node = &nodePool[numAllocNodes++];
	memset(node, 0, sizeof(std3D_decalAtlasNode));
	return node;
}

std3D_decalAtlasNode* std3D_InsertDecal(std3D_decalAtlasNode* parent, const rdRect* bound, rdDDrawSurface* tex)
{
	std3D_decalAtlasNode* newNode;
	if (parent->children[0]) // if not a leaf, insert into children
	{
		newNode = std3D_InsertDecal(parent->children[0], bound, tex);
		if (newNode)
			return newNode;

		return std3D_InsertDecal(parent->children[1], bound, tex);
	}
	else
	{
		// already have one
		if (parent->texture)
			return NULL;

		// doesn't fit
		if (parent->rect.width < bound->width || parent->rect.height < bound->height)
			return NULL;

		if (parent->rect.width == bound->width && parent->rect.height == bound->height)
		{
			sprintf_s(parent->name, 32, "decalTex%d", tex->texture_id);
			parent->texture = tex;
			return parent;
		}

		parent->children[0] = std3D_AllocDecalNode();
		parent->children[1] = std3D_AllocDecalNode();

		float dw = parent->rect.width - bound->width;
		float dh = parent->rect.height - bound->height;
		if (dw > dh)
		{
			parent->children[0]->rect = parent->rect;
			parent->children[1]->rect = parent->rect;
			parent->children[0]->rect.width = bound->width;
			parent->children[1]->rect.x = parent->rect.x + bound->width;
		}
		else
		{
			parent->children[0]->rect = parent->rect;
			parent->children[1]->rect = parent->rect;
			parent->children[0]->rect.height = bound->height;
			parent->children[1]->rect.y = parent->rect.y + bound->height;
		}
		return std3D_InsertDecal(parent->children[0], bound, tex);
	}
}

int std3D_UploadDecalTexture(rdRectf* out, stdVBuffer* vbuf, rdDDrawSurface* pTexture)
{
	if(!decalHashTable)
		decalHashTable = stdHashTable_New(256); // todo: move

	char tmpName[32];
	sprintf_s(tmpName, 32, "decalTex%d", pTexture->texture_id);

	int32_t index = -1;
	std3D_decalAtlasNode* findNode = (std3D_decalAtlasNode*)stdHashTable_GetKeyVal(decalHashTable, tmpName);
	if (findNode)
	{
		out->x = (float)findNode->rect.x / DECAL_ATLAS_SIZE;
		out->y = (float)findNode->rect.y / DECAL_ATLAS_SIZE;
		out->width = (float)findNode->rect.width / DECAL_ATLAS_SIZE;
		out->height = (float)findNode->rect.height / DECAL_ATLAS_SIZE;
		return 1;
	}
	else
	{
		rdRect rect;
		rect.x = 0;
		rect.y = 0;
		rect.width = vbuf->format.width;
		rect.height = vbuf->format.height;

		std3D_decalAtlasNode* node = std3D_InsertDecal(&decalRootNode, &rect, pTexture);
		if (node)
		{
			std3D_PushDebugGroup("std3D_InsertDecalTexture");

			glBindFramebuffer(GL_FRAMEBUFFER, decalAtlasFBO.fbo);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glDepthFunc(GL_ALWAYS);
			glDisable(GL_CULL_FACE);
			glDisable(GL_BLEND);
			glDepthMask(GL_FALSE);
			glDisable(GL_DEPTH_TEST);
			glUseProgram(std3D_decalAtlasStage.program);

			glBindVertexArray(vao);

			glActiveTexture(GL_TEXTURE0 + 0);
			glBindTexture(GL_TEXTURE_2D, pTexture->texture_id);
			glActiveTexture(GL_TEXTURE0 + 1);
			glBindTexture(GL_TEXTURE_2D, worldpal_texture);

			glUniform1i(std3D_decalAtlasStage.uniform_tex, 0);
			glUniform1i(std3D_decalAtlasStage.uniform_tex2, 1);
			glUniform1f(std3D_decalAtlasStage.uniform_param1, pTexture && pTexture->is_16bit ? TEX_MODE_16BPP : TEX_MODE_WORLDPAL);

			glViewport(node->rect.x, node->rect.y, node->rect.width, node->rect.height);

			glDrawArrays(GL_TRIANGLES, 0, 3);

			stdHashTable_SetKeyVal(decalHashTable, node->name, node);
	
			std3D_PopDebugGroup();
			
			out->x = (float)rect.x / DECAL_ATLAS_SIZE;
			out->y = (float)rect.y / DECAL_ATLAS_SIZE;
			out->width = (float)rect.width / DECAL_ATLAS_SIZE;
			out->height = (float)rect.height / DECAL_ATLAS_SIZE;
		}
		else
		{
			stdPlatform_Printf("std3D: ERROR, Decal texture atlas out of space!\n");
			return 0;
		}
	}
}

#endif