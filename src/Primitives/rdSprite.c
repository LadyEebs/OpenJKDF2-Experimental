#include "rdSprite.h"

#include "General/stdMath.h"
#include "General/stdString.h"
#include "Engine/rdroid.h"
#include "Raster/rdCache.h"
#include "Engine/rdClip.h"
#include "Engine/rdColormap.h"
#include "Primitives/rdPrimit3.h"
#include <math.h>

static rdVector3 rdSprite_inVerts[32];
static rdVector3 rdSprite_tmpVerts[32];

rdSprite* rdSprite_New(int type, char *fpath, char *materialFpath, flex_t width, flex_t height, int geometryMode, int lightMode, int textureMode, flex_t extraLight, rdVector3 *offset)
{
    rdSprite *sprite;

    sprite = (rdSprite *)rdroid_pHS->alloc(sizeof(rdSprite));
    if ( sprite )
    {
        rdSprite_NewEntry(sprite, fpath, type, materialFpath, width, height, geometryMode, lightMode, textureMode, extraLight, offset);
    }
    
    return sprite;
}

int rdSprite_NewEntry(rdSprite *sprite, char *spritepath, int type, char *material, flex_t width, flex_t height, rdGeoMode_t geometryMode, rdLightMode_t lightMode, rdTexMode_t textureMode, flex_t extraLight, rdVector3 *offset)
{
    if (spritepath)
    {
        stdString_SafeStrCopy(sprite->path, spritepath, 0x20);
    }
    sprite->width = width;
    sprite->type = type;
    sprite->height = height;
    sprite->offset = *offset;
    sprite->face.type = RD_FF_DOUBLE_SIDED | RD_FF_TEX_CLAMP_X | RD_FF_TEX_CLAMP_Y;
    sprite->face.geometryMode = geometryMode;
    sprite->face.lightingMode = lightMode;
    sprite->face.textureMode = textureMode;
    sprite->face.extraLight = extraLight;
    sprite->face.material = rdMaterial_Load(material, 0, 0);
#ifdef QOL_IMPROVEMENTS
	sprite->face.sortId = 0;
#endif
#ifdef DYNAMIC_POV
	sprite->id = -1;
#endif
    if ( sprite->face.material )
    {
        rdMaterial_EnsureDataForced(sprite->face.material); // Added: TWL
        sprite->face.numVertices = 4;
        sprite->face.vertexPosIdx = (int *)rdroid_pHS->alloc(sizeof(int) * sprite->face.numVertices);
        if ( sprite->face.vertexPosIdx )
        {
            if ( sprite->face.geometryMode <= RD_GEOMODE_SOLIDCOLOR)
            {
                for (int i = 0; i < sprite->face.numVertices; i++)
                {
                   sprite->face.vertexPosIdx[i] = i;
                }
            }
            else
            {
                sprite->face.vertexUVIdx = (int *)rdroid_pHS->alloc(sizeof(int) * sprite->face.numVertices);
                if ( !sprite->face.vertexUVIdx )
                    return 0;

                for (int i = 0; i < sprite->face.numVertices; i++)
                {
                   sprite->face.vertexPosIdx[i] = i;
                   sprite->face.vertexUVIdx[i] = i;
                }
                sprite->vertexUVs = (rdVector2 *)rdroid_pHS->alloc(sizeof(rdVector2) * sprite->face.numVertices);
                if ( !sprite->vertexUVs )
                    return 0;
                
                // Odd quirk: This requires the material be actually loaded
                // Added: nullptr fallbacks
                stdVBuffer* v24 = NULL;
                if (sprite->face.material->texinfos[0] && sprite->face.material->texinfos[0]->texture_ptr) {
                    v24 = sprite->face.material->texinfos[0]->texture_ptr->texture_struct[0];
                }
                int32_t width = v24 ? v24->format.width : 1; // Added: nullptr check and fallback
                int32_t height = v24 ? v24->format.height : 1; // Added: nullptr check and fallback

                sprite->vertexUVs[0].x = 0.5;
                sprite->vertexUVs[0].y = (flex_d_t)height - 0.5;
                sprite->vertexUVs[1].x = (flex_d_t)width - 0.5;
                sprite->vertexUVs[1].y = (flex_d_t)height - 0.5;
                sprite->vertexUVs[2].x = (flex_d_t)width - 0.5;
                sprite->vertexUVs[2].y = 0.5;
                sprite->vertexUVs[3].x = 0.5;
                sprite->vertexUVs[3].y = 0.5;
            }
            sprite->halfWidth = sprite->width * 0.5;
            sprite->halfHeight = sprite->height * 0.5;
            sprite->radius = stdMath_Sqrt(sprite->halfWidth * sprite->halfWidth + sprite->halfHeight * sprite->halfHeight);
            return 1;
        }
    }
    else {
        jk_printf("OpenJKDF2: Sprite `%s` is missing material.\n", sprite->path);
    }
    return 0;
}

void rdSprite_Free(rdSprite *sprite)
{
    if (sprite)
    {
        rdSprite_FreeEntry(sprite);
        rdroid_pHS->free(sprite);
    }
}

void rdSprite_FreeEntry(rdSprite *sprite)
{
    if (sprite->vertexUVs)
    {
        rdroid_pHS->free(sprite->vertexUVs);
        sprite->vertexUVs = 0;
    }
    if (sprite->face.vertexPosIdx)
    {
        rdroid_pHS->free(sprite->face.vertexPosIdx);
        sprite->face.vertexPosIdx = 0;
    }
    if (sprite->face.vertexUVIdx)
    {
        rdroid_pHS->free(sprite->face.vertexUVIdx);
        sprite->face.vertexUVIdx = 0;
    }
}

#ifdef RENDER_DROID2
int rdSprite_Draw(rdThing* thing, rdMatrix34* mat)
{
	rdSprite* sprite = thing->sprite3;

	rdVector3 vertex_out;
	rdMatrix_TransformPoint34(&vertex_out, &mat->scale, &rdCamera_pCurCamera->view_matrix);

	int clipResult;
	if (rdroid_curCullFlags & 2)
		clipResult = rdClip_SphereInFrustum(rdCamera_pCurCamera->pClipFrustum, &vertex_out, sprite->radius);
	else
		clipResult = thing->clippingIdk;

	if (clipResult == 2)
		return 0;

	rdMatrix44 viewMatrix;
	rdGetMatrix(&viewMatrix, RD_MATRIX_VIEW);

	// vertices are already in view space
	rdMatrixMode(RD_MATRIX_VIEW);
	rdIdentity();

	rdMatrixMode(RD_MATRIX_MODEL);
	rdIdentity();

#ifdef MOTION_BLUR
	rdMatrix44 viewMatrixPrev;
	rdGetMatrix(&viewMatrixPrev, RD_MATRIX_VIEW_PREV);

	rdMatrixMode(RD_MATRIX_VIEW_PREV); // fixme
	rdIdentity();
	rdMatrixMode(RD_MATRIX_MODEL_PREV);
	rdIdentity();
#endif

	rdGeoMode_t curGeometryMode_ = rdroid_curGeometryMode;
	rdLightMode_t curLightingMode_ = rdroid_curLightingMode;
	rdTexMode_t curTextureMode_ = rdroid_curTextureMode;

	if (curGeometryMode_ >= sprite->face.geometryMode)
		curGeometryMode_ = sprite->face.geometryMode;

	if (curGeometryMode_ >= thing->curGeoMode)
		curGeometryMode_ = thing->curGeoMode;
	else if (rdroid_curGeometryMode >= sprite->face.geometryMode)
		curGeometryMode_ = sprite->face.geometryMode;
	else
		curGeometryMode_ = rdroid_curGeometryMode;

	rdSetGeoMode(curGeometryMode_);

	if (curLightingMode_ >= sprite->face.lightingMode)
		curLightingMode_ = sprite->face.lightingMode;

	if (curLightingMode_ >= thing->curLightMode)
		curLightingMode_ = thing->curLightMode;
	else if (curLightingMode_ < sprite->face.lightingMode)
		curLightingMode_ = rdroid_curLightingMode;

	rdSetLightMode(curLightingMode_);

	if (curTextureMode_ >= sprite->face.textureMode)
		curTextureMode_ = sprite->face.textureMode;

	if (curTextureMode_ >= sprite->face.textureMode)
		curTextureMode_ = sprite->face.textureMode;
	else
		curTextureMode_ = rdroid_curTextureMode;
	rdSetTexMode(curTextureMode_);

#ifdef RGB_AMBIENT
	if (rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT)
		rdAmbientLight(rdCamera_pCurCamera->ambientLight.x, rdCamera_pCurCamera->ambientLight.y, rdCamera_pCurCamera->ambientLight.z);
	else
		rdAmbientLight(0, 0, 0);
#else
	if (rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT)
		rdAmbientLight(rdCamera_pCurCamera->ambientLight, rdCamera_pCurCamera->ambientLight, rdCamera_pCurCamera->ambientLight);
	else
		rdAmbientLight(0, 0, 0);
#endif

	rdSortOrder(sprite->face.sortId);
	rdSortDistance(vertex_out.y);
		rdSetShader(0);

	rdBindMaterial(sprite->face.material, thing->wallCel);

	// temp, make this controllable
	rdSetGlowIntensity(0.8f);

	int oldZ = rdroid_curZBufferMethod;

	float alpha = 1.0f;
	if ((sprite->face.type & RD_FF_TEX_TRANSLUCENT) != 0)
	{
		alpha = 90.0f / 255.0f;
		rdSetBlendEnabled(RD_TRUE);
		rdSetBlendMode(RD_BLEND_SRCALPHA, RD_BLEND_INVSRCALPHA);
		rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
	}
	else if (sprite->face.type & RD_FF_ADDITIVE)
	{
		rdSetBlendEnabled(RD_TRUE);
		rdSetBlendMode(RD_BLEND_ONE, RD_BLEND_ONE);
		rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
	}
	else if (sprite->face.type & RD_FF_SCREEN)
	{
		rdSetBlendEnabled(RD_TRUE);
		rdSetBlendMode(RD_BLEND_ONE, RD_BLEND_INVSRCCOLOR);
		rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
	}
	else
	{
		rdSetBlendEnabled(RD_FALSE);
		rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
	}

	extern int jkPlayer_enableTextureFilter;
	rdTexFilterMode(!jkPlayer_enableTextureFilter || (sprite->face.type & RD_FF_TEX_FILTER_NEAREST) ? RD_TEXFILTER_NEAREST : RD_TEXFILTER_BILINEAR);
	rdTexClampMode(RD_FF_TEX_CLAMP_X, RD_FF_TEX_CLAMP_Y);

	rdTexOffseti(RD_TEXCOORD0, sprite->face.clipIdk.x, sprite->face.clipIdk.y);

	float halfWidth = sprite->halfWidth * thing->spriteScale;
	float halfHeight = sprite->halfHeight * thing->spriteScale;

	rdSprite_inVerts[0].x = sprite->offset.x - halfWidth + vertex_out.x;
	rdSprite_inVerts[1].y = sprite->offset.y + vertex_out.y;
	rdSprite_inVerts[1].z = sprite->offset.z - halfHeight + vertex_out.z;
	rdSprite_inVerts[2].x = halfWidth + sprite->offset.x + vertex_out.x;
	rdSprite_inVerts[2].y = sprite->offset.y + vertex_out.y;
	rdSprite_inVerts[2].z = sprite->offset.z + halfHeight + vertex_out.z;
	rdSprite_inVerts[0].y = sprite->offset.y + vertex_out.y;
	rdSprite_inVerts[0].z = sprite->offset.z - halfHeight + vertex_out.z;
	rdSprite_inVerts[3].x = sprite->offset.x - halfWidth + vertex_out.x;
	rdSprite_inVerts[1].x = halfWidth + sprite->offset.x + vertex_out.x;
	rdSprite_inVerts[3].y = sprite->offset.y + vertex_out.y;
	rdSprite_inVerts[3].z = sprite->offset.z + halfHeight + vertex_out.z;

	rdVector3 tint = { 1,1,1 };
	//if (thing->parentSithThing->sector != sithCamera_currentCamera->sector)
		//tint = thing->parentSithThing->sector->tint;

	rdVector3 halfTint;
	halfTint.x = tint.x * 0.5f;
	halfTint.y = tint.y * 0.5f;
	halfTint.z = tint.z * 0.5f;

	tint.x -= (halfTint.z + halfTint.y);
	tint.y -= (halfTint.x + halfTint.y);
	tint.z -= (halfTint.x + halfTint.z);

	float s, c;
	if (thing->spriteRot != 0.0)
		stdMath_SinCos(thing->spriteRot, &s, &c);

	rdVector3 cmpTint = { 1,1,1 };
	if (pCurThing->parentSithThing && pCurThing->parentSithThing->sector)
		cmpTint = pCurThing->parentSithThing->sector->colormap->tint;

	rdVector3 rgb;
	if (sprite->face.type & RD_FF_VERTEX_COLORS)
	{
		rgb.x = thing->color.x * tint.x + thing->color.x;
		rgb.y = thing->color.x * tint.y + thing->color.x;
		rgb.z = thing->color.x * tint.z + thing->color.x;
	}
	else
	{
		rgb.x = tint.x + 1.0f;
		rgb.y = tint.y + 1.0f;
		rgb.z = tint.z + 1.0f;
	}
	rdColor4f(rgb.x * cmpTint.x, rgb.y * cmpTint.y, rgb.z * cmpTint.z, alpha);

	if (rdBeginPrimitive(RD_PRIMITIVE_POLYGON))
	{
		for (int i = 0; i < sprite->face.numVertices; ++i)
		{
			if (sprite->vertexUVs)
			{
				rdVector2 uv = sprite->vertexUVs[i];
				// todo: make this transform a rdroid thing
				if (thing->spriteRot != 0.0)
				{
					stdVBuffer* v24 = sprite->face.material->texinfos[0]->texture_ptr->texture_struct[0];

					rdVector2 uvCentered;
					uvCentered.x = uv.x - (float)v24->format.width / 2.0f;
					uvCentered.y = uv.y - (float)v24->format.height / 2.0f;

					uv.x = c * uvCentered.x - s * uvCentered.y;
					uv.y = s * uvCentered.x + c * uvCentered.y;

					uv.x += (float)v24->format.width / 2.0f;
					uv.y += (float)v24->format.height / 2.0f;
				}
				rdTexCoord2i(RD_TEXCOORD0, uv.x, uv.y);
			}
			rdVertex3v(&rdSprite_inVerts[i].x);
		}
		rdEndPrimitive();
	}

	rdSortOrder(0);
	rdSortDistance(0);
	rdTexOffseti(RD_TEXCOORD0, 0, 0);
	rdMatrixMode(RD_MATRIX_VIEW);
	rdLoadMatrix(&viewMatrix);
#ifdef MOTION_BLUR
	rdMatrixMode(RD_MATRIX_VIEW_PREV);
	rdLoadMatrix(&viewMatrixPrev);
#endif
	rdSetZBufferMethod(oldZ);
	rdTexClampMode(0, 0);
	rdSetBlendEnabled(RD_FALSE);
	rdSetBlendMode(RD_BLEND_ONE, RD_BLEND_ZERO);

	rdSetGlowIntensity(0.4f);

	return 1;
}
#else
int rdSprite_Draw(rdThing *thing, rdMatrix34 *mat)
{
    rdProcEntry *procEntry;
    rdVector2 *vertexUVs;
    int geometryMode;
    int textureMode;
    int clipResult;
    rdVector3 vertex_out;
    rdMeshinfo mesh_out;
    rdMeshinfo mesh_in;

    rdSprite *sprite = thing->sprite3;
    rdMatrix_TransformPoint34(&vertex_out, &mat->scale, &rdCamera_pCurCamera->view_matrix);
    if ( rdroid_curCullFlags & 2 )
        clipResult = rdClip_SphereInFrustum(rdCamera_pCurCamera->pClipFrustum, &vertex_out, sprite->radius);
    else
        clipResult = thing->clippingIdk;

    if ( clipResult == 2 )
        return 0;

    procEntry = rdCache_GetProcEntry();
    if (!procEntry)
        return 0;

    mesh_in.numVertices = sprite->face.numVertices;
    mesh_in.vertexPosIdx = sprite->face.vertexPosIdx;
    mesh_in.vertexUVIdx = sprite->face.vertexUVIdx;
    mesh_in.vertices = rdSprite_inVerts;
    mesh_in.paDynamicLight = 0;
    mesh_in.vertexUVs = sprite->vertexUVs;
    mesh_in.intensities = 0;
    mesh_out.vertices = rdSprite_tmpVerts;
    mesh_out.verticesOrig = procEntry->vertices;
    mesh_out.vertexUVs = procEntry->vertexUVs;
    mesh_out.paDynamicLight = procEntry->vertexIntensities;
    rdSprite_inVerts[0].x = sprite->offset.x - sprite->halfWidth + vertex_out.x;
    rdSprite_inVerts[1].y = sprite->offset.y + vertex_out.y;
    rdSprite_inVerts[1].z = sprite->offset.z - sprite->halfHeight + vertex_out.z;
    rdSprite_inVerts[2].x = sprite->halfWidth + sprite->offset.x + vertex_out.x;
    rdSprite_inVerts[2].y = sprite->offset.y + vertex_out.y;
    rdSprite_inVerts[2].z = sprite->offset.z + sprite->halfHeight + vertex_out.z;
    rdSprite_inVerts[0].y = sprite->offset.y + vertex_out.y;
    rdSprite_inVerts[0].z = sprite->offset.z - sprite->halfHeight + vertex_out.z;
    rdSprite_inVerts[3].x = sprite->offset.x - sprite->halfWidth + vertex_out.x;
    rdSprite_inVerts[1].x = sprite->halfWidth + sprite->offset.x + vertex_out.x;
    rdSprite_inVerts[3].y = sprite->offset.y + vertex_out.y;
    rdSprite_inVerts[3].z = sprite->offset.z + sprite->halfHeight + vertex_out.z;

    rdGeoMode_t curGeometryMode_ = rdroid_curGeometryMode;
    rdLightMode_t curLightingMode_ = rdroid_curLightingMode;
    rdTexMode_t curTextureMode_ = rdroid_curTextureMode;
    if ( curGeometryMode_ >= sprite->face.geometryMode )
        curGeometryMode_ = sprite->face.geometryMode;
    if ( curGeometryMode_ >= thing->curGeoMode )
    {
        procEntry->geometryMode = thing->curGeoMode;
    }    
    else if ( rdroid_curGeometryMode >= sprite->face.geometryMode )
    {
        procEntry->geometryMode = sprite->face.geometryMode;
    }
    else
    {
        procEntry->geometryMode = rdroid_curGeometryMode;
    }
    
    procEntry->geometryMode = procEntry->geometryMode;
#ifdef RGB_AMBIENT
	if (rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT && rdCamera_pCurCamera->ambientLight.x >= 1.0 && rdCamera_pCurCamera->ambientLight.y >= 1.0 && rdCamera_pCurCamera->ambientLight.z >= 1.0)
#else
    if ( rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT && rdCamera_pCurCamera->ambientLight >= 1.0 )
#endif
    {
        procEntry->lightingMode = RD_LIGHTMODE_FULLYLIT;
    }
    else
    {
        if ( curLightingMode_ >= sprite->face.lightingMode )
            curLightingMode_ = sprite->face.lightingMode;
        if ( curLightingMode_ >= thing->curLightMode )
        {
            sprite->face.lightingMode = thing->curLightMode;
        }
        else if ( rdroid_curLightingMode < sprite->face.lightingMode )
        {
            sprite->face.lightingMode = rdroid_curLightingMode;
        }
        procEntry->lightingMode = sprite->face.lightingMode;
    }

    if ( curTextureMode_ >= sprite->face.textureMode )
        curTextureMode_ = sprite->face.textureMode;
    
    procEntry->textureMode = thing->curTexMode;
    if ( curTextureMode_ < procEntry->textureMode )
    {
        if ( curTextureMode_ >= sprite->face.textureMode )
            procEntry->textureMode = sprite->face.textureMode;
        else
            procEntry->textureMode = rdroid_curTextureMode;
    }

    if ( clipResult )
        rdPrimit3_ClipFace(
            rdCamera_pCurCamera->pClipFrustum,
            procEntry->geometryMode,
            procEntry->lightingMode,
            procEntry->textureMode,
            &mesh_in,
            &mesh_out,
            &sprite->face.clipIdk);
    else
        rdPrimit3_NoClipFace(procEntry->geometryMode, procEntry->lightingMode, procEntry->textureMode, &mesh_in, &mesh_out, &sprite->face.clipIdk);
    if ( mesh_out.numVertices < 3u )
        return 0;

#ifdef VIEW_SPACE_GBUFFER
	memcpy(procEntry->vertexVS, mesh_out.vertices, sizeof(rdVector3) * mesh_out.numVertices);
#endif
    rdCamera_pCurCamera->fnProjectLst(mesh_out.verticesOrig, mesh_out.vertices, mesh_out.numVertices);

#ifdef RGB_AMBIENT
	if (rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT)
		rdVector_Copy3(&procEntry->ambientLight, &rdCamera_pCurCamera->ambientLight);
	else
		rdVector_Zero3(&procEntry->ambientLight);
#else
    if ( rdroid_curRenderOptions & RD_USE_AMBIENT_LIGHT)
        procEntry->ambientLight = rdCamera_pCurCamera->ambientLight;
    else
        procEntry->ambientLight = 0.0;
#endif

    // Software renderer optimizations
#ifndef TARGET_TWL
    if ( procEntry->lightingMode )
    {
#ifdef RGB_AMBIENT
		if (procEntry->ambientLight.x < 1.0 || procEntry->ambientLight.y < 1.0 || procEntry->ambientLight.z < 1.0)
#else
		if ( procEntry->ambientLight < 1.0 )
#endif
        {
            if ( procEntry->lightingMode == 2 )
            {
                if ( procEntry->light_level_static < 1.0 || rdColormap_pCurMap != rdColormap_pIdentityMap )
                {
                    if ( procEntry->light_level_static <= 0.0 )
                        procEntry->lightingMode = 1;
                }
                else
                {
                    procEntry->lightingMode = 0;
                }
            }
            else if ( USES_VERTEX_LIGHTING(procEntry->lightingMode) )
            {
                int lightIdx;
                for (lightIdx = 1; lightIdx < mesh_out.numVertices; lightIdx++)
                {
                    if ( procEntry->vertexIntensities[lightIdx] != procEntry->vertexIntensities[0] )
                        break;
                }
                if ( lightIdx == mesh_out.numVertices )
                {
                    if ( procEntry->vertexIntensities[0] == 1.0 )
                    {
                        if ( rdColormap_pCurMap == rdColormap_pIdentityMap )
                        {
                            procEntry->lightingMode = 0;
                        }
                        else
                        {
                            procEntry->lightingMode = 2;
                            procEntry->light_level_static = 1.0;
                        }
                    }
                    else if ( procEntry->vertexIntensities[0] == 0.0 )
                    {
                        procEntry->lightingMode = 1;
                        procEntry->light_level_static = 0.0;
                    }
                    else
                    {
                        procEntry->lightingMode = 2;
                        procEntry->light_level_static = procEntry->vertexIntensities[0];
                    }
                }
            }
        }
        else
        {
            procEntry->lightingMode = rdColormap_pCurMap == rdColormap_pIdentityMap ? 0 : 2;
        }
    }
#else
    if ( procEntry->lightingMode == 3 )
    {
        procEntry->light_level_static = *procEntry->vertexIntensities;
    }
#endif

    int procFlags = 1;
    if ( procEntry->geometryMode >= 4 )
        procFlags = 3;
    if ( procEntry->lightingMode >= 3 )
        procFlags |= 4u;

#ifdef QOL_IMPROVEMENTS
	procEntry->sortId = sprite->face.sortId;
#endif

    procEntry->light_flags = 0;
    procEntry->wallCel = thing->wallCel;
    procEntry->type = sprite->face.type;
    procEntry->extralight = sprite->face.extraLight;
    procEntry->material = sprite->face.material;

	int extraData = 0;
#ifdef STENCIL_BUFFER
	extraData |= 2; // mark stencil buffer
#endif
#ifdef VERTEX_COLORS
	rdVector_Copy3(&procEntry->color, &thing->color);
#endif

	// Added: Sprites should always be drawn
    rdMaterial_EnsureDataForced(procEntry->material);

    rdCache_AddProcFace(extraData, mesh_out.numVertices, procFlags);

    return 1;
}
#endif