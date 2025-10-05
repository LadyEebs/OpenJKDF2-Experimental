#include "sithRender.h"

#include <math.h>
#include <float.h>

#include "Cog/sithCog.h"
#include "Main/sithMain.h"
#include "World/sithMaterial.h"
#include "World/sithModel.h"
#include "Engine/sithKeyFrame.h"
#include "Engine/rdMaterial.h"
#include "Engine/rdKeyframe.h"
#include "Engine/rdColormap.h"
#include "Engine/rdroid.h"
#include "Gameplay/sithTime.h"
#include "Engine/sithCamera.h"
#include "Raster/rdCache.h"
#include "Engine/rdClip.h"
#include "Engine/rdCamera.h"
#include "Engine/sithRenderSky.h"
#include "General/stdMath.h"
#include "Raster/rdFace.h"
#include "Primitives/rdModel3.h"
#include "Primitives/rdPrimit3.h"
#include "World/jkPlayer.h"
#include "Gameplay/sithPlayer.h"
#include "World/sithSector.h"
#include "World/sithWorld.h"
#include "World/sithExplosion.h"
#include "Platform/std3D.h"
#include "stdPlatform.h"
#include "Modules/std/stdProfiler.h"
#ifdef PUPPET_PHYSICS
#include "Modules/sith/Engine/sithRagdoll.h"
#include "Modules/sith/Engine/sithConstraint.h"
#include "Engine/sithPuppet.h"
#include "World/sithSoundClass.h"
#endif
#ifdef JOB_SYSTEM
#include "Modules/std/stdJob.h"
#endif

#ifdef JOB_SYSTEM
#define USE_LIGHT_JOBS
#endif

#include "Modules/rdroid/Engine/rdCluster.h"

#ifdef TARGET_TWL
void sithRender_NoClip(sithSector* sector, rdClipFrustum* frustumArg, flex_t a3, int depth);
#endif

#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
#include "World/sithDecal.h"
#endif

#ifdef RENDER_DROID2
#include "Modules/sith/World/sithShader.h"
#endif

#ifdef QOL_IMPROVEMENTS
#include "General/stdFont.h"

#if 0
static rdThing* lightDebugThing = NULL;
static rdModel3* lightDebugThing_model3 = NULL;
static rdMatrix34 lightDebugThing_mat;
static int lightDebugNum = 0;
#endif
#ifdef RGB_AMBIENT
#if 0
static rdThing* ambientDebugThing = NULL;
static rdModel3* ambientDebugThing_model3 = NULL;
static rdMatrix34 ambientDebugThing_mat;
#endif
#endif

sithRender_weapRendFunc_t sithRender_weaponRenderOpaqueHandle;
sithRender_weapRendFunc_t sithRender_weaponRenderAlphaHandle;

uint32_t sithRender_numSkyPortals = 0;

#ifdef JKM_LIGHTING
int sithRender_008d4094 = 0;
flex_t sithRender_008d4098 = 0.0;
flex_t sithRender_008d409c = 0.0;
#endif

int sithRender_008d1668 = 0;

// Added: safeguard
int sithRender_adjoinSafeguard = 0;

#ifdef QOL_IMPROVEMENTS
sithThing* sithRender_alphaDrawThing = NULL; // list of things to render after with zwrite off
#endif

#ifdef RENDER_DROID2
rdAmbientFlags_t sithRender_aoFlags = 0;
uint32_t sithRender_numStaticLights = 0;

rdShader* sithRender_defaultShader = NULL;
rdShader* sithRender_specularShader = NULL;
rdShader* sithRender_horizonSky = NULL;
rdShader* sithRender_ceilingSky = NULL;
rdShader* sithRender_jkgmShader = NULL;
rdShader* sithRender_waterShader = NULL;
rdShader* sithRender_scopeShader = NULL;
#endif

#ifdef RGB_THING_LIGHTS
void sithRender_GetSaberLightColor(rdVector3* outColor, sithThing* thing)
{
	rdVector_Set3(outColor, 1.0f, 1.0f, 1.0f);
	if (thing->playerInfo && thing->playerInfo->polyline.tipFace.material)
		rdMaterial_GetFillColor(outColor, thing->playerInfo->polyline.tipFace.material, thing->sector->colormap, 0, -1);
}
#endif

void sithRender_DebugDrawThingName(sithThing* pThing)
{
#ifndef TILE_SW_RASTER // TILETODO
	rdVector3 viewPos;
	rdMatrix_TransformPoint34(&viewPos, &pThing->position, &rdCamera_pCurCamera->view_matrix);

	int clipResult = rdClip_SphereInFrustrum(rdCamera_pCurCamera->pClipFrustum, &viewPos, 0.05f);
	if (clipResult != 2)
	{
		// todo: custom font for debugging
		stdFont* debugFont = jkHud_pMsgFontSft;

		rdVector3 projPos;
		rdCamera_pCurCamera->fnProject(&projPos, &viewPos);

		char tmpText[1024];
		snprintf(&tmpText, 1024, "%d: %s", pThing->thingIdx, pThing->template_name);
		stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

		if (jkPlayer_showThingInfo > 1)
		{
			int fontHeight = stdFont_GetHeight(debugFont) + debugFont->marginY;

			// general info
			if (jkPlayer_showThingInfo == 2)
			{
				projPos.y += fontHeight * jkPlayer_hudScale;
				snprintf(&tmpText, 1024, "signature: %d", pThing->signature);
				stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				
				projPos.y += fontHeight * jkPlayer_hudScale;
				snprintf(&tmpText, 1024, "type: %s", sithThing_aTypes[pThing->type]);
				stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

				projPos.y += fontHeight * jkPlayer_hudScale;
				snprintf(&tmpText, 1024, "move type: %s", sithThing_aMoveTypes[pThing->moveType]);
				stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

				//projPos.y += fontHeight * jkPlayer_hudScale;
				//snprintf(&tmpText, 1024, "control type: %s", sithThing_aControlTypes[pThing->controlType]);
				//stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

				projPos.y += fontHeight * jkPlayer_hudScale;
				snprintf(&tmpText, 1024, "collide type: %s", sithThing_aCollideTypes[pThing->collide]);
				stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

				if (pThing->sector)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "sector: %d", pThing->sector->id);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}

				if (pThing->lifeLeftMs > 0)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "life left: %d ms", pThing->lifeLeftMs);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
			}
			// resource info
			else if (jkPlayer_showThingInfo == 3)
			{
				if (pThing->animclass)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "anim class: %s", pThing->animclass->name);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
				if (pThing->soundclass)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "sound class: %s", pThing->soundclass->snd_fname);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
				if (pThing->pAIClass)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "ai class: %s", pThing->pAIClass->fpath);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
				if (pThing->rdthing.type)
				{
					tmpText[0] = '\0';

					projPos.y += fontHeight * jkPlayer_hudScale;
					switch(pThing->rdthing.type)
					{
					case RD_THINGTYPE_MODEL:
						snprintf(&tmpText, 1024, "model: %s", pThing->rdthing.model3->filename);
						break;

					case RD_THINGTYPE_SPRITE3:
						snprintf(&tmpText, 1024, "sprite: %s", pThing->rdthing.sprite3->path);
						break;

					case RD_THINGTYPE_PARTICLECLOUD:
						if(pThing->rdthing.particlecloud->name[0] != '\0')	
							snprintf(&tmpText, 1024, "particle: %s", pThing->rdthing.particlecloud->name);
						break;

					case RD_THINGTYPE_POLYLINE:
						snprintf(&tmpText, 1024, "polyline: %s", pThing->rdthing.polyline->fname);
						break;

					default:
						break;
					};
					if (tmpText[0] != '\0')
						stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
			}
			// cog info
			else if (jkPlayer_showThingInfo == 4 && pThing->thingflags & SITH_TF_CAPTURED)
			{
				if(pThing->class_cog)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, "class cog:", 1, jkPlayer_hudScale);
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "\t%d: %s", pThing->class_cog->selfCog, pThing->class_cog->cogscript_fpath);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
				if (pThing->capture_cog)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, "capture cog:", 1, jkPlayer_hudScale);
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "\t%d: %s", pThing->capture_cog->selfCog, pThing->capture_cog->cogscript_fpath);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}

				projPos.y += fontHeight * jkPlayer_hudScale;
				stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, "used in cogs:", 1, jkPlayer_hudScale);
				for (int i = 0; i < sithCog_numThingLinks; i++)
				{
					sithCogThingLink* v15 = &sithCog_aThingLinks[i];
					if (v15->thing == pThing && v15->signature == pThing->signature)
					{
						projPos.y += fontHeight * jkPlayer_hudScale;
						snprintf(&tmpText, 1024, "\t%d: % s", v15->cog->selfCog, v15->cog->cogscript_fpath);
						stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
					}
				}
			}
			// physics info
			else if (jkPlayer_showThingInfo == 5)
			{
				if(pThing->moveType == SITH_MT_PHYSICS)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "mass:\t% .3f", pThing->physicsParams.mass);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

	#ifdef PUPPET_PHYSICS
					if (pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
					{
						projPos.y += fontHeight * jkPlayer_hudScale;
						snprintf(&tmpText, 1024, "inertia:\t% .3f", pThing->physicsParams.inertia);
						stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
					}
	#endif

					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(&tmpText, 1024, "vel:\t(% .3f / % .3f / % .3f)", pThing->physicsParams.vel.x, pThing->physicsParams.vel.y, pThing->physicsParams.vel.z);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				
					projPos.y += fontHeight * jkPlayer_hudScale;
	#ifdef PUPPET_PHYSICS
					if (pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
						snprintf(&tmpText, 1024, "rotvel:\t(% .3f / % .3f / % .3f)", pThing->physicsParams.rotVel.x, pThing->physicsParams.rotVel.y, pThing->physicsParams.rotVel.z);
					else
						snprintf(&tmpText, 1024, "angvel:\t(% .3f / % .3f / % .3f)", pThing->physicsParams.angVel.x, pThing->physicsParams.angVel.y, pThing->physicsParams.angVel.z);
	#endif
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
#ifdef PUPPET_PHYSICS
				// draw joint things as well
				if (pThing->moveType == SITH_MT_RAGDOLL && pThing->animclass && pThing->animclass->ragdoll && pThing->ragdoll)
				{
					uint64_t jointBits = pThing->animclass->ragdoll->physicsJointBits;
					while (jointBits != 0)
					{
						int jointIdx = stdMath_FindLSB64(jointBits);
						jointBits ^= 1ull << jointIdx;
						sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
						sithRender_DebugDrawThingName(&pJoint->thing);
					}
				}
#endif
			}
			// AI info
			else if (jkPlayer_showThingInfo == 6 && pThing->controlType == SITH_CT_AI)
			{
				if (pThing->actor)
				{
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(tmpText, 1024,"ai class: %s", pThing->actor->pAIClass->fpath);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
					
					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(tmpText, 1024, "flags: 0x%x", pThing->actor->flags);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

					//projPos.y += fontHeight * jkPlayer_hudScale;
					//snprintf(tmpText, 1024, "moods: %d/%d/%d", pThing->actor->mood0, pThing->actor->mood1, pThing->actor->mood2);
					//stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);

					projPos.y += fontHeight * jkPlayer_hudScale;
					snprintf(tmpText, 1024, "next update: %d ms", pThing->actor->nextUpdate - sithTime_curMs);
					stdFont_DrawAsciiGPU(debugFont, projPos.x, projPos.y, 999, tmpText, 1, jkPlayer_hudScale);
				}
			}
		}
	}
#endif
}

void sithRender_RenderDebugLight(flex_t intensity, rdVector3* pos)
{
#if 0
    rdVector3 scale_test;

    //intensity *= 10.0;
    
    scale_test.x = intensity * 2.0;
    scale_test.y = intensity * 2.0;
    scale_test.z = intensity * 2.0;

    lightDebugThing_model3->radius = 0;
    lightDebugThing->lightingMode = 0;
    lightDebugThing->geosetSelect = 0;
    lightDebugThing_model3->geosetSelect = 0;
    
    /*if (intensity == 1.0)
    {
        scale_test.x = intensity * 2.0;
    }*/
    
    rdMatrix_Identity34(&lightDebugThing_mat);
    rdMatrix_PreScale34(&lightDebugThing_mat, &scale_test);
    
    //printf("light %u: %f %f %f, %f\n", lightDebugNum++, pos->x, pos->y, pos->z, intensity);
    rdVector_Copy3(&lightDebugThing_mat.scale, pos);
    rdThing_Draw(lightDebugThing, &lightDebugThing_mat);
#endif
}

// Added: pulled this out of the thing render loop to add FP legs
int sithRender_ShouldRenderCameraThing(sithThing* thing)
{
	int isFirstPerson = (sithCamera_currentCamera->cameraPerspective & 0xFC) == 0;
	int isFocusThing = thing == sithCamera_currentCamera->primaryFocus;
	int isFirstPersonThing = isFirstPerson && isFocusThing;

#ifndef FP_LEGS
	return !isFirstPersonThing;
#else
	thing->rdthing.hiddenJoint = -1;
	thing->rdthing.hideWeaponMesh = 0;

	// if cam is 1st person and a player is focused, hide the upper body
	if (isFirstPersonThing)
	{
		if (thing->type == SITH_THING_PLAYER)
		{
			thing->rdthing.hideWeaponMesh = 1;

			sithAnimclass* animclass = thing->animclass;
			if (animclass)
			{
				int jointIdx = animclass->bodypart_to_joint[JOINTTYPE_TORSO]; // torso
				if (jointIdx >= 0)
				{
					if (thing->rdthing.model3 && jointIdx < thing->rdthing.model3->numHierarchyNodes)
						thing->rdthing.hiddenJoint = jointIdx;
				}

				return 1;
			}
		}
	}

	return !isFirstPersonThing;
#endif
}

void sithRender_RenderDebugLights()
{
    sithSector *sectorIter; // edx
    //rdLight **lightIter; // ebx
    //rdLight **curCamera_lights; // edi
    int *verticeIdxs; // edx
    rdLight **lightIter2; // edi
    //unsigned int v24; // [esp+8h] [ebp-13Ch]
    sithSector **aSectorIter; // [esp+Ch] [ebp-138h]
    flex_t attenuationMax; // [esp+40h] [ebp-104h]
    rdLight *tmpLights[RDCAMERA_MAX_LIGHTS]; // [esp+44h] [ebp-100h] BYREF

    if (!sithRender_numSectors)
        return;

    aSectorIter = sithRender_aSectors;
    for (int k = 0; k < sithRender_numSectors; k++)
    {
        sectorIter = aSectorIter[k];
        
        //lightIter = tmpLights;
        //curCamera_lights = rdCamera_pCurCamera->lights;
        
        sithRender_RenderDebugLight(1.0, &sectorIter->center);
        
        //v24 = 0;
        for (int i = 0; i < rdCamera_pCurCamera->numLights; i++)
        {
            sithRender_RenderDebugLight(rdCamera_pCurCamera->lights[i]->intensity, &rdCamera_pCurCamera->lightPositions[i]);
        
            /*flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[i], &sectorIter->center);
            if ( (*curCamera_lights)->falloffMin + sectorIter->radius > distCalc)
            {
                *lightIter++ = *curCamera_lights;
                ++v24;
            }
            ++curCamera_lights;*/
        }

        /*verticeIdxs = sectorIter->verticeIdxs;
        for (int j = 0; j < sectorIter->numVertices; j++)
        {
            int idx = *verticeIdxs;
            if ( sithWorld_pCurrentWorld->alloc_unk9c[idx] != sithRender_lastRenderTick )
            {
                sithWorld_pCurrentWorld->verticesDynamicLight[idx] = 0.0;
                lightIter2 = tmpLights;
                for (int i = 0; i < v24; i++)
                {
                    int id = (*lightIter2)->id;
                    flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[id], &sithWorld_pCurrentWorld->vertices[idx]);
                    if ( distCalc < (*lightIter2)->falloffMax )
                        sithWorld_pCurrentWorld->verticesDynamicLight[idx] = (*lightIter2)->intensity - distCalc * rdCamera_pCurCamera->attenuationMax + sithWorld_pCurrentWorld->verticesDynamicLight[idx];
                    if ( sithWorld_pCurrentWorld->verticesDynamicLight[idx] >= 1.0 )
                        break;
                    ++lightIter2;
                }
                sithWorld_pCurrentWorld->alloc_unk9c[idx] = sithRender_lastRenderTick;
            }
            verticeIdxs++;
        }*/
    }
}
#endif

#ifdef RGB_AMBIENT

void sithRender_RenderDebugAmbient(rdVector3* pos)
{
#if 0
	ambientDebugThing->curLightMode = RD_LIGHTMODE_SPECULAR;
	ambientDebugThing->geosetSelect = 0;
	ambientDebugThing_model3->geosetSelect = 0;
	ambientDebugThing->frameTrue = 0;

	rdMatrix_Identity34(&ambientDebugThing_mat);
	rdVector_Copy3(&ambientDebugThing_mat.scale, pos);
	rdThing_Draw(ambientDebugThing, &ambientDebugThing_mat);
#endif
}

void sithRender_RenderDebugAmbientCubes()
{
#if 0
	if (!sithRender_numSectors)
		return;
	sithRender_numLights = 0;
	rdCamera_ClearLights(rdCamera_pCurCamera);

	for (int k = 0; k < sithRender_numSectors; k++)
	{
		sithSector* sectorIter = sithRender_aSectors[k];

		rdVector3 ambientRGB;
		ambientRGB.x = 0.01f;
		ambientRGB.y = 0.01f;
		ambientRGB.z = 0.01f;
		rdCamera_SetAmbientLight(rdCamera_pCurCamera, &ambientRGB);
		rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &sectorIter->ambientSH);

		sithRender_RenderDebugAmbient(&sectorIter->center);//.dominantDir);
	}
	rdCache_Flush();
#endif
}

#endif

int sithRender_Startup()
{
    rdMaterial_RegisterLoader(sithMaterial_LoadEntry);
    rdModel3_RegisterLoader(sithModel_LoadEntry);
    rdKeyframe_RegisterLoader(sithKeyFrame_LoadEntry);
    sithRender_flag = 0;
#ifdef QOL_IMPROVEMENTS
	sithRender_weaponRenderOpaqueHandle = 0;
	sithRender_weaponRenderAlphaHandle = 0;
#else
    sithRender_weaponRenderHandle = 0;
#endif


    return 1;
}

// MOTS altered
int sithRender_Open()
{
    sithRender_geoMode = RD_GEOMODE_TEXTURED;
#ifdef RENDER_DROID2
	sithRender_lightMode = RD_LIGHTMODE_SUBSURFACE;
#elif defined(SPECULAR_LIGHTING)
	sithRender_lightMode = RD_LIGHTMODE_SPECULAR;
#else
    sithRender_lightMode = RD_LIGHTMODE_GOURAUD;
#endif
    sithRender_texMode = RD_TEXTUREMODE_PERSPECTIVE;

    for (int i = 0; i < SITHREND_NUM_LIGHTS; i++)
    {
        rdLight_NewEntry(&sithRender_aLights[i]);
    }

    rdColormap_SetCurrent(sithWorld_pCurrentWorld->colormaps);
    rdColormap_SetIdentity(sithWorld_pCurrentWorld->colormaps);

    sithRenderSky_Open(sithWorld_pCurrentWorld->horizontalPixelsPerRev, sithWorld_pCurrentWorld->horizontalDistance, sithWorld_pCurrentWorld->ceilingSky);

#ifdef RENDER_DROID2
	sithRender_defaultShader = sithShader_LoadEntry("default.asm");
	sithRender_specularShader = sithShader_LoadEntry("specular.asm");
	sithRender_horizonSky = sithShader_LoadEntry("horizonsky.asm");
	sithRender_ceilingSky = sithShader_LoadEntry("ceilingsky.asm");
	sithRender_jkgmShader = sithShader_LoadEntry("jkgm.asm");
	sithRender_waterShader = sithShader_LoadEntry("water.asm");
	sithRender_scopeShader = sithShader_LoadEntry("scope.asm");
#endif

    sithRender_lightingIRMode = 0; 
    sithRender_needsAspectReset = 0;

#ifdef JKM_LIGHTING
    // MOTS added
    sithRender_008d4094 = 0;
    sithRender_008d4098 = 0.0;
    sithRender_008d409c = 0.0;
#endif
    
#ifdef QOL_IMPROVEMENTS
#if 0
    // Added: Light debug
    lightDebugThing = rdThing_New(NULL);
    if (!lightDebugThing_model3)
        lightDebugThing_model3 = rdModel3_New("3d0\\lamp.3do");
    rdThing_SetModel3(lightDebugThing, lightDebugThing_model3);
    rdMatrix_Identity34(&lightDebugThing_mat);
#endif
#endif

#ifdef RGB_AMBIENT
#if 0
	ambientDebugThing = rdThing_New(NULL);
	if (!ambientDebugThing_model3)
		ambientDebugThing_model3 = rdModel3_New("dflt2.3DO");
	rdThing_SetModel3(ambientDebugThing, ambientDebugThing_model3);
	rdMatrix_Identity34(&ambientDebugThing_mat);
#endif
#endif
    
    return 1;
}

void sithRender_Close()
{
    // Added: Light debug
    //rdModel3_Free(lightDebugThing_model3); // TODO figure out weird free issues
    //rdThing_Free(lightDebugThing);

#ifdef RGB_AMBIENT
#if 0
	rdModel3_Free(ambientDebugThing_model3);
	rdThing_Free(ambientDebugThing);
#endif
#endif

#ifdef RENDER_DROID2
	sithRender_defaultShader = 0;
	sithRender_specularShader = 0;
	sithRender_horizonSky = 0;
	sithRender_ceilingSky = 0;
	sithRender_jkgmShader = 0;
	sithRender_waterShader = 0;
	sithRender_scopeShader = 0;
#endif

    sithRenderSky_Close();
}

void sithRender_Shutdown()
{
    ;
}

void sithRender_SetSomeRenderflag(int flag)
{
    sithRender_flag = flag;
}

int sithRender_GetSomeRenderFlag()
{
    return sithRender_flag;
}

void sithRender_EnableIRMode(flex_t a, flex_t b)
{
    sithRender_lightingIRMode = 1;
    sithRender_f_83198C = stdMath_Clamp(a, 0.0, 1.0);
    sithRender_f_831990 = stdMath_Clamp(b, 0.0, 1.0);
}

void sithRender_DisableIRMode()
{
    sithRender_lightingIRMode = 0;
}

void sithRender_SetGeoMode(rdGeoMode_t geoMode)
{
    sithRender_geoMode = geoMode;
}

void sithRender_SetLightMode(rdLightMode_t lightMode)
{
    sithRender_lightMode = lightMode;
}

void sithRender_SetTexMode(rdTexMode_t texMode)
{
    sithRender_texMode = texMode;
}

void sithRender_SetPalette(const void *palette)
{
    rdColormap_SetCurrent(sithWorld_pCurrentWorld->colormaps);
    rdColormap_SetIdentity(sithWorld_pCurrentWorld->colormaps);
    if ( rdroid_curAcceleration > 0 )
    {
        sithMaterial_UnloadAll();
#ifndef TILE_SW_RASTER
        std3D_UnloadAllTextures();
#endif
        std3D_SetCurrentPalette((rdColor24 *)palette, 90);
    }
}

void sithRender_DrawSurface(sithSurface* surface);

#ifdef RENDER_DROID2
void sithRender_SetCameraFog()
{
#ifdef FOG
	rdFogLightDir(sithWorld_pCurrentWorld->fogLightDir.x, sithWorld_pCurrentWorld->fogLightDir.y, sithWorld_pCurrentWorld->fogLightDir.z);

	if (sithCamera_currentCamera->sector->flags & SITH_SECTOR_UNDERWATER)
	{
		rdVector4 fog = { sithCamera_currentCamera->sector->tint.x, sithCamera_currentCamera->sector->tint.y, sithCamera_currentCamera->sector->tint.z, 1.0f };
		
		rdSetFogMode(RD_FOG_ENABLED);

		rdVector3 halfFog;
		halfFog.x = fog.x * 0.5f;
		halfFog.y = fog.y * 0.5f;
		halfFog.z = fog.z * 0.5f;

		fog.x = fog.x - (halfFog.z + halfFog.y);
		fog.y = fog.y - (halfFog.x + halfFog.y);
		fog.z = fog.z - (halfFog.x + halfFog.z);		
		
		rdFogColorf(fog.x, fog.y, fog.z, fog.w);
		rdFogRange(0.0f, 5.0f);
		rdFogAnisotropy(0.35f);
	}
	else
	{
		if (sithWorld_pCurrentWorld->fogEnabled) // user fog is enabled
		{
			rdSetFogMode(RD_FOG_ENABLED);
			rdFogColorf(sithWorld_pCurrentWorld->fogColor.x, sithWorld_pCurrentWorld->fogColor.y, sithWorld_pCurrentWorld->fogColor.z, sithWorld_pCurrentWorld->fogColor.w);
			rdFogRange(sithWorld_pCurrentWorld->fogStartDepth, sithWorld_pCurrentWorld->fogEndDepth);
			rdFogAnisotropy(0.35f);
		}
		else if (sithSurface_hasSkyColorGuess)
		{
			float mapDist = rdVector_Dist3(&sithWorld_pCurrentWorld->maxBounds, &sithWorld_pCurrentWorld->minBounds);

			rdSetFogMode(RD_FOG_ENABLED);
			rdFogColorf(sithSurface_skyColorGuess.x, sithSurface_skyColorGuess.y, sithSurface_skyColorGuess.z, 1.0f);
			rdFogRange(0, mapDist);//sithCamera_currentCamera->rdCam.pClipFrustum->zFar);
			rdFogAnisotropy(0.35f);
		}
		else
		{
			rdSetFogMode(RD_FOG_DISABLED);
		}
	}
#endif
}

void sithRender_DrawBackdrop()
{
	rdCache_Flush("sithRender_DrawBackdrop:Start");

	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);

	rdSetCullFlags(0);
	rdStencilMode(1);
	rdStencilBit(1);

	rdMatrix34 viewOriginal = rdCamera_pCurCamera->view_matrix;
	sithSector* origSector = sithCamera_currentCamera->sector;

	sithSector* centerSector = sithWorld_pCurrentWorld->backdropSector;
	while (centerSector)
	{
		if (!(centerSector->flags & SITH_SECTOR_DRAW_AS_3DO))
			break;
		centerSector = centerSector->nextBackdropSector;
	}

	if (!centerSector)
		centerSector = sithWorld_pCurrentWorld->backdropSector;

	if (!centerSector)
		return;

	sithCamera_currentCamera->sector = centerSector;

	rdMatrix34 backdropCamMat = rdCamera_camMatrix;
	backdropCamMat.scale = centerSector->center;
	rdMatrix_InvertOrtho34(&rdCamera_pCurCamera->view_matrix, &backdropCamMat);

	rdMatrixMode(RD_MATRIX_VIEW);
	rdLoadMatrix34(&rdCamera_pCurCamera->view_matrix);
	
	rdMatrixMode(RD_MATRIX_MODEL);
	rdIdentity();

#ifdef MOTION_BLUR
	rdMatrixMode(RD_MATRIX_VIEW_PREV);
	rdLoadMatrix34(&rdCamera_pCurCamera->view_matrix);

	rdMatrixMode(RD_MATRIX_MODEL_PREV);
	rdIdentity();
#endif

	sithSector* backdropSector = sithWorld_pCurrentWorld->backdropSector;
	while (backdropSector)
	{
		rdColormap_SetCurrent(backdropSector->colormap);

		sithRender_UpdateLights(backdropSector, 0.0f, 0.0, 0);

		sithSurface* surface = backdropSector->surfaces;
		for (int v75 = 0; v75 < backdropSector->numSurfaces; ++surface, v75++)
		{
			if (!surface->surfaceInfo.face.geometryMode)
				continue;
			sithRender_DrawSurface(surface);
		}

		int safeguard = 0;
		for (sithThing* pThing = backdropSector->thingsList; pThing; pThing = pThing->nextThing)
		{
			if (++safeguard >= SITH_MAX_THINGS)
				break;

			if (pThing->thingflags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_WILLBEREMOVED))
				continue;

			if (pThing->rdthing.type != RD_THINGTYPE_MODEL)
				continue;

			if (((pThing->rdthing).type == RD_THINGTYPE_MODEL))
				pThing->rdthing.model3->geosetSelect = 0;

			rdMatrix_TransformPoint34(&pThing->screenPos, &pThing->position, &rdCamera_pCurCamera->view_matrix);
			pThing->rdthing.clippingIdk = rdClip_SphereInFrustrum(rdCamera_pCurCamera->pClipFrustum, &pThing->screenPos, pThing->rdthing.model3->radius);
			if (pThing->rdthing.clippingIdk == 2)
				continue;

			sithRender_RenderThing(pThing);
		}

		backdropSector = backdropSector->nextBackdropSector;
	}

	rdStencilMode(0);
	rdStencilBit(0);

	sithCamera_currentCamera->sector = origSector;
	rdCamera_pCurCamera->view_matrix.scale = viewOriginal.scale;

	rdCache_Flush("sithRender_DrawBackdrop:End");
}

void sithRender_ResetState()
{
	rdMatrixMode(RD_MATRIX_PROJECTION);
	rdIdentity();
	rdPerspective(rdCamera_pCurCamera->fov, rdCamera_pCurCamera->screenAspectRatio, rdCamera_pCurCamera->pClipFrustum->zNear, rdCamera_pCurCamera->pClipFrustum->zFar);

	rdMatrixMode(RD_MATRIX_MODEL);
	rdIdentity();

	rdMatrixMode(RD_MATRIX_VIEW);
	rdLoadMatrix34(&rdCamera_pCurCamera->view_matrix);

#ifdef MOTION_BLUR
	rdMatrixMode(RD_MATRIX_MODEL_PREV);
	rdIdentity();

	rdMatrixMode(RD_MATRIX_VIEW_PREV);
	rdLoadMatrix34(&rdCamera_pCurCamera->view_matrix);
#endif

	sithRender_SetCameraFog();

	rdSetCullFlags(1);
}


void sithRender_RenderOccludersAndDecals()
{
	if (!jkPlayer_enableShadows && !jkPlayer_enableDecals)
		return;

	// iterate all visible things and add their occluder volumes
	for (int i = 0; i < sithRender_numSectors2; i++)
	{
		sithSector* sector = sithRender_aSectors2[i];

		int safeguard = 0;
		for (sithThing* thingIter = sector->thingsList; thingIter; thingIter = thingIter->nextThing)
		{
			if (++safeguard >= SITH_MAX_THINGS)
				break;

			if ((thingIter->thingflags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_WILLBEREMOVED)))
				continue;

			if (thingIter->rdthing.type != RD_THINGTYPE_MODEL && thingIter->rdthing.type != RD_THINGTYPE_DECAL)
				continue;

			if (thingIter->rdthing.type == RD_THINGTYPE_MODEL && !jkPlayer_enableShadows)
				continue;
			else if (thingIter->rdthing.type == RD_THINGTYPE_DECAL && !jkPlayer_enableDecals)
				continue;

			// todo: cache me
			rdMatrix_TransformPoint34(&thingIter->screenPos, &thingIter->position, &rdCamera_pCurCamera->view_matrix);

			// todo: cache me toooo
			flex_t radius = thingIter->rdthing.model3->radius;
			int clippingVal = rdClip_SphereInFrustrum(sector->clipFrustum, &thingIter->screenPos, radius);

			thingIter->rdthing.clippingIdk = clippingVal;
			if (clippingVal == 2 || sithRender_008d1668) // MoTS added: sithRender_008d1668
				continue;

			thingIter->lookOrientation.scale = thingIter->position;
			if (thingIter->rdthing.type == RD_THINGTYPE_MODEL)
				rdModel3_DrawOccluders(&thingIter->rdthing, &thingIter->lookOrientation);
			else
				rdDecal_Draw(&thingIter->rdthing, &thingIter->lookOrientation);
			rdVector_Zero3(&thingIter->lookOrientation.scale);
		}
	}
}

int sithRender_AddSurfaceLightAt(sithSurface* surface, const rdVector3* pos, flex_t radius, flex_t intensity)
{
	rdMaterial_GetFillColor(&sithRender_aLights[sithRender_numLights].color, surface->surfaceInfo.face.material, surface->parent_sector->colormap, surface->surfaceInfo.face.wallCel, -1);
	
	rdVector3 offset;
	rdVector_Scale3(&offset, &surface->surfaceInfo.face.normal, radius * 0.01);
	
	rdVector3 center;
	rdVector_Add3(&center, pos, &offset);
	
	sithRender_aLights[sithRender_numLights].intensity = intensity;// / rdCamera_pCurCamera->attenuationMin;
	sithRender_aLights[sithRender_numLights].direction = surface->surfaceInfo.face.normal;
	rdLight_SetAngles(&sithRender_aLights[sithRender_numLights], 175.0f, 180.0f);
	
	rdCamera_AddLightExplicitRadius(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], radius, &center);
	sithRender_aLights[sithRender_numLights].type = RD_LIGHT_SPOTLIGHT; // spot
	return ++sithRender_numLights;
}

int sithRender_AddSurfaceLight(sithSurface* surface)
{
	rdMaterial_GetFillColor(&sithRender_aLights[sithRender_numLights].color, surface->surfaceInfo.face.material, surface->parent_sector->colormap, surface->surfaceInfo.face.wallCel, -1);

	float radius = fmin(surface->localSize.x, surface->localSize.y) * 2.0 * M_PI;// fmax(surface->radius, 0.025f);
	//float radius = surface->radius * 2.0;
	//float radius = stdMath_Sqrt(surface->localSize.x * surface->localSize.x + surface->localSize.y * surface->localSize.y) * 2.0;

	rdVector3 offset;
	rdVector_Scale3(&offset, &surface->surfaceInfo.face.normal, radius * 0.001f);
	
	rdVector3 center;
	rdVector_Add3(&center, &surface->center, &offset);
	
	sithRender_aLights[sithRender_numLights].intensity = 1.5;
	rdCamera_AddLightExplicitRadius(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], radius, &center);
	
	sithRender_aLights[sithRender_numLights].type      = RD_LIGHT_RECTANGLE; // rectangle
	sithRender_aLights[sithRender_numLights].width     = surface->localSize.x - 0.05f;
	sithRender_aLights[sithRender_numLights].height    = surface->localSize.y - 0.05f;
	sithRender_aLights[sithRender_numLights].right     = surface->tangent;
	sithRender_aLights[sithRender_numLights].up        = surface->bitangent;
	sithRender_aLights[sithRender_numLights].direction = surface->surfaceInfo.face.normal;
	
	return ++sithRender_numLights;
	
	//float aspectX = stdMath_Floor(surface->localSize.x / surface->localSize.y);
	//float aspectY = stdMath_Floor(surface->localSize.y / surface->localSize.x);
	//
	//if (aspectX == 1 && aspectY == 1)
	//{
	//	sithRender_AddSurfaceLightAt(surface, &surface->center, surface->radius * 2.0f, 1.0f);
	//}
	//else if (aspectX > aspectY)
	//{
	//	for (float i = -aspectX/2.0; i < aspectX/2.0; ++ i)
	//	{
	//		rdVector3 deltaStep;
	//		rdVector_Scale3(&deltaStep, &surface->tangent, surface->localSize.x * 2.0f * i / aspectX);
	//
	//		rdVector3 pos;
	//		rdVector_Add3(&pos, &surface->center, &deltaStep);
	//
	//		sithRender_AddSurfaceLightAt(surface, &pos, surface->localSize.y * M_PI, 1.0f);// / aspectX);
	//	}
	//}
	//else
	//{
	//	for (float i = -aspectY / 2.0; i < aspectY / 2.0; ++i)
	//	{
	//		rdVector3 deltaStep;
	//		rdVector_Scale3(&deltaStep, &surface->bitangent, surface->localSize.y * i / aspectY);
	//
	//		rdVector3 pos;
	//		rdVector_Add3(&pos, &surface->center, &deltaStep);
	//
	//		sithRender_AddSurfaceLightAt(surface, &pos, surface->localSize.x * 2.0f * M_PI, 1.0f);// / aspectY);
	//	}
	//}
	
	return sithRender_numLights;

	//float radius = fmax(surface->radius, 0.025f);
	//
	//rdVector3 offset;
	//rdVector_Scale3(&offset, &surface->surfaceInfo.face.normal, radius * 0.01);
	//
	//rdVector3 center;
	//rdVector_Add3(&center, &surface->center, &offset);
	//
	//sithRender_aLights[sithRender_numLights].intensity = 1.0;// / rdCamera_pCurCamera->attenuationMin;
	//sithRender_aLights[sithRender_numLights].direction = surface->surfaceInfo.face.normal;
	//rdLight_SetAngles(&sithRender_aLights[sithRender_numLights], 0.0f, 180.0f);
	//
	//rdCamera_AddLightExplicitRadius(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], radius * 2.0, &center);
	//sithRender_aLights[sithRender_numLights].type = RD_LIGHT_SPOTLIGHT; // spot
	//return ++sithRender_numLights;
}

#endif

#if defined(FOG) && defined(TILE_SW_RASTER)
void sithRender_SetCameraFog()
{
	if (sithCamera_currentCamera->sector->flags & SITH_SECTOR_UNDERWATER)
	{
		rdVector4 fog = { sithCamera_currentCamera->sector->tint.x, sithCamera_currentCamera->sector->tint.y, sithCamera_currentCamera->sector->tint.z, 1.0f };

		rdVector3 halfFog;
		halfFog.x = fog.x * 0.5f;
		halfFog.y = fog.y * 0.5f;
		halfFog.z = fog.z * 0.5f;

		fog.x = fog.x - (halfFog.z + halfFog.y);
		fog.y = fog.y - (halfFog.x + halfFog.y);
		fog.z = fog.z - (halfFog.x + halfFog.z);

		rdSetFog(1, &fog, 0.0f, 5.0f);
	}
	else
	{
		rdVector4 fog = { sithSurface_skyColorGuess.x, sithSurface_skyColorGuess.y, sithSurface_skyColorGuess.z, 1 };
		rdSetFog(1, &fog, 0.0f, sithCamera_currentCamera->rdCam.pClipFrustum->zFar);
	}
}
#endif

// todo: in order to do refractions better, we need to split the frame into before and after water surfaces
void sithRender_Draw()
{
    sithSector *v2; // edi
    sithSector *v4; // eax
    flex_t a2; // [esp+0h] [ebp-28h]
    flex_t v7; // [esp+8h] [ebp-20h]
    flex_t v9; // [esp+8h] [ebp-20h]
    flex_t a3; // [esp+1Ch] [ebp-Ch] BYREF
    flex_t a4; // [esp+24h] [ebp-4h] BYREF

    //printf("%x %x %x\n", sithRender_texMode, rdroid_curTextureMode, sithRender_lightMode);

    //lightDebugNum = 0; // Added

#ifdef TARGET_TWL
    //sithRender_geoMode = RD_GEOMODE_TEXTURED;
    //sithRender_lightMode = RD_LIGHTMODE_FULLYLIT;
    //sithRender_texMode = RD_TEXTUREMODE_PERSPECTIVE;
    //rdroid_curVertexColorMode = 0;
#endif

    // Keeping this here in case I need to check for weird corruption again
#if 0
    for (int i = 0; i < sithWorld_pCurrentWorld->numThingsLoaded; i++)
    {
        sithThing* v16 = &sithWorld_pCurrentWorld->things[i];
        if (v16->moveType == SITH_MT_PATH)
        {
            if (v16->trackParams.loadedFrames < 0) {
                stdPlatform_Printf("OpenJKDF2: Track thing 0x%x %s has corrupted loadedFrames %x %x\n", i, v16->template_name, v16->trackParams.loadedFrames, v16->trackParams.sizeFrames);
            }
        }
    }

    for (int i = 0; i < sithWorld_pCurrentWorld->numTemplatesLoaded; i++)
    {
        sithThing* v16 = &sithWorld_pCurrentWorld->templates[i];
        if (v16->moveType == SITH_MT_PATH)
        {
            if (v16->trackParams.loadedFrames < 0 || v16->trackParams.sizeFrames <= 0) {
                stdPlatform_Printf("OpenJKDF2: Template track thing 0x%x %s has corrupted loadedFrames %x %x\n", i, v16->template_name, v16->trackParams.loadedFrames, v16->trackParams.sizeFrames);
            }
        }
    }
#endif

    sithRenderSky_Update();
    if (!sithRender_geoMode)
        return;

    rdSetGeometryMode(sithRender_geoMode);
    if ( sithRender_lightingIRMode )
        rdSetLightingMode(2);
    else
        rdSetLightingMode(sithRender_lightMode);
    rdSetTextureMode(sithRender_texMode);
    rdSetRenderOptions(rdGetRenderOptions() | 2);

    // Somehow backface culling on models got unset...?
#ifdef QOL_IMPROVEMENTS
    rdSetRenderOptions(rdGetRenderOptions() | 1);
#endif

    if (!sithCamera_currentCamera || !sithCamera_currentCamera->sector)
        return;

	STD_BEGIN_PROFILER_LABEL();

    sithPlayer_SetScreenTint(sithCamera_currentCamera->sector->tint.x, sithCamera_currentCamera->sector->tint.y, sithCamera_currentCamera->sector->tint.z);

	sithCamera_currentCamera->rdCam.flags &= ~0x1;
    // TODO: Verify this is expensive
#ifndef TARGET_TWL
    if ( (sithCamera_currentCamera->sector->flags & 2) != 0 )
    {
        flex_t fov = sithCamera_currentCamera->fov;
        flex_t aspect = sithCamera_currentCamera->aspectRatio;

#ifdef QOL_IMPROVEMENTS
        fov = jkPlayer_fov;
        aspect = sithMain_lastAspect;
#endif
        stdMath_SinCos(sithTime_curSeconds * 70.0, &a3, &a4);
        rdCamera_SetFOV(&sithCamera_currentCamera->rdCam, a3 + fov);
        stdMath_SinCos(sithTime_curSeconds * 100.0, &a3, &a4);
        rdCamera_SetAspectRatio(&sithCamera_currentCamera->rdCam, a3 * 0.016666668 + aspect);
        sithRender_needsAspectReset = 1;
		sithCamera_currentCamera->rdCam.flags |= 0x1;
    }
    else if ( sithRender_needsAspectReset )
    {
        rdCamera_SetFOV(&sithCamera_currentCamera->rdCam, sithCamera_currentCamera->fov);
        rdCamera_SetAspectRatio(&sithCamera_currentCamera->rdCam, sithCamera_currentCamera->aspectRatio);
        sithRender_needsAspectReset = 0;
    }
#endif

    rdSetSortingMethod(0);
    rdSetMipDistances(&sithWorld_pCurrentWorld->mipmapDistance);
    rdSetCullFlags(1);
    sithRender_numSectors = 0;
    sithRender_numSectors2 = 0;
    sithRender_numLights = 0;
    sithRender_numClipFrustums = 0;
    sithRender_numSurfaces = 0;
    sithRender_82F4B4 = 0;
    sithRender_sectorsDrawn = 0;
    sithRender_nongeoThingsDrawn = 0;
    sithRender_geoThingsDrawn = 0;
	sithRender_numSkyPortals = 0;
    rdCamera_ClearLights(rdCamera_pCurCamera);
	
	// todo: get this out of here
	extern int Window_xSize;
	extern int Window_ySize;

	int32_t tex_w = (int32_t)((flex_d_t)Window_xSize * jkPlayer_ssaaMultiple);
	int32_t tex_h = (int32_t)((flex_d_t)Window_ySize * jkPlayer_ssaaMultiple);
	tex_w = (tex_w < 320 ? 320 : tex_w);
	tex_h = tex_w * (flex_t)Window_ySize / Window_xSize;

#ifdef RENDER_DROID2
	rdViewport(0, 0, tex_w, tex_h);

	sithRender_numStaticLights = 0;

	rdDepthRange(0.05f, 1.0f);
	rdSetGlowIntensity(0.4f);

	extern flex_t jkPlayer_overbright;
	rdSetOverbright(jkPlayer_overbright);

	_memset(sithWorld_pCurrentWorld->lightBuckets, 0, sizeof(uint64_t)*sithWorld_pCurrentWorld->numLightBuckets);

	sithRender_aoFlags = (jkPlayer_enableShadows ? RD_AMBIENT_OCCLUDERS : 0) | (jkPlayer_enableSSAO ? RD_AMBIENT_SCREEN_SPACE : 0);
	rdSetDecalMode(jkPlayer_enableDecals ? RD_DECALS_ENABLED : RD_DECALS_DISABLED);
	rdDitherMode(jkPlayer_enableDithering ? RD_DITHER_4x4 : RD_DITHER_NONE);
	rdAmbientFlags(sithRender_aoFlags);
	rdCluster_Clear();

	sithRender_ResetState();
#endif

#if defined(FOG) && defined(TILE_SW_RASTER)
	sithRender_SetCameraFog();
#endif

    //printf("------\n");
    sithRender_adjoinSafeguard = 0; // Added: safeguard

    // Added: noclip
    if (!(g_debugmodeFlags & DEBUGFLAG_NOCLIP)) {
        sithPlayer_bNoClippingRend = 0;
    }

#ifdef TARGET_TWL
    int testClip = stdPlatform_GetTimeMsec();
#endif
    // TWL: 26ms
    // Added: noclip
    if (!sithPlayer_bNoClippingRend) {
        sithRender_Clip(sithCamera_currentCamera->sector, rdCamera_pCurCamera->pClipFrustum, 0.0, 0);
    }
    else {
        // TODO: Basic view sphere clipping at least?
        for (int i = 0; i < sithWorld_pCurrentWorld->numSectors; i++)
        {
            sithRender_Clip(&sithWorld_pCurrentWorld->sectors[i], rdCamera_pCurCamera->pClipFrustum, 0.0, 0);
        }
    }
#ifdef TARGET_TWL
    int testClipEnd = stdPlatform_GetTimeMsec();
#endif

#ifdef TARGET_TWL
    int testLights = stdPlatform_GetTimeMsec();
#endif
    // TWL: 0ms
    sithRender_UpdateAllLights();
    
    if ( (sithRender_flag & 2) != 0 )
        sithRender_RenderDynamicLights();

#ifdef RENDER_DROID2
	sithRender_RenderOccludersAndDecals();

	sithRender_ResetState();

	// all cluster stuff should be ready now, build it
	rdMatrix44 proj;
	rdGetMatrix(&proj, RD_MATRIX_PROJECTION);
	rdCluster_Build(&proj, tex_w, tex_h);
#endif

#ifdef JKM_LIGHTING
    // MOTS added
    if (sithRender_008d4094 != 0) {
        int local_8, iVar6, iVar5;

        if (0.0 <= sithRender_008d4098) {
            local_8 = 1;
            if (sithRender_008d4098 < 0.0) {
                local_8 = 0;
            }
        }
        else {
            local_8 = 0xffffffff;
        }
        flex_t fVar3 = sithRender_008d4098 - (flex_t)local_8 * sithRender_008d409c * sithTime_deltaSeconds;
        if (0.0 <= sithRender_008d4098) {
            if (sithRender_008d4098 < 0.0) {
                iVar6 = 0;
            }
            else {
                iVar6 = 1;
            }
        }
        else {
            iVar6 = -1;
        }
        if (0.0 <= fVar3) {
            if (fVar3 > 0.0) {
                iVar5 = 1;
            }
            else {
                iVar5 = 0;
            }
        }
        else {
            iVar5 = -1;
        }
        sithRender_008d4098 = fVar3;
        if (iVar6 != iVar5) {
            sithRender_008d4098 = 0.0;
        }
        if (sithRender_008d4098 == 0.0) {
            sithRender_008d4094 = 0;
            sithRender_008d4098 = 0.0;
            sithRender_008d409c = 0.0;
        }
    }
#endif
#ifdef TARGET_TWL
    int testLightsEnd = stdPlatform_GetTimeMsec();

    int testLevelGeo = stdPlatform_GetTimeMsec();
#endif

    // TWL: 16ms
    sithRender_RenderLevelGeometry();

#ifdef TARGET_TWL
    int testLevelGeoEnd = stdPlatform_GetTimeMsec();

    int testThings = stdPlatform_GetTimeMsec();
#endif

    // TWL: 10-20ms
    if ( sithRender_numSectors2 )
        sithRender_RenderThings();

#ifdef RENDER_DROID2
	if (sithWorld_pCurrentWorld->backdropSector && sithRender_numSkyPortals > 0)
	{
		void sithRender_DrawStencils();
		sithRender_DrawStencils();
	}
#endif

#ifdef DECAL_RENDERING
	rdCache_FlushDecals();
#endif

#ifdef SPHERE_AO
	rdCache_FlushOccluders();
#endif

#ifdef RENDER_DROID2
	if (sithWorld_pCurrentWorld->backdropSector && sithRender_numSkyPortals > 0)
	{
		sithRender_DrawBackdrop();
		sithRender_ResetState();
	}
#endif

#ifdef TARGET_TWL
    int testThingsEnd = stdPlatform_GetTimeMsec();

    int testAlpha = stdPlatform_GetTimeMsec();
#endif

    // TWL: 0ms
    if ( sithRender_numSurfaces )
        sithRender_RenderAlphaSurfaces();

#if defined(QOL_IMPROVEMENTS) && !defined(TARGET_TWL)
	// draw list of alpha things
	// it would be better to replace alpha surface drawing with a reverse-sector
	// traversal, only drawing transparent surfaces, then things, then moving onto the next
	// sector in the list (similar to SITH_TF_LEVELGEO things).
	// that would preserve draw order better at the expense of some traversal cost
	if (sithRender_alphaDrawThing)
	{
		rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
		rdSetSortingMethod(2);
	#ifdef RENDER_DROID2
		rdSetBlendEnabled(RD_TRUE);
		rdSetBlendMode(RD_BLEND_SRCALPHA, RD_BLEND_INVSRCALPHA);
	#endif
		for (sithThing* iter = sithRender_alphaDrawThing; iter; )
		{
			// call the alpha callback for renderweapon
			if ((iter->thingflags & SITH_TF_RENDERWEAPON))
			{
				if (sithRender_weaponRenderAlphaHandle)
#ifdef FP_LEGS
					if (!iter->rdthing.hideWeaponMesh)
#endif
						sithRender_weaponRenderAlphaHandle(iter);
			}
			else if (sithRender_RenderThing(iter))
			{
				++sithRender_nongeoThingsDrawn;
			}
			sithThing* i = iter;
			iter = iter->nextDrawThing;
			i->nextDrawThing = NULL;
		}
	}
	rdCache_Flush("sithRender_Draw:AlphaThings");

#ifdef SDL2_RENDER
	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
#endif
#endif

#ifdef PARTICLE_LIGHTS
	rdCache_FlushLights();
#endif

    rdSetCullFlags(3);
#ifdef QOL_IMPROVEMENTS
    sithRender_RenderDebugLights();
#endif

#ifdef RGB_AMBIENT
	sithRender_RenderDebugAmbientCubes();
#endif

	STD_END_PROFILER_LABEL();

#ifdef TARGET_TWL
    int testAlphaEnd = stdPlatform_GetTimeMsec();
    //printf("clp=%d lts=%d geo=%d thg=%d al=%d %d\n", testClipEnd - testClip, testLightsEnd - testLights, testLevelGeoEnd - testLevelGeo, testThingsEnd - testThings, testAlphaEnd - testAlpha, sithRender_numSectors);
#endif
}

// MOTS altered?
// Added: depth safety
void sithRender_Clip(sithSector *sector, rdClipFrustum *frustumArg, flex_t a3, int depth)
{
    int v5; // ecx
    rdClipFrustum *frustum; // edx
    sithThing *thing; // esi
    unsigned int lightIdx; // ecx
    sithAdjoin *adjoinIter; // ebx
    sithSurface *adjoinSurface; // esi
    rdMaterial *adjoinMat; // eax
    rdVector3 *v20; // eax
    int v25; // eax
    unsigned int v27; // edi
    rdClipFrustum *v31; // ecx
    rdClipFrustum outClip; // [esp+Ch] [ebp-74h] BYREF
    rdVector3 vertex_out; // [esp+40h] [ebp-40h] BYREF
    int v45; // [esp+4Ch] [ebp-34h]
    rdTexinfo *v51; // [esp+64h] [ebp-1Ch]

    // Clip visited hardening
    // Does not help much, but no visual harm either
#ifdef QOL_IMPROVEMENTS
    if (sector->clipVisited == sithRender_lastRenderTick) {
        return;
    }
#endif

    if ( sector->renderTick == sithRender_lastRenderTick )
    {
        sector->clipFrustum = rdCamera_pCurCamera->pClipFrustum;
    }
    else
    {
        sector->renderTick = sithRender_lastRenderTick;
        // Added: Prevent crashing
        if (sithRender_numSectors >= SITH_MAX_VISIBLE_SECTORS) {
            jk_printf("OpenJKDF2: Hit max visible sectors.\n");
            return;
        }
        // Added: Prevent crashing
        if (sithRender_numClipFrustums >= SITH_MAX_VISIBLE_SECTORS) {
            jk_printf("OpenJKDF2: Hit max visible sector clip frustums.\n");
            return;
        }
        // Added: Prevent crashing
        if (sithRender_numSectors2 >= SITH_MAX_VISIBLE_SECTORS_2) {
            jk_printf("OpenJKDF2: Hit max visible sectors (2).\n");
            return;
        }

        sithRender_aSectors[sithRender_numSectors++] = sector;
        if (!(sector->flags & SITH_SECTOR_AUTOMAPVISIBLE) && !(g_debugmodeFlags & DEBUGFLAG_NOCLIP)) // Added: don't send sighted stuff in noclip, otherwise the whole map reveals
        {
            sector->flags |= SITH_SECTOR_AUTOMAPVISIBLE;
            if ( (sector->flags & SITH_SECTOR_COGLINKED) != 0 )
                sithCog_SendMessageFromSector(sector, 0, SITH_MESSAGE_SIGHTED);
        }
        frustum = &sithRender_clipFrustums[sithRender_numClipFrustums++];
        _memcpy(frustum, frustumArg, sizeof(rdClipFrustum));
        thing = sector->thingsList;
        sector->clipFrustum = frustum;
        lightIdx = sithRender_numLights;

#ifdef RENDER_DROID2
		for (int i = 0; i < sector->numSurfaces; ++i)
		{
			if (sector->surfaces[i].surfaceInfo.face.material && sector->surfaces[i].surfaceFlags & SITH_SURFACE_EMISSIVE)
				lightIdx = sithRender_AddSurfaceLight(&sector->surfaces[i]);

			if (sector->surfaces[i].surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY))
				++sithRender_numSkyPortals;
		}
#endif

        // Added: safety
        int safeguard = 0;
        while ( thing )
        {
            if ( lightIdx >= SITHREND_NUM_LIGHTS)
                break;

            // Added: safety
            if (++safeguard >= SITH_MAX_THINGS)
                break;

            // Debug, add extra light from player
#if 0
            if (thing->type == SITH_THING_PLAYER)
            {
                rdMatrix_TransformPoint34(&vertex_out, &thing->actorParams.lightOffset, &thing->lookOrientation);
                rdVector_Add3Acc(&vertex_out, &thing->position);
                sithRender_aLights[sithRender_numLights].intensity = 1.0;//thing->actorParams.lightIntensity;
                rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &vertex_out);
                lightIdx = ++sithRender_numLights;
            }
#endif

            if ((thing->thingflags & SITH_TF_LIGHT)
                 && !(thing->thingflags & (SITH_TF_DISABLED| SITH_TF_INVISIBLE |SITH_TF_WILLBEREMOVED)))
            {
                if ( thing->light > 0.0 )
                {
                    sithRender_aLights[lightIdx].intensity = thing->light;
				#ifdef RGB_THING_LIGHTS
					sithRender_aLights[lightIdx].color = thing->lightColor;
				#endif
#ifdef RENDER_DROID2
					if (thing->lightRadius > 0.0)
					{
						rdCamera_AddLightExplicitRadius(rdCamera_pCurCamera, &sithRender_aLights[lightIdx], thing->lightRadius, &thing->position);
					}
					else
#endif
					{
						rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[lightIdx], &thing->position);
					}
#ifdef RENDER_DROID2
					if (thing->lightAngle > 0.0)
					{
						sithRender_aLights[lightIdx].type = RD_LIGHT_SPOTLIGHT;
						sithRender_aLights[lightIdx].direction = thing->lookOrientation.lvec;
						rdLight_SetAngles(&sithRender_aLights[lightIdx], thing->lightAngle * 0.1, thing->lightAngle);
						sithRender_aLights[lightIdx].width = thing->lightSize;
					}
#endif
                    lightIdx = ++sithRender_numLights;
                }

                if ( (thing->type == SITH_THING_ACTOR || thing->type == SITH_THING_PLAYER) && lightIdx < SITHREND_NUM_LIGHTS)
                {
                    if ( (thing->actorParams.typeflags & SITH_AF_FIELDLIGHT) != 0 && thing->actorParams.lightIntensity > 0.0 )
                    {
                        rdMatrix_TransformPoint34(&vertex_out, &thing->actorParams.lightOffset, &thing->lookOrientation);
                        rdVector_Add3Acc(&vertex_out, &thing->position);
                        sithRender_aLights[sithRender_numLights].intensity = thing->actorParams.lightIntensity;
#ifdef RGB_THING_LIGHTS
						rdVector_Set3(&sithRender_aLights[sithRender_numLights].color, 1.0f, 1.0f, 1.0f);
#endif
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &vertex_out);
                        lightIdx = ++sithRender_numLights;
                    }
                    if ( thing->actorParams.timeLeftLengthChange > 0.0 )
                    {
                        sithRender_aLights[lightIdx].intensity = thing->actorParams.timeLeftLengthChange;
#ifdef RGB_THING_LIGHTS
						sithRender_GetSaberLightColor(&sithRender_aLights[lightIdx].color, thing);
#endif
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[lightIdx], &thing->actorParams.saberBladePos);
                        lightIdx = ++sithRender_numLights;
                    }
                }
            }
            thing = thing->nextThing;
        }

#ifdef RENDER_DROID2
		for (int bucket = 0; bucket < sithWorld_pCurrentWorld->numLightBuckets; ++bucket)
		{
			if(sector->lightBuckets)
				sithWorld_pCurrentWorld->lightBuckets[bucket] |= sector->lightBuckets[bucket];
		}
#endif

        sithRender_aSectors2[sithRender_numSectors2++] = sector;
    }

    
    v45 = sector->clipVisited;
    sithRender_idxInfo.vertices = sithWorld_pCurrentWorld->verticesTransformed;
    sithRender_idxInfo.vertexUVs = sithWorld_pCurrentWorld->vertexUVs;

    sithRender_idxInfo.paDynamicLight = sithWorld_pCurrentWorld->verticesDynamicLight;
#ifdef RGB_THING_LIGHTS
	sithRender_idxInfo.paDynamicLightR = sithWorld_pCurrentWorld->verticesDynamicLightR;
	sithRender_idxInfo.paDynamicLightG = sithWorld_pCurrentWorld->verticesDynamicLightG;
	sithRender_idxInfo.paDynamicLightB = sithWorld_pCurrentWorld->verticesDynamicLightB;
#endif

    // Clip visited hardening
#ifdef QOL_IMPROVEMENTS
    sector->clipVisited = sithRender_lastRenderTick;
#else
    sector->clipVisited = 1;
#endif
    sithRender_idxInfo.paDynamicLight = sithWorld_pCurrentWorld->verticesDynamicLight;

    // Added: safeguard
    for (adjoinIter = sector->adjoins ; adjoinIter != NULL; adjoinIter = adjoinIter->next)
    {
        // Clip visited hardening
#ifdef QOL_IMPROVEMENTS
        if (adjoinIter->sector->clipVisited == sithRender_lastRenderTick)
#else
        if (adjoinIter->sector->clipVisited)
#endif
        {
            continue;
        }

        // Added
        if (++sithRender_adjoinSafeguard >= 0x100000) {
            stdPlatform_Printf("Hit safeguard...\n");
            break;
        }

        adjoinSurface = adjoinIter->surface;

        // Avoid rendering adjoins if they're behind the near clipping plane
        // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
        if ((adjoinSurface->field_4 == sithRender_lastRenderTick) && (adjoinIter->timesClipped > 1) && (adjoinIter->maxZ < frustumArg->zNear)) {
            continue;
        }
#endif
        adjoinMat = adjoinSurface->surfaceInfo.face.material;
        if ( adjoinMat )
        {
            int v19 = adjoinSurface->surfaceInfo.face.wallCel;
            if ( v19 == -1 )
                v19 = adjoinMat->celIdx;
            v51 = adjoinMat->texinfos[v19]; 
        }
        else {
            v51 = NULL; // Added. TODO: does setting this to NULL cause issues?
        }

        v20 = &sithWorld_pCurrentWorld->vertices[*adjoinSurface->surfaceInfo.face.vertexPosIdx];
        flex_t dist = (sithCamera_currentCamera->vec3_1.y - v20->y) * adjoinSurface->surfaceInfo.face.normal.y
                   + (sithCamera_currentCamera->vec3_1.z - v20->z) * adjoinSurface->surfaceInfo.face.normal.z
                   + (sithCamera_currentCamera->vec3_1.x - v20->x) * adjoinSurface->surfaceInfo.face.normal.x;
        flex_t adjoinDistAdd = adjoinIter->dist + adjoinIter->mirror->dist + a3;

        // Avoid rendering adjoins if they're far enough away
#ifdef TARGET_TWL
        // adjoinDistAdd compare GREATLY reduces recursion issues 
        // TODO: Test against TODOA and verify if this is QOL-worthy
        if (/*(adjoinDistAdd > 4.0) ||*/ (dist > 3.0)) {
            // Doesn't help, causes visual issues
            //adjoinIter->sector->clipVisited = sithRender_lastRenderTick;

            continue;
        }
#endif

        if ( dist > 0.0 || (dist == 0.0 && sector == sithCamera_currentCamera->sector))
        {
            int bAdjoinIsTransparent = (((!adjoinSurface->surfaceInfo.face.material ||
                        (adjoinSurface->surfaceInfo.face.geometryMode == 0)) ||
                       ((adjoinSurface->surfaceInfo.face.type & 2))) ||
                      (v51 && (v51->header.texture_type & 8) && (v51->texture_ptr->alpha_en & 1))
                      );

#ifdef QOL_IMPROVEMENTS
            // Added: Somehow the clipping changed enough to cause a bug in MoTS Lv12.
            // The ground under the water surface somehow renders.
            // As a mitigation, if a mirror surface is transparent but the top-layer isn't,
            // we will render underneath anyways.
            sithSurface* adjoinMirrorSurface = adjoinIter->mirror->surface;
            rdMaterial* adjoinMirrorMat = adjoinMirrorSurface->surfaceInfo.face.material;
            rdTexinfo* adjoinMirrorTexinfo = NULL;
            if ( adjoinMirrorMat )
            {
                int v19 = adjoinMirrorSurface->surfaceInfo.face.wallCel;
                if ( v19 == -1 )
                    v19 = adjoinMirrorMat->celIdx;
                adjoinMirrorTexinfo = adjoinMirrorMat->texinfos[v19]; 
            }
            else {
                adjoinMirrorTexinfo = NULL; // Added. TODO: does setting this to NULL cause issues?
            }

            int bMirrorAdjoinIsTransparent = (((!adjoinMirrorSurface->surfaceInfo.face.material ||
                        (adjoinMirrorSurface->surfaceInfo.face.geometryMode == RD_GEOMODE_NOTRENDERED)) ||
                       ((adjoinMirrorSurface->surfaceInfo.face.type & 2))) ||
                      (adjoinMirrorTexinfo && (adjoinMirrorTexinfo->header.texture_type & 8) && (adjoinMirrorTexinfo->texture_ptr->alpha_en & 1))
                      );

            bAdjoinIsTransparent |= bMirrorAdjoinIsTransparent;
#endif

            if ( adjoinSurface->field_4 != sithRender_lastRenderTick )
            {
                for (int i = 0; i < adjoinSurface->surfaceInfo.face.numVertices; i++)
                {
                    v25 = adjoinSurface->surfaceInfo.face.vertexPosIdx[i];
                    if ( sithWorld_pCurrentWorld->alloc_unk98[v25] != sithRender_lastRenderTick )
                    {
                        rdMatrix_TransformPoint34(&sithWorld_pCurrentWorld->verticesTransformed[v25], &sithWorld_pCurrentWorld->vertices[v25], &rdCamera_pCurCamera->view_matrix);
                        sithWorld_pCurrentWorld->alloc_unk98[v25] = sithRender_lastRenderTick;
                    }
                }
                adjoinSurface->field_4 = sithRender_lastRenderTick;
#ifdef TARGET_TWL
                adjoinIter->timesClipped = 1;
#endif
            }
            else {
                // Added?
                //continue;

                // slight improvement, visual issues
                /*if (adjoinSurface->frustum == frustumArg) {
                    continue;
                }*/

                // doesn't help, severely hurts perf
                /*flex_t a3a = adjoinIter->dist + adjoinIter->mirror->dist + a3;
                if (!(sithRender_flag & 4) || a3a < sithRender_f_82F4B0 ) // wtf is with this float?
                    sithRender_Clip(adjoinIter->sector, frustumArg, a3a);
                continue;*/

                // Droidworks has a peculiar area where this function would shoot upwards of 70ms
                // just clipping some adjoin in an open space, so we cap the number of times a surface
                // can be clipped
                // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
                if (adjoinIter->timesClipped >= SITH_MAX_SURFACE_CLIP_ITERS) {
                    continue;
                }

                int bFrustumSmaller = (frustumArg->minX > adjoinIter->minX 
                                            && frustumArg->maxX < adjoinIter->maxX 
                                            && frustumArg->minY > adjoinIter->minY 
                                            && frustumArg->maxY < adjoinIter->maxY);
                int bFrustumSame = (frustumArg->minX == adjoinIter->minX 
                                            && frustumArg->maxX == adjoinIter->maxX 
                                            && frustumArg->minY == adjoinIter->minY 
                                            && frustumArg->maxY == adjoinIter->maxY);

                //printf("%d: %x %x\n", sector->id, bFrustumSmaller, depth);

                // Skip clipping calculations if the frustum is larger or the same
                if ((adjoinIter->timesClipped > 1) && (!bFrustumSmaller || bFrustumSame)) {
                    flex_t a3a = adjoinIter->dist + adjoinIter->mirror->dist + a3;
                    if (!(sithRender_flag & 4) || a3a < sithRender_f_82F4B0 ) {
                        // Block backward traversal during depth-first search
                        int mirrorTimesClipped = adjoinIter->mirror->timesClipped;
                        int surfaceTimesClipped = adjoinIter->timesClipped;
                        adjoinIter->mirror->timesClipped = sithRender_lastRenderTick;
                        adjoinIter->timesClipped = sithRender_lastRenderTick;

                        sithRender_NoClip(adjoinIter->sector, frustumArg, a3a, depth+1);
                        //sithRender_Clip(adjoinIter->sector, frustumArg, a3a, depth+1);

                        adjoinIter->timesClipped = surfaceTimesClipped;
                        adjoinIter->mirror->timesClipped = mirrorTimesClipped;
                        continue;
                    }
                }
#endif
            }

            

            sithRender_idxInfo.numVertices = adjoinSurface->surfaceInfo.face.numVertices;
            sithRender_idxInfo.vertexPosIdx = adjoinSurface->surfaceInfo.face.vertexPosIdx;
            meshinfo_out.verticesProjected = sithRender_aVerticesTmp;
            sithRender_idxInfo.vertexUVIdx = adjoinSurface->surfaceInfo.face.vertexUVIdx;

            rdPrimit3_ClipFace(frustumArg, RD_GEOMODE_WIREFRAME, RD_LIGHTMODE_NOTLIT, RD_TEXTUREMODE_AFFINE, &sithRender_idxInfo, &meshinfo_out, &adjoinSurface->surfaceInfo.face.clipIdk);

            if ((((unsigned int)meshinfo_out.numVertices >= 3u) || (rdClip_faceStatus & CLIPSTAT_NONE_VISIBLE)) 
                && ((rdClip_faceStatus & (CLIPSTAT_NEAR|CLIPSTAT_NONE_VISIBLE)) || ((adjoinIter->flags & 1) && bAdjoinIsTransparent))) 
            {
                rdCamera_pCurCamera->fnProjectLst(sithRender_aVerticesTmp_projected, sithRender_aVerticesTmp, meshinfo_out.numVertices);
                
                v31 = frustumArg;

                // no frustum culling if forced
                if (rdClip_faceStatus & (CLIPSTAT_NEAR|CLIPSTAT_NONE_VISIBLE))
                {
                    v31 = frustumArg;
                }
                else
                {
                    flex_t minX = FLT_MAX;
                    flex_t minY = FLT_MAX;
                    flex_t maxX = -FLT_MAX;
                    flex_t maxY = -FLT_MAX;
#ifdef TARGET_TWL
                    flex_t minZ = FLT_MAX;
                    flex_t maxZ = -FLT_MAX;
#endif
                    for (int i = 0; i < meshinfo_out.numVertices; i++)
                    {
                        flex_t v34 = sithRender_aVerticesTmp_projected[i].x;
                        flex_t v57 = sithRender_aVerticesTmp_projected[i].y;
                        flex_t v_z = sithRender_aVerticesTmp_projected[i].z;
                        if (v34 < minX)
                            minX = v34;
                        if (v34 > maxX)
                            maxX = v34;

                        if (v57 < minY)
                            minY = v57;
                        if (v57 > maxY)
                            maxY = v57;
#ifdef TARGET_TWL
                        minZ = stdMath_Min(v_z, minZ);
                        maxZ = stdMath_Max(v_z, maxZ);
#endif
                    }

                    // Causes random black lines?
#if 0
                    flex_t v49 = stdMath_Ceil(maxY);
                    flex_t v48 = stdMath_Ceil(maxX);
                    flex_t v47 = stdMath_Ceil(minY);
                    flex_t v46 = stdMath_Ceil(minX);
#endif

                    // Fixed
                    flex_t v46 = minX - 2.0;//stdMath_Ceil(minX);
                    flex_t v47 = minY - 2.0;//stdMath_Ceil(minY);
                    flex_t v48 = maxX + 1.5;
                    flex_t v49 = maxY + 1.5;
                    
                    // Check that the new frustum will be smaller than the last, 
                    //  if it won't be then stop recursing on this surface--
                    // the clipping will just return the same vertices
                    //  and waste time.
                    // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
                    adjoinIter->timesClipped++;
                    /*adjoinIter->minX = stdMath_Max(v46, adjoinIter->minX);
                    adjoinIter->minY = stdMath_Max(v47, adjoinIter->minY);
                    adjoinIter->maxX = stdMath_Min(v48, adjoinIter->maxX);
                    adjoinIter->maxY = stdMath_Min(v49, adjoinIter->maxY);*/
                    adjoinIter->minX = v46;
                    adjoinIter->minY = v47;
                    adjoinIter->maxX = v48;
                    adjoinIter->maxY = v49;
                    adjoinIter->minZ = minZ;
                    adjoinIter->maxZ = maxZ;

                    adjoinIter->mirror->timesClipped++;
                    adjoinIter->mirror->minX = adjoinIter->minX;
                    adjoinIter->mirror->minY = adjoinIter->minY;
                    adjoinIter->mirror->maxX = adjoinIter->maxX;
                    adjoinIter->mirror->maxY = adjoinIter->maxY;
                    adjoinIter->mirror->minZ = adjoinIter->minZ;
                    adjoinIter->mirror->maxZ = adjoinIter->maxZ;
#endif

                    rdCamera_BuildClipFrustum(rdCamera_pCurCamera, &outClip, (int)(v46 - -0.5), (int)(v47 - -0.5), (int)v48, (int)v49);
                    v31 = &outClip;

                    // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
                    v31->zNear = minZ - 0.1;
#endif
                }

                // Added: noclip
                if (sithPlayer_bNoClippingRend) continue;
                
                // Block backward traversal during depth-first search
                // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
                int mirrorTimesClipped = adjoinIter->mirror->timesClipped;
                int surfaceTimesClipped = adjoinIter->timesClipped;
                
                // wtf is with this float?
                if (!(sithRender_flag & 4) || adjoinDistAdd < sithRender_f_82F4B0 ) {
                    if (depth > 3) {
                        adjoinIter->mirror->timesClipped = sithRender_lastRenderTick;
                        adjoinIter->timesClipped = sithRender_lastRenderTick;
                        sithRender_NoClip(adjoinIter->sector, v31, adjoinDistAdd, depth+1);    
                    }
                    else {
                        adjoinIter->mirror->timesClipped = SITH_MAX_SURFACE_CLIP_ITERS;
                        adjoinIter->timesClipped = SITH_MAX_SURFACE_CLIP_ITERS;
                        sithRender_Clip(adjoinIter->sector, v31, adjoinDistAdd, depth+1);
                    }
                    
                }
#else
                // wtf is with this float?
                if (!(sithRender_flag & 4) || adjoinDistAdd < sithRender_f_82F4B0 ) {
                    sithRender_Clip(adjoinIter->sector, v31, adjoinDistAdd, depth+1);
                }
#endif
#ifdef TARGET_TWL
                adjoinIter->timesClipped = surfaceTimesClipped;
                adjoinIter->mirror->timesClipped = mirrorTimesClipped;
#endif
            }
        }
    }
    sector->clipVisited = v45;
}

#ifdef RENDER_DROID2
void sithRender_DrawSkyStencil(sithSurface* surface)
{
	if (rdBeginPrimitive(RD_PRIMITIVE_POLYGON))
	{
		for (int j = 0; j < surface->surfaceInfo.face.numVertices; j++)
		{
			int posidx = surface->surfaceInfo.face.vertexPosIdx[j];
			rdVector3* v = &sithWorld_pCurrentWorld->vertices[posidx];
			rdVertex3v(&v->x);
		}
		rdEndPrimitive();
	}
}

void sithRender_DrawSurface(sithSurface* surface)
{
	if(!surface->surfaceInfo.face.material)
		return;
		
	// sky punches through to the backdrop if there is one
	if ((surface->surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY))
		&& sithWorld_pCurrentWorld->backdropSector
		&& sithWorld_pCurrentWorld->backdropSector != surface->parent_sector
	)
	{
		return;
	}

	float w = 1.0f, h = 1.0f;
	if (surface->surfaceInfo.face.material->num_textures)
	{
		w = (float)(surface->surfaceInfo.face.material->textures[0].width_minus_1 + 1);
		h = (float)(surface->surfaceInfo.face.material->textures[0].height_minus_1 + 1);
	}


	int geoMode;
	if ((surface->surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY)) != 0)
	{
		geoMode = sithRender_geoMode;
		if (sithRender_geoMode > RD_GEOMODE_TEXTURED)
			geoMode = RD_GEOMODE_TEXTURED;
	}
	else
	{
		geoMode = surface->surfaceInfo.face.geometryMode;
		if (geoMode >= sithRender_geoMode)
			geoMode = sithRender_geoMode;
	}
	rdSetGeoMode(geoMode);

	int lightMode = surface->surfaceInfo.face.lightingMode;

	//float dist = rdVector_Dist3(&sithCamera_currentCamera->vec3_1, &sithWorld_pCurrentWorld->vertices[*surface->surfaceInfo.face.vertexPosIdx]);
	//if (dist >= (double)sithWorld_pCurrentWorld->gouradDistance)
	//{
	//	if (lightMode > RD_LIGHTMODE_DIFFUSE)
	//		lightMode = RD_LIGHTMODE_DIFFUSE;
	//}

	if (sithRender_lightingIRMode)
	{
		if (lightMode >= RD_LIGHTMODE_DIFFUSE)
			lightMode = RD_LIGHTMODE_DIFFUSE;
	}
	else if (lightMode >= sithRender_lightMode)
	{
		lightMode = sithRender_lightMode;
	}
	//lightMode = RD_LIGHTMODE_SPECULAR;
	if ((surface->surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY)) != 0)
		lightMode = RD_LIGHTMODE_FULLYLIT;

	rdSetLightMode(lightMode);

	int isWater = 0;
	if (surface->adjoin && (surface->parent_sector->flags & SITH_SECTOR_UNDERWATER || surface->adjoin->sector->flags & SITH_SECTOR_UNDERWATER))
		isWater = 1;
	//else if (!(surface->parent_sector->flags & SITH_SECTOR_UNDERWATER) && (surface->surfaceFlags & SITH_SURFACE_WATER || surface->surfaceFlags & SITH_SURFACE_PUDDLE || surface->surfaceFlags & SITH_SURFACE_VERYDEEPWATER))
		//isWater = 1;

	if (isWater)
	{
		rdSetShader(sithRender_waterShader);
	}
	else if (surface->surfaceFlags & SITH_SURFACE_HORIZON_SKY)
	{
		rdSetShader(sithRender_horizonSky);
		rdSetShaderConstant4f(0, sithSector_flt_8553C0, sithSector_flt_8553C8, sithSector_flt_8553F4, 0);
		rdSetShaderConstant4f(1, (sithWorld_pCurrentWorld->horizontalSkyOffs.x + sithSector_flt_8553B8) / w, (sithWorld_pCurrentWorld->horizontalSkyOffs.y + sithSector_flt_8553C4) / h, 0, 0);
	}
	else if (surface->surfaceFlags & SITH_SURFACE_CEILING_SKY)
	{
		extern rdVector3 sithSector_ceilingSkyNormal;
		extern float sithSector_ceilingDot;

		rdSetShader(sithRender_ceilingSky);
		rdSetShaderConstant4f(0, sithSector_ceilingSkyNormal.x, sithSector_ceilingSkyNormal.y, sithSector_ceilingSkyNormal.z, sithSector_ceilingDot);
		rdSetShaderConstant4f(1, sithWorld_pCurrentWorld->ceilingSkyOffs.x / w, sithWorld_pCurrentWorld->ceilingSkyOffs.y / h, 0, 0);
	}
	else if (jkPlayer_bEnableJkgm
		&& surface->surfaceInfo.face.material->textures
		&& surface->surfaceInfo.face.material->textures->has_jkgm_override)
	{
		rdSetShader(sithRender_jkgmShader);
	}
	else if(lightMode == RD_LIGHTMODE_SPECULAR)
	{
		rdSetShader(sithRender_specularShader);
	}
	else
	{
		rdSetShader(sithRender_defaultShader);
	}

	int wallCel = surface->surfaceInfo.face.wallCel;
	rdBindMaterial(surface->surfaceInfo.face.material, wallCel);

	if (sithRender_lightingIRMode)
	{
		rdAmbientLight(sithRender_f_83198C, sithRender_f_83198C, sithRender_f_83198C);
	}
	else
	{
		float extra = (surface->parent_sector->extraLight + sithRender_008d4098);//, 0.0, 1.0);
		rdAmbientLight(extra, extra, extra);
	}
	rdExtraLight(surface->surfaceInfo.face.extraLight);
	rdAmbientLightSH(NULL);
	//rdAmbientLightSH(&surface->parent_sector->ambientSH);

	rdTexFilterMode(!jkPlayer_enableTextureFilter || (surface->surfaceInfo.face.type & RD_FF_TEX_FILTER_NEAREST) ? RD_TEXFILTER_NEAREST : RD_TEXFILTER_BILINEAR);

	int texMode = surface->surfaceInfo.face.textureMode;
	if (texMode >= sithRender_texMode)
		texMode = sithRender_texMode;

	//if (surface->surfaceFlags & SITH_SURFACE_HORIZON_SKY)
	//{
	//	texMode = texMode > RD_TEXTUREMODE_AFFINE ? RD_TEXTUREMODE_AFFINE : texMode;
	//	rdTexGen(RD_TEXGEN_HORIZON);
	//	rdTexGenParams(sithSector_flt_8553C0, sithSector_flt_8553C8, sithSector_flt_8553F4, 0);
	//	rdTexOffset(RD_TEXCOORD0, sithWorld_pCurrentWorld->horizontalSkyOffs.x + sithSector_flt_8553B8, sithWorld_pCurrentWorld->horizontalSkyOffs.y + sithSector_flt_8553C4);
	//}
	//else if (surface->surfaceFlags & SITH_SURFACE_CEILING_SKY)
	//{
	//	texMode = RD_TEXTUREMODE_PERSPECTIVE;
	//	rdTexGen(RD_TEXGEN_CEILING);
	//	rdTexGenParams(sithSector_zMaxVec.x, sithSector_zMaxVec.y, sithSector_zMaxVec.z, 0);
	//	rdTexOffset(RD_TEXCOORD0, sithWorld_pCurrentWorld->ceilingSkyOffs.x, sithWorld_pCurrentWorld->ceilingSkyOffs.y);
	//}
	//else
	{
		if (isWater)// if (surface->adjoin && (surface->parent_sector->flags & SITH_SECTOR_UNDERWATER || surface->adjoin->sector->flags & SITH_SECTOR_UNDERWATER))//if (surface->adjoin && surface->adjoin->sector->flags & SITH_SECTOR_UNDERWATER)
		{
			rdSetLightMode(RD_LIGHTMODE_FULLYLIT);
			rdTexFilterMode(RD_TEXFILTER_BILINEAR);
			rdSetGeoMode(RD_GEOMODE_TEXTURED);

			texMode = RD_TEXTUREMODE_PERSPECTIVE;
			//rdSetLightMode(RD_LIGHTMODE_FULLYLIT);//SPECULAR);
			rdTexGen(RD_TEXGEN_WATER);
			if (surface->adjoin && (surface->adjoin->sector->flags & SITH_SECTOR_UNDERWATER))
				rdTexGenParams(surface->adjoin->sector->tint.x, surface->adjoin->sector->tint.y, surface->adjoin->sector->tint.z, sithTime_curSeconds);
			else
				rdTexGenParams(0, 0, 0, sithTime_curSeconds);

			// surface scroll currently causes popping when it loops
			rdTexOffseti(RD_TEXCOORD0, surface->surfaceInfo.face.clipIdk.x, surface->surfaceInfo.face.clipIdk.y);

			//if(surface->adjoin)
			//	rdAmbientLightSH(&surface->adjoin->sector->ambientSH);
			//else
			//	rdAmbientLightSH(&surface->parent_sector->ambientSH);
		}
		else
		{
			if (surface->parent_sector->flags & SITH_SECTOR_UNDERWATER)
				rdAmbientFlags(sithRender_aoFlags | RD_AMBIENT_CAUSTICS);
			rdTexOffseti(RD_TEXCOORD0, surface->surfaceInfo.face.clipIdk.x, surface->surfaceInfo.face.clipIdk.y);
		}
	}

	if(surface->parent_sector->flags & SITH_SECTOR_DRAW_AS_3DO)
		rdSetCullMode(RD_CULL_MODE_FRONT);
	else
		rdSetCullMode(RD_CULL_MODE_BACK);

	rdSetTexMode(texMode);

	int isAlpha = (surface->surfaceInfo.face.type & RD_FF_TEX_TRANSLUCENT) != 0;

	// if this is a ceiling sky and we have a backdrop, render it with alpha as a cloud layer
	//if ((surface->surfaceFlags & SITH_SURFACE_CEILING_SKY) && sithWorld_pCurrentWorld->backdropSector)
	//	isAlpha = 1;

	float alpha = 1.0f;
	if (isAlpha)
	{
		alpha = 90.0f / 255.0f;
		rdSetBlendEnabled(RD_TRUE);
		rdSetBlendMode(RD_BLEND_SRCALPHA, RD_BLEND_INVSRCALPHA);
		rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
	}
	else
	{
		rdSetBlendEnabled(RD_FALSE);
		rdSetZBufferMethod((surface->surfaceInfo.face.type & RD_FF_ZWRITE_DISABLED) ? RD_ZBUFFER_READ_NOWRITE : RD_ZBUFFER_READ_WRITE);
	}

	if (isWater)
		alpha = 1.0f;

	rdVector3 tint = { 1,1,1 };
	if (isWater)
	{
		//tint = (rdVector3){ surface->adjoin->sector->tint.x, surface->adjoin->sector->tint.y, surface->adjoin->sector->tint.z };
	}
	else ///if (surface->parent_sector != sithCamera_currentCamera->sector)
	{
		//tint = surface->parent_sector->tint;
	}

	rdVector3 cmpTint = surface->parent_sector->colormap->tint;

	rdVector3 halfTint;
	halfTint.x = tint.x * 0.5f;
	halfTint.y = tint.y * 0.5f;
	halfTint.z = tint.z * 0.5f;

	tint.x -= (halfTint.z + halfTint.y);
	tint.y -= (halfTint.x + halfTint.y);
	tint.z -= (halfTint.x + halfTint.z);

	if (rdBeginPrimitive(RD_PRIMITIVE_POLYGON))
	{
		for (int j = 0; j < surface->surfaceInfo.face.numVertices; j++)
		{
			int posidx = surface->surfaceInfo.face.vertexPosIdx[j];

			if (lightMode == RD_LIGHTMODE_FULLYLIT)
			{
				rdVector3 rgb;
				rgb.x = tint.x * 1.5 + 1.5f;
				rgb.y = tint.y * 1.5 + 1.5f;
				rgb.z = tint.z * 1.5 + 1.5f;
				rdColor4f(rgb.x * cmpTint.x, rgb.y * cmpTint.y, rgb.z * cmpTint.z, alpha);
			}
			else if (lightMode == RD_LIGHTMODE_NOTLIT)
			{
				rdVector3 rgb;
				rgb.x = tint.x + 1.0f;
				rgb.y = tint.y + 1.0f;
				rgb.z = tint.z + 1.0f;
				rdColor4f(rgb.x* cmpTint.x, rgb.y* cmpTint.y, rgb.z* cmpTint.z, alpha);
			}
			else if ((surface->surfaceFlags & SITH_SURFACE_1000000) == 0)
			{
				float intensity = surface->surfaceInfo.intensities[j];
				
				rdVector3 rgb;
				rgb.x = intensity * tint.x + intensity;
				rgb.y = intensity * tint.y + intensity;
				rgb.z = intensity * tint.z + intensity;

				rdColor4f(rgb.x* cmpTint.x, rgb.y* cmpTint.y, rgb.z* cmpTint.z, alpha);
			}
			else
			{
				float* red = (surface->surfaceInfo).intensities + surface->surfaceInfo.face.numVertices;
				float* green = red + surface->surfaceInfo.face.numVertices;
				float* blue = green + surface->surfaceInfo.face.numVertices;

				float r = (red[j]);
				float g = (green[j]);
				float b = (blue[j]);

				rdVector3 rgb;
				rgb.x = r * tint.x + r;
				rgb.y = g * tint.y + g;
				rgb.z = b * tint.z + b;

				rdColor4f(rgb.x * cmpTint.x, rgb.y * cmpTint.y, rgb.z * cmpTint.z, alpha);
			}

			if (surface->surfaceInfo.face.geometryMode >= RD_GEOMODE_TEXTURED && surface->surfaceInfo.face.vertexUVIdx)
			{
				int uvidx = surface->surfaceInfo.face.vertexUVIdx[j];
				rdVector2* uv = &sithWorld_pCurrentWorld->vertexUVs[uvidx];
				rdTexCoord2i(RD_TEXCOORD0, uv->x, uv->y);

				if(isWater)
				{
					rdTexCoord2i(RD_TEXCOORD0, uv->x * 0.6, uv->y * 0.6);
					rdTexCoord2i(RD_TEXCOORD1, uv->x * 0.4, uv->y * 0.4);
					rdTexCoord2i(RD_TEXCOORD2, uv->x * 1.1, uv->y * 1.1);
					rdTexCoord2i(RD_TEXCOORD3, uv->x * 0.9, uv->y * 0.9);

					rdTexOffset(RD_TEXCOORD0,stdMath_Frac( 0.2 * sithTime_curSeconds),stdMath_Frac( 0.2 * sithTime_curSeconds));
					rdTexOffset(RD_TEXCOORD1,stdMath_Frac(-0.1 * sithTime_curSeconds*0.5)*2.0,stdMath_Frac(-0.1 * sithTime_curSeconds * 0.5) * 2.0);
					rdTexOffset(RD_TEXCOORD2,stdMath_Frac( 0.2 * sithTime_curSeconds * 0.35)/0.35,stdMath_Frac( 0.2 * sithTime_curSeconds * 0.35)/0.35);
					rdTexOffset(RD_TEXCOORD3,stdMath_Frac(-0.1 * sithTime_curSeconds* 0.25)/ 0.25,stdMath_Frac(-0.1 * sithTime_curSeconds* 0.25)/ 0.25);
				}
			}

			rdNormal3v(&surface->surfaceInfo.face.normal.x);

			rdVector3* v = &sithWorld_pCurrentWorld->vertices[posidx];
			rdVertex3v(&v->x);
		}
		rdEndPrimitive();
	}

	rdAmbientFlags(sithRender_aoFlags);
	
	rdSetShader(0);
	rdSetBlendEnabled(RD_FALSE);

	rdTexGen(RD_TEXGEN_NONE);
	rdTexGenParams(0, 0, 0, 0);
	rdTexOffset(RD_TEXCOORD0, 0, 0);
	rdTexOffset(RD_TEXCOORD1, 0, 0);
	rdTexOffset(RD_TEXCOORD2, 0, 0);
	rdTexOffset(RD_TEXCOORD3, 0, 0);
}
#endif

#ifdef RENDER_DROID2
void sithRender_DrawStencils()
{
	rdSetShader(0);
	rdSetGeoMode(RD_GEOMODE_SOLIDCOLOR);
	rdSetLightMode(RD_LIGHTMODE_NOTLIT);
	rdSetTexMode(RD_TEXTUREMODE_AFFINE);
	rdTexFilterMode(RD_TEXFILTER_NEAREST);
	rdTexGen(RD_TEXGEN_NONE);
	rdTexGenParams(0, 0, 0, 0);
	rdTexOffset(RD_TEXCOORD0, 0, 0);

	rdColorMask(0, 0, 0, 0); // no color writes
	rdStencilMode(2);
	rdStencilBit(1);

	rdAmbientLight(0, 0, 0);
	rdAmbientLightSH(NULL);

	rdSetCullMode(RD_CULL_MODE_BACK);

	rdSetBlendEnabled(RD_FALSE);
	rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);


	rdClipFrustum* clipFrustum = rdCamera_pCurCamera->pClipFrustum;
	for (int i = 0; i < sithRender_numSectors; ++i)
	{
		sithSector* pSector = sithRender_aSectors[i];
		if (pSector->flags & SITH_SECTOR_BACKDROP)
			continue;

		sithSurface* surface = pSector->surfaces;
		for (int v75 = 0; v75 < pSector->numSurfaces; ++surface, v75++)
		{
			// sky punches through to the backdrop if there is one
			if (!((surface->surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY))
				  && sithWorld_pCurrentWorld->backdropSector
				  && sithWorld_pCurrentWorld->backdropSector != surface->parent_sector
				  ))
			{
				continue;
			}

			if (!surface->surfaceInfo.face.geometryMode)
				continue;

			rdVector3* vertices_alloc = sithWorld_pCurrentWorld->vertices;
			if ((sithCamera_currentCamera->vec3_1.z - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].z) * surface->surfaceInfo.face.normal.z
				+ (sithCamera_currentCamera->vec3_1.y - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].y) * surface->surfaceInfo.face.normal.y
				+ (sithCamera_currentCamera->vec3_1.x - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].x) * surface->surfaceInfo.face.normal.x <= 0.0)
				continue;

			rdVector3 screenPos;
			rdMatrix_TransformPoint34(&screenPos, &surface->center, &rdCamera_pCurCamera->view_matrix);
			int clippingIdk = rdClip_SphereInFrustrum(pSector->clipFrustum, &screenPos, surface->radius);
			if (clippingIdk == 2)
				continue;

			void sithRender_DrawSkyStencil(sithSurface * surface);
			sithRender_DrawSkyStencil(surface);
		}
	}
	rdCache_Flush("sithRender_DrawStencils");
	rdCamera_pCurCamera->pClipFrustum = clipFrustum;

	rdColorMask(1, 1, 1, 1);
	rdStencilMode(0);
	rdStencilBit(0);
	rdSetShader(0);

	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
	rdSetSortingMethod(0);
}

// todo: need to split this into before and after water
void sithRender_RenderLevelGeometry()
{
	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
	if (sithRender_flag & 0x80)
		rdSetVertexColorMode(1);
	rdSetSortingMethod(0);

	rdClipFrustum* clipFrustum = rdCamera_pCurCamera->pClipFrustum;
	for (int i = 0; i < sithRender_numSectors; ++i)
	{
		sithSector* pSector = sithRender_aSectors[i];
		if (pSector->flags & SITH_SECTOR_BACKDROP)
			continue;

		if (sithRender_lightingIRMode)
		{
			rdVector3 ambient;
			ambient.x = ambient.y = ambient.z = sithRender_f_83198C;
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &ambient);
			rdAmbient_Zero(&rdCamera_pCurCamera->ambientSH);
		}
		else
		{
			float baseLight = pSector->extraLight + sithRender_008d4098;
			rdVector3 ambient;
			ambient.x = stdMath_Clamp(baseLight + pSector->ambientRGB.x, 0.0, 1.0);
			ambient.y = stdMath_Clamp(baseLight + pSector->ambientRGB.y, 0.0, 1.0);
			ambient.z = stdMath_Clamp(baseLight + pSector->ambientRGB.z, 0.0, 1.0);
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &ambient);
			rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &pSector->ambientSH);
		}
		rdColormap_SetCurrent(pSector->colormap);
		//int v68 = pSector->colormap == sithWorld_pCurrentWorld->colormaps;
		//rdSetProcFaceUserData(pSector->id);

		//if ((pSector->flags & SITH_SECTOR_UNDERWATER) && !(sithCamera_currentCamera->sector->flags & SITH_SECTOR_UNDERWATER))
		//{
		//	rdVector4 fog = { pSector->tint.x, pSector->tint.y, pSector->tint.z, 1.0f };
		//
		//	rdSetFogMode(RD_FOG_ENABLED);
		//
		//	rdVector3 halfFog;
		//	halfFog.x = fog.x * 0.5f;
		//	halfFog.y = fog.y * 0.5f;
		//	halfFog.z = fog.z * 0.5f;
		//
		//	fog.x = fog.x - (halfFog.z + halfFog.y);
		//	fog.y = fog.y - (halfFog.x + halfFog.y);
		//	fog.z = fog.z - (halfFog.x + halfFog.z);
		//
		//	rdFogColorf(fog.x, fog.y, fog.z, fog.w);
		//	rdFogRange(0.0f, 5.0f);
		//	rdFogAnisotropy(0.35f);
		//}
		//else
		//{
		//	sithRender_SetCameraFog();
		//}

		//rdScissorMode(RD_SCISSOR_ENABLED);
		//rdScissorf(pSector->clipFrustum->x, pSector->clipFrustum->y, pSector->clipFrustum->width, pSector->clipFrustum->height);

		sithSurface* surface = pSector->surfaces;
		for (int v75 = 0; v75 < pSector->numSurfaces; ++surface, v75++)
		{
			if ((surface->surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY))
				&& sithWorld_pCurrentWorld->backdropSector
				&& sithWorld_pCurrentWorld->backdropSector != surface->parent_sector
				)
			{
				continue;
			}

			if (!surface->surfaceInfo.face.geometryMode)
				continue;

			rdVector3* vertices_alloc = sithWorld_pCurrentWorld->vertices;
			if ((sithCamera_currentCamera->vec3_1.z - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].z) * surface->surfaceInfo.face.normal.z
				+ (sithCamera_currentCamera->vec3_1.y - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].y) * surface->surfaceInfo.face.normal.y
				+ (sithCamera_currentCamera->vec3_1.x - vertices_alloc[*surface->surfaceInfo.face.vertexPosIdx].x) * surface->surfaceInfo.face.normal.x <= 0.0)
				continue;

			rdVector3 screenPos;
			rdMatrix_TransformPoint34(&screenPos, &surface->center, &rdCamera_pCurCamera->view_matrix);
			int clippingIdk = rdClip_SphereInFrustrum(pSector->clipFrustum, &screenPos, surface->radius);
			if (clippingIdk == 2)
				continue;

			rdMaterial* surfaceMat = surface->surfaceInfo.face.material;
			rdTexinfo* texInfo = NULL;
			if (surfaceMat)
			{
				if (surface->surfaceInfo.face.wallCel == -1)
					texInfo = surfaceMat->texinfos[surfaceMat->celIdx];
				else
					texInfo = surfaceMat->texinfos[surface->surfaceInfo.face.wallCel];
			}

			if (surface->adjoin && surfaceMat
				&& ((surface->surfaceInfo.face.type & 2) != 0) // surface is translucent
				// || (texInfo->header.texture_type & 8) != 0 && (texInfo->texture_ptr->alpha_en & 1) != 0) // surface is texture + alpha test
			)
			{
				if (sithRender_numSurfaces < SITH_MAX_VISIBLE_ALPHA_SURFACES)
					sithRender_aSurfaces[sithRender_numSurfaces++] = surface;
				continue;
			}
			
			rdSortDistance(rdVector_Dist3(&sithCamera_currentCamera->vec3_1, &sithWorld_pCurrentWorld->vertices[*surface->surfaceInfo.face.vertexPosIdx]));
			sithRender_DrawSurface(surface);
		}

		rdSetProcFaceUserData(pSector->id | 0x10000);

		int safeguard = 0;
		for (sithThing* pThing = pSector->thingsList; pThing; pThing = pThing->nextThing)
		{
			if (++safeguard >= SITH_MAX_THINGS)
				break;

			if (!(pThing->thingflags & SITH_TF_LEVELGEO))
				continue;

			if (pThing->thingflags & (SITH_TF_DISABLED | SITH_TF_INVISIBLE | SITH_TF_WILLBEREMOVED))
				continue;

#ifdef RENDER_DROID2
			// decals are rendered before everything else in sithRender_RenderOccludersAndDecals
			if (pThing->rdthing.type == RD_THINGTYPE_DECAL)
				continue;
#endif

			if (!sithRender_ShouldRenderCameraThing(pThing))
			//if (!((sithCamera_currentCamera->cameraPerspective & 0xFC) != 0 || pThing != sithCamera_currentCamera->primaryFocus))
				continue;

			if (pThing->rdthing.type != RD_THINGTYPE_MODEL)
				continue;

			rdMatrix_TransformPoint34(&pThing->screenPos, &pThing->position, &rdCamera_pCurCamera->view_matrix);
			pThing->rdthing.clippingIdk = rdClip_SphereInFrustrum(pSector->clipFrustum, &pThing->screenPos, pThing->rdthing.model3->radius);
			if (pThing->rdthing.clippingIdk == 2)
				continue;

			// Added: set the geoset to 0
			// todo: geoset selection, perhaps it needs to be in sithRender_RenderThing?
			if (((pThing->rdthing).type == RD_THINGTYPE_MODEL))
				pThing->rdthing.model3->geosetSelect = 0;

			// MOTS added
#ifdef JKM_LIGHTING
			if ((pThing->archlightIdx != -1) && ((pThing->rdthing).type == RD_THINGTYPE_MODEL))
			{
				rdModel3* pModel = pThing->rdthing.model3;
				for (int k = 0; k < pModel->numGeosets; k++)
				{
					for (int j = 0; j < pModel->geosets[k].numMeshes; j++)
					{
						if (rdGetVertexColorMode() == 0)
						{
							pModel->geosets[k].meshes[j].vertices_unk = pModel->geosets[k].meshes[j].vertices_i;
							pModel->geosets[k].meshes[j].vertices_i = sithWorld_pCurrentWorld->aArchlights[pThing->archlightIdx].aMeshes[j].aMono;
						}
						else
						{
							pModel->geosets[k].meshes[j].paRedIntensities = sithWorld_pCurrentWorld->aArchlights[pThing->archlightIdx].aMeshes[j].aRed;
							pModel->geosets[k].meshes[j].paGreenIntensities = sithWorld_pCurrentWorld->aArchlights[pThing->archlightIdx].aMeshes[j].aGreen;
							pModel->geosets[k].meshes[j].paBlueIntensities = sithWorld_pCurrentWorld->aArchlights[pThing->archlightIdx].aMeshes[j].aBlue;
						}
					}
				}
			}
			if ((pThing->archlightIdx == -1) && (rdGetVertexColorMode() == 1))
			{
				rdModel3* pModel = pThing->rdthing.model3;
				for (int k = 0; k < pModel->numGeosets; k++)
				{
					for (int j = 0; j < pModel->geosets[k].numMeshes; j++)
					{
						pModel->geosets[k].meshes[j].paRedIntensities = pModel->geosets[k].meshes[j].vertices_i;
						pModel->geosets[k].meshes[j].paGreenIntensities = pModel->geosets[k].meshes[j].vertices_i;
						pModel->geosets[k].meshes[j].paBlueIntensities = pModel->geosets[k].meshes[j].vertices_i;
					}
				}
			}
#endif // JKM_LIGHTING
			if (sithRender_RenderThing(pThing))
				++sithRender_geoThingsDrawn;

			// MOTS added
#ifdef JKM_LIGHTING
			if (((pThing->archlightIdx != -1) && (pThing->rdthing.type == RD_THINGTYPE_MODEL)) && (rdGetVertexColorMode() == 0))
			{
				rdModel3* pModel = pThing->rdthing.model3;
				for (int k = 0; k < pModel->numGeosets; k++)
				{
					for (int j = 0; j < pModel->geosets[k].numMeshes; j++)
					{
						pModel->geosets[k].meshes[j].vertices_i = pModel->geosets[k].meshes[j].vertices_unk;
					}
				}
			}
#endif
		}
		++sithRender_sectorsDrawn;
	}

	rdCache_Flush("sithRender_RenderLevelGeometry");
	rdCamera_pCurCamera->pClipFrustum = clipFrustum;

	rdScissorMode(RD_SCISSOR_DISABLED);
}
#else

#ifdef TARGET_TWL
// TODO: clean this up of ifdefs
void sithRender_NoClip(sithSector *sector, rdClipFrustum *frustumArg, flex_t a3, int depth)
{
    int v5; // ecx
    rdClipFrustum *frustum; // edx
    sithThing *thing; // esi
    unsigned int lightIdx; // ecx
    sithAdjoin *adjoinIter; // ebx
    sithSurface *adjoinSurface; // esi
    rdMaterial *adjoinMat; // eax
    rdVector3 *v20; // eax
    int v25; // eax
    unsigned int v27; // edi
    rdClipFrustum *v31; // ecx
    rdClipFrustum outClip; // [esp+Ch] [ebp-74h] BYREF
    rdVector3 vertex_out; // [esp+40h] [ebp-40h] BYREF
    int v45; // [esp+4Ch] [ebp-34h]
    rdTexinfo *v51; // [esp+64h] [ebp-1Ch]

    // Clip visited hardening
    // Does not help much, but no visual harm either
#ifdef QOL_IMPROVEMENTS
    if (sector->clipVisited == sithRender_lastRenderTick || sector->renderTick == sithRender_lastRenderTick) {
        return;
    }
#endif

    if ( sector->renderTick == sithRender_lastRenderTick )
    {
        sector->clipFrustum = rdCamera_pCurCamera->pClipFrustum;
    }
    else
    {
        //stdPlatform_Printf("Render sector %u %x %u\n", sector->id, sithRender_lastRenderTick, depth);

        sector->renderTick = sithRender_lastRenderTick;
        sector->clipVisited = 0;

        // Added: Prevent crashing
        if (sithRender_numSectors >= SITH_MAX_VISIBLE_SECTORS) {
            jk_printf("OpenJKDF2: Hit max visible sectors.\n");
            return;
        }

        // Added: Prevent crashing
        if (sithRender_numClipFrustums >= SITH_MAX_VISIBLE_SECTORS) {
            jk_printf("OpenJKDF2: Hit max visible sector clip frustums.\n");
            return;
        }

        // Added: Prevent crashing
        if (sithRender_numSectors2 >= SITH_MAX_VISIBLE_SECTORS_2) {
            jk_printf("OpenJKDF2: Hit max visible sectors (2).\n");
            return;
        }

        sithRender_aSectors[sithRender_numSectors++] = sector;
        if (!(sector->flags & SITH_SECTOR_AUTOMAPVISIBLE) && !(g_debugmodeFlags & DEBUGFLAG_NOCLIP)) // Added: don't send sighted stuff in noclip, otherwise the whole map reveals
        {
            sector->flags |= SITH_SECTOR_AUTOMAPVISIBLE;
            if ( (sector->flags & SITH_SECTOR_COGLINKED) != 0 )
                sithCog_SendMessageFromSector(sector, 0, SITH_MESSAGE_SIGHTED);
        }
        frustum = &sithRender_clipFrustums[sithRender_numClipFrustums++];
        _memcpy(frustum, frustumArg, sizeof(rdClipFrustum));
        thing = sector->thingsList;
        sector->clipFrustum = frustum;
        lightIdx = sithRender_numLights;

        // Added: safety
        int safeguard = 0;
        while ( thing )
        {
            if ( lightIdx >= 0x20 )
                break;

            // Added: safety
            if (++safeguard >= SITH_MAX_THINGS)
                break;

            // Debug, add extra light from player
#if 0
            if (thing->type == SITH_THING_PLAYER)
            {
                rdMatrix_TransformPoint34(&vertex_out, &thing->actorParams.lightOffset, &thing->lookOrientation);
                rdVector_Add3Acc(&vertex_out, &thing->position);
                sithRender_aLights[sithRender_numLights].intensity = 1.0;//thing->actorParams.lightIntensity;
                rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &vertex_out);
                lightIdx = ++sithRender_numLights;
            }
#endif

            if ((thing->thingflags & SITH_TF_LIGHT)
                 && !(thing->thingflags & (SITH_TF_DISABLED| SITH_TF_INVISIBLE |SITH_TF_WILLBEREMOVED)))
            {
                if ( thing->light > 0.0 )
                {
                    sithRender_aLights[lightIdx].intensity = thing->light;
                    rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[lightIdx], &thing->position);
                    lightIdx = ++sithRender_numLights;
                }

                if ( (thing->type == SITH_THING_ACTOR || thing->type == SITH_THING_PLAYER) && lightIdx < 0x20 )
                {
                    if ( (thing->actorParams.typeflags & SITH_AF_FIELDLIGHT) != 0 && thing->actorParams.lightIntensity > 0.0 )
                    {
                        rdMatrix_TransformPoint34(&vertex_out, &thing->actorParams.lightOffset, &thing->lookOrientation);
                        rdVector_Add3Acc(&vertex_out, &thing->position);
                        sithRender_aLights[sithRender_numLights].intensity = thing->actorParams.lightIntensity;
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &vertex_out);
                        lightIdx = ++sithRender_numLights;
                    }
                    if ( thing->actorParams.timeLeftLengthChange > 0.0 )
                    {
                        sithRender_aLights[lightIdx].intensity = thing->actorParams.timeLeftLengthChange;
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[lightIdx], &thing->actorParams.saberBladePos);
                        lightIdx = ++sithRender_numLights;
                    }
                }
            }
            thing = thing->nextThing;
        }
        sithRender_aSectors2[sithRender_numSectors2++] = sector;
    }

    
    //v45 = sector->clipVisited;

    // Clip visited hardening
#ifdef QOL_IMPROVEMENTS
    sector->clipVisited = sithRender_lastRenderTick;
#else
    sector->clipVisited = 1;
#endif

    // Added: safeguard
    for (adjoinIter = sector->adjoins ; adjoinIter != NULL; adjoinIter = adjoinIter->next)
    {
        // Clip visited hardening
        if (adjoinIter->sector->clipVisited == sithRender_lastRenderTick)
        {
            continue;
        }

        // Added: safeguard
        if (++sithRender_adjoinSafeguard >= 0x100000) {
            stdPlatform_Printf("Hit safeguard...\n");
            break;
        }

        adjoinSurface = adjoinIter->surface;

        // Avoid rendering adjoins if they're behind the near clipping plane
        // TODO: Test against TODOA and verify if this is QOL-worthy
#ifdef TARGET_TWL
        if (adjoinIter->timesClipped == sithRender_lastRenderTick) {
            continue;
        }
#endif
        adjoinMat = adjoinSurface->surfaceInfo.face.material;
        if ( adjoinMat )
        {
            int v19 = adjoinSurface->surfaceInfo.face.wallCel;
            if ( v19 == -1 )
                v19 = adjoinMat->celIdx;
            v51 = adjoinMat->texinfos[v19]; 
        }
        else {
            v51 = NULL; // Added. TODO: does setting this to NULL cause issues?
        }

        v20 = &sithWorld_pCurrentWorld->vertices[*adjoinSurface->surfaceInfo.face.vertexPosIdx];
        flex_t dist = (sithCamera_currentCamera->vec3_1.y - v20->y) * adjoinSurface->surfaceInfo.face.normal.y
                   + (sithCamera_currentCamera->vec3_1.z - v20->z) * adjoinSurface->surfaceInfo.face.normal.z
                   + (sithCamera_currentCamera->vec3_1.x - v20->x) * adjoinSurface->surfaceInfo.face.normal.x;
        flex_t adjoinDistAdd = adjoinIter->dist + adjoinIter->mirror->dist + a3;

        // Avoid rendering adjoins if they're far enough away
#ifdef TARGET_TWL
        // adjoinDistAdd compare GREATLY reduces recursion issues 
        // TODO: Test against TODOA and verify if this is QOL-worthy
        if (/*(adjoinDistAdd > 3.5) ||*/ (dist > 3.0)) {
            // Doesn't help, causes visual issues
            //adjoinIter->sector->clipVisited = sithRender_lastRenderTick;
            adjoinIter->timesClipped = sithRender_lastRenderTick;
            adjoinIter->mirror->timesClipped = sithRender_lastRenderTick;

            continue;
        }
#endif

        if ( dist > 0.0 || (dist == 0.0 && sector == sithCamera_currentCamera->sector))
        {
            int bAdjoinIsTransparent = (((!adjoinSurface->surfaceInfo.face.material ||
                        (adjoinSurface->surfaceInfo.face.geometryMode == 0)) ||
                       ((adjoinSurface->surfaceInfo.face.type & 2))) ||
                      (v51 && (v51->header.texture_type & 8) && (v51->texture_ptr->alpha_en & 1))
                      );

#ifdef QOL_IMPROVEMENTS
            // Added: Somehow the clipping changed enough to cause a bug in MoTS Lv12.
            // The ground under the water surface somehow renders.
            // As a mitigation, if a mirror surface is transparent but the top-layer isn't,
            // we will render underneath anyways.
            sithSurface* adjoinMirrorSurface = adjoinIter->mirror->surface;
            rdMaterial* adjoinMirrorMat = adjoinMirrorSurface->surfaceInfo.face.material;
            rdTexinfo* adjoinMirrorTexinfo = NULL;
            if ( adjoinMirrorMat )
            {
                int v19 = adjoinMirrorSurface->surfaceInfo.face.wallCel;
                if ( v19 == -1 )
                    v19 = adjoinMirrorMat->celIdx;
                adjoinMirrorTexinfo = adjoinMirrorMat->texinfos[v19]; 
            }
            else {
                adjoinMirrorTexinfo = NULL; // Added. TODO: does setting this to NULL cause issues?
            }

            int bMirrorAdjoinIsTransparent = (((!adjoinMirrorSurface->surfaceInfo.face.material ||
                        (adjoinMirrorSurface->surfaceInfo.face.geometryMode == RD_GEOMODE_NOTRENDERED)) ||
                       ((adjoinMirrorSurface->surfaceInfo.face.type & 2))) ||
                      (adjoinMirrorTexinfo && (adjoinMirrorTexinfo->header.texture_type & 8) && (adjoinMirrorTexinfo->texture_ptr->alpha_en & 1))
                      );

            bAdjoinIsTransparent |= bMirrorAdjoinIsTransparent;
#endif

            if ((adjoinIter->flags & 1) && bAdjoinIsTransparent) 
            {
                v31 = frustumArg;
                
                v31 = &outClip;
                outClip = *frustumArg;
                adjoinIter->timesClipped = sithRender_lastRenderTick;
                adjoinIter->mirror->timesClipped = sithRender_lastRenderTick;

                // Added: noclip
                if (sithPlayer_bNoClippingRend) continue;
                
                // wtf is with this float?
                if (!(sithRender_flag & 4) || adjoinDistAdd < sithRender_f_82F4B0 ) {
                    //stdPlatform_Printf("Render sector %u %x %u\n", adjoinIter->sector->id, sithRender_lastRenderTick, depth);
                    sithRender_NoClip(adjoinIter->sector, v31, adjoinDistAdd, depth+1);
                }
            }
        }
    }
    //sector->clipVisited = v45;
}
#endif

// MOTS altered
void sithRender_RenderLevelGeometry()
{
    rdVector2 *vertices_uvs; // edx
    rdVector3 *vertices_alloc; // esi
    rdTexinfo *v10; // ecx
    int v18; // ebx
    int v19; // ebp
    rdProcEntry *v20; // esi
    int v21; // eax
    rdLightMode_t lightMode2; // eax
    int v23; // ecx
    int v24; // eax
    unsigned int v28; // ebp
#ifdef RGB_AMBIENT
	rdVector3 v29;
#else
    flex_t v29; // ecx
#endif
    flex_t *v31; // eax
    unsigned int v32; // ecx
    flex_t *v33; // edx
    flex_d_t v34; // st7
    int v38; // ecx
    char v39; // al
    rdProcEntry *procEntry; // esi
    rdGeoMode_t geoMode; // eax
    rdLightMode_t lightMode; // eax
    rdTexMode_t texMode; // ecx
    rdTexMode_t texMode2; // eax
    unsigned int num_vertices; // ebp
#ifdef RGB_AMBIENT
	rdVector3 v49;
#else
    flex_t v49; // edx
#endif
    flex_t *v51; // eax
    unsigned int v52; // ecx
    flex_t *v53; // edx
    flex_d_t v54; // st7
    int surfaceFlags; // eax
    int v57; // edx
    rdMaterial *v58; // ecx
    int v59; // ecx
    char rend_flags; // al
    sithThing *i; // esi
    int v63; // eax
    rdTexMode_t texMode3; // [esp-10h] [ebp-74h]
    sithSurface *v65; // [esp+10h] [ebp-54h]
    flex_t v66; // [esp+14h] [ebp-50h]
    flex_t v67; // [esp+14h] [ebp-50h]
    BOOL v68; // [esp+18h] [ebp-4Ch]
    sithSector *level_idk; // [esp+1Ch] [ebp-48h]
#ifdef RGB_AMBIENT
	rdVector3 a2;
#else
    flex_t a2; // [esp+20h] [ebp-44h]
#endif
	int v71; // [esp+24h] [ebp-40h]
    int v72; // [esp+28h] [ebp-3Ch]
    rdTexinfo *v73; // [esp+2Ch] [ebp-38h]
    int v74; // [esp+30h] [ebp-34h]
    int v75; // [esp+34h] [ebp-30h]
    signed int v76; // [esp+38h] [ebp-2Ch]
    rdClipFrustum *v77; // [esp+3Ch] [ebp-28h]
    int v78[3]; // [esp+40h] [ebp-24h] BYREF
    int v79[3]; // [esp+4Ch] [ebp-18h] BYREF
    flex_t v80[3]; // [esp+58h] [ebp-Ch] BYREF
    flex_t tmpBlue[3];
    flex_t tmpGreen[3];

#ifdef TARGET_TWL
	int skip_this_surface = 1;
	//rdroid_curAcceleration = 1;
#endif

#ifdef USE_LIGHT_JOBS
	// wait for any lighting jobs to finish
	stdJob_Wait();
#endif

#ifdef TILE_SW_RASTER
	// TILETODO
	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
	rdSetVertexColorMode(0);
#else

    if ( rdroid_curAcceleration )
    {
        rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
        if (sithRender_flag & 0x80) {
            rdSetVertexColorMode(1);
        }
    }
    else
    {
        rdSetZBufferMethod(RD_ZBUFFER_NOREAD_WRITE);
        if ( (sithRender_flag & 0x20) != 0 )
            rdSetOcclusionMethod(0);
        else
            rdSetOcclusionMethod(1);
        rdSetVertexColorMode(0);
    }
#endif
    rdSetSortingMethod(0);

#ifdef TARGET_TWL
    //rdSetVertexColorMode(0);
    //sithRender_SetLightMode(RD_LIGHTMODE_DIFFUSE);
    //printf("%x %x %x %x\n", rdroid_curVertexColorMode, sithRender_flag, rdroid_curAcceleration, sithRender_lightMode);
#endif

    vertices_uvs = sithWorld_pCurrentWorld->vertexUVs;
    sithRender_idxInfo.vertices = sithWorld_pCurrentWorld->verticesTransformed;
    sithRender_idxInfo.paDynamicLight = sithWorld_pCurrentWorld->verticesDynamicLight;
#ifdef RGB_THING_LIGHTS
	sithRender_idxInfo.paDynamicLightR = sithWorld_pCurrentWorld->verticesDynamicLightR;
	sithRender_idxInfo.paDynamicLightG = sithWorld_pCurrentWorld->verticesDynamicLightG;
	sithRender_idxInfo.paDynamicLightB = sithWorld_pCurrentWorld->verticesDynamicLightB;
#endif
    sithRender_idxInfo.vertexUVs = vertices_uvs;
    v77 = rdCamera_pCurCamera->pClipFrustum;

    for (v72 = 0; v72 < sithRender_numSectors; v72++)
    {
        // Surfaces are 13ms on landing terminal spawn
        level_idk = sithRender_aSectors[v72];
#ifdef TARGET_TWL
        level_idk->clipVisited = 0;
        if (level_idk->geoRenderTick == sithRender_lastRenderTick) {
            continue;
        }
        level_idk->geoRenderTick = sithRender_lastRenderTick;
        level_idk->clipFrustum = rdCamera_pCurCamera->pClipFrustum;
#endif
        if ( sithRender_lightingIRMode )
        {
#ifdef RGB_AMBIENT
			a2.x = a2.y = a2.z = sithRender_f_83198C;
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &a2);
			rdAmbient_Zero(&rdCamera_pCurCamera->ambientSH);
#else
            a2 = sithRender_f_83198C;
            rdCamera_SetAmbientLight(rdCamera_pCurCamera, sithRender_f_83198C);
#endif
        }
        else
        {
#ifdef RGB_AMBIENT
			float baseLight = level_idk->extraLight + sithRender_008d4098;
			a2.x = stdMath_Clamp(baseLight + level_idk->ambientRGB.x, 0.0, 1.0);
			a2.y = stdMath_Clamp(baseLight + level_idk->ambientRGB.y, 0.0, 1.0);
			a2.z = stdMath_Clamp(baseLight + level_idk->ambientRGB.z, 0.0, 1.0);
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &a2);
			rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &level_idk->ambientSH);
#else
            flex_t baseLight = level_idk->ambientLight + level_idk->extraLight + sithRender_008d4098;
            a2 = stdMath_Clamp(baseLight, 0.0, 1.0);
            rdCamera_SetAmbientLight(rdCamera_pCurCamera, a2);
#endif
		}
        rdColormap_SetCurrent(level_idk->colormap);
        v68 = level_idk->colormap == sithWorld_pCurrentWorld->colormaps;
        rdSetProcFaceUserData(level_idk->id);
        v65 = level_idk->surfaces;

		for (v75 = 0; v75 < level_idk->numSurfaces; v65->field_4 = sithRender_lastRenderTick, ++v65, v75++)
        {
            if ( !v65->surfaceInfo.face.geometryMode )
                continue;
            vertices_alloc = sithWorld_pCurrentWorld->vertices;

            // TODO macro/vector func?
            flex_t dist = (sithCamera_currentCamera->vec3_1.z - vertices_alloc[*v65->surfaceInfo.face.vertexPosIdx].z) * v65->surfaceInfo.face.normal.z
               + (sithCamera_currentCamera->vec3_1.y - vertices_alloc[*v65->surfaceInfo.face.vertexPosIdx].y) * v65->surfaceInfo.face.normal.y
               + (sithCamera_currentCamera->vec3_1.x - vertices_alloc[*v65->surfaceInfo.face.vertexPosIdx].x) * v65->surfaceInfo.face.normal.x;
            if (dist <= 0.0 )
                continue;
#ifdef TARGET_TWL
            if (dist > 3.0) {
                continue;
            }
#endif

            rdMaterial* surfaceMat = v65->surfaceInfo.face.material;
            if ( surfaceMat )
            {
                if ( v65->surfaceInfo.face.wallCel == -1 )
                    v10 = surfaceMat->texinfos[surfaceMat->celIdx];
                else
                    v10 = surfaceMat->texinfos[v65->surfaceInfo.face.wallCel];
                v73 = v10;
            }
            else
            {
                v10 = v73;
            }

            if ( v65->adjoin && surfaceMat && ((v65->surfaceInfo.face.type & 2) != 0 || (v10->header.texture_type & 8) != 0 && (v10->texture_ptr->alpha_en & 1) != 0) )
            {
                if (sithRender_numSurfaces < SITH_MAX_VISIBLE_ALPHA_SURFACES)
                {
                    sithRender_aSurfaces[sithRender_numSurfaces++] = v65;
                }
                continue;
            }

			if ( v65->field_4 != sithRender_lastRenderTick )
            {
                for (int j = 0; j < v65->surfaceInfo.face.numVertices; j++)
                {
                    int idx = v65->surfaceInfo.face.vertexPosIdx[j];
                    if ( sithWorld_pCurrentWorld->alloc_unk98[idx] != sithRender_lastRenderTick )
                    {
                        rdMatrix_TransformPoint34(&sithWorld_pCurrentWorld->verticesTransformed[idx], &sithWorld_pCurrentWorld->vertices[idx], &rdCamera_pCurCamera->view_matrix);
                        sithWorld_pCurrentWorld->alloc_unk98[idx] = sithRender_lastRenderTick;
                    }
                }
                v65->field_4 = sithRender_lastRenderTick;
            }

            // Render sky vertices specifically?
            if ( (sithRender_flag & 8) == 0 || v65->surfaceInfo.face.numVertices <= 3 || (v65->surfaceFlags & (SITH_SURFACE_CEILING_SKY|SITH_SURFACE_HORIZON_SKY)) != 0 || !v65->surfaceInfo.face.lightingMode )
            {
                procEntry = rdCache_GetProcEntry();
                if ( !procEntry )
                    continue;
                if ( (v65->surfaceFlags & (SITH_SURFACE_HORIZON_SKY|SITH_SURFACE_CEILING_SKY)) != 0 )
                {
#ifdef RENDER_DROID2
					++sithRender_numSkySurfacesDrawn;
#endif

                    geoMode = sithRender_geoMode;
                    if ( sithRender_geoMode > RD_GEOMODE_SOLIDCOLOR)
                        geoMode = RD_GEOMODE_SOLIDCOLOR;
                }
                else
                {
                    geoMode = v65->surfaceInfo.face.geometryMode;
                    if ( geoMode >= sithRender_geoMode )
                        geoMode = sithRender_geoMode;
                }
                procEntry->geometryMode = geoMode;
                lightMode = v65->surfaceInfo.face.lightingMode;
                if ( sithRender_lightingIRMode )
                {
                    if ( lightMode >= RD_LIGHTMODE_DIFFUSE)
                        lightMode = RD_LIGHTMODE_DIFFUSE;
                }
                else if ( lightMode >= sithRender_lightMode )
                {
                    lightMode = sithRender_lightMode;
                }
                texMode = sithRender_texMode;
                procEntry->lightingMode = lightMode;
                texMode2 = v65->surfaceInfo.face.textureMode;
                if ( texMode2 >= texMode )
                    texMode2 = texMode;
                procEntry->textureMode = texMode2;
                meshinfo_out.verticesProjected = sithRender_aVerticesTmp;
                meshinfo_out.paDynamicLight = procEntry->vertexIntensities;
#ifdef RGB_THING_LIGHTS
				meshinfo_out.paDynamicLightR = procEntry->paRedIntensities;
				meshinfo_out.paDynamicLightG = procEntry->paGreenIntensities;
				meshinfo_out.paDynamicLightB = procEntry->paBlueIntensities;
#endif
                sithRender_idxInfo.vertexPosIdx = v65->surfaceInfo.face.vertexPosIdx;
                meshinfo_out.vertexUVs = procEntry->vertexUVs;
                sithRender_idxInfo.numVertices = v65->surfaceInfo.face.numVertices;
                texMode3 = texMode2;
                sithRender_idxInfo.vertexUVIdx = v65->surfaceInfo.face.vertexUVIdx;
                
                // MOTS added
                if (rdGetVertexColorMode() == 0) {
                    sithRender_idxInfo.intensities = v65->surfaceInfo.intensities;
                    rdPrimit3_ClipFace(level_idk->clipFrustum, 
                                       procEntry->geometryMode, 
                                       procEntry->lightingMode, 
                                       texMode3, 
                                       &sithRender_idxInfo, 
                                       &meshinfo_out, 
                                       &v65->surfaceInfo.face.clipIdk);
                }
                else 
                {
                    if ((v65->surfaceFlags & SITH_SURFACE_1000000) == 0) {
                        sithRender_idxInfo.paRedIntensities = (v65->surfaceInfo).intensities;
                        sithRender_idxInfo.paGreenIntensities = sithRender_idxInfo.paRedIntensities;
                        sithRender_idxInfo.paBlueIntensities = sithRender_idxInfo.paRedIntensities;
                    }
                    else {
                        sithRender_idxInfo.paRedIntensities =
                             (v65->surfaceInfo).intensities +
                             sithRender_idxInfo.numVertices;

                        sithRender_idxInfo.paGreenIntensities =
                             sithRender_idxInfo.paRedIntensities +
                             sithRender_idxInfo.numVertices;

                        sithRender_idxInfo.paBlueIntensities =
                             sithRender_idxInfo.paGreenIntensities +
                             sithRender_idxInfo.numVertices;
                    }
                    meshinfo_out.paGreenIntensities = procEntry->paGreenIntensities;
                    meshinfo_out.paRedIntensities = procEntry->paRedIntensities;
                    meshinfo_out.paBlueIntensities = procEntry->paBlueIntensities;
                    rdPrimit3_ClipFaceRGBLevel
                              (level_idk->clipFrustum,
                               procEntry->geometryMode,
                               procEntry->lightingMode,
                               texMode3,
                               &sithRender_idxInfo,
                               &meshinfo_out,
                               &(v65->surfaceInfo).face.clipIdk);
                }
                
                num_vertices = meshinfo_out.numVertices;
                if ( meshinfo_out.numVertices < 3u )
                {
                    continue;
                }
#ifdef VIEW_SPACE_GBUFFER
				memcpy(procEntry->vertexVS, sithRender_aVerticesTmp, sizeof(rdVector3) * meshinfo_out.numVertices);
#endif
                rdCamera_pCurCamera->fnProjectLst(procEntry->vertices, sithRender_aVerticesTmp, meshinfo_out.numVertices);

                if ( sithRender_lightingIRMode )
                {
#ifdef RGB_AMBIENT
					v49.x = v49.y = v49.z = sithRender_f_83198C;
					procEntry->light_level_static = 0.0;
					rdVector_Copy3(&procEntry->ambientLight, &v49);
#else
                    v49 = sithRender_f_83198C;
                    procEntry->light_level_static = 0.0;
                    procEntry->ambientLight = v49;
#endif
                }
                else
                {
#ifdef RGB_AMBIENT
					procEntry->ambientLight.x = procEntry->ambientLight.y = procEntry->ambientLight.z = stdMath_Clamp(level_idk->extraLight + sithRender_008d4098, 0.0, 1.0);
#else
                    procEntry->ambientLight = stdMath_Clamp(level_idk->extraLight + sithRender_008d4098, 0.0, 1.0);
#endif
                }
#ifdef RGB_AMBIENT
				if (procEntry->ambientLight.x >= 1.0 && procEntry->ambientLight.y >= 1.0 && procEntry->ambientLight.z >= 1.0)
#else
                if ( procEntry->ambientLight >= 1.0 )
#endif
				{
                    if ( v68 )
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_FULLYLIT;
                    }
                    else
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_DIFFUSE;
                        procEntry->light_level_static = 1.0;
                    }
                }
                else if ( procEntry->lightingMode == RD_LIGHTMODE_DIFFUSE)
                {
                    if ( procEntry->light_level_static >= 1.0 && v68 )
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_FULLYLIT;
                    }
                    else if ( procEntry->light_level_static <= 0.0 )
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_NOTLIT;
                    }
                }
                else if ( (rdGetVertexColorMode() == 0) && procEntry->lightingMode == RD_LIGHTMODE_GOURAUD)
                {
                    v51 = procEntry->vertexIntensities;
                    v67 = *v51;
                    v52 = 1;
                    if ( num_vertices > 1 )
                    {
                        v53 = v51 + 1;
                        do
                        {
                            v54 = stdMath_Fabs(*v53 - v67);
                            if ( v54 > 0.015625 )
                                break;
                            ++v52;
                            ++v53;
                        }
                        while ( v52 < num_vertices );
                    }
                    if ( v52 != num_vertices )
                    {
                        
                    }
                    else if ( v67 == 1.0 )
                    {
                        if ( v68 )
                        {
                            procEntry->lightingMode = RD_LIGHTMODE_FULLYLIT;
                        }
                        else
                        {
                            procEntry->lightingMode = RD_LIGHTMODE_DIFFUSE;
                            procEntry->light_level_static = 1.0;
                        }
                    }
                    else if ( v67 == 0.0 )
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_NOTLIT;
                        procEntry->light_level_static = 0.0;
                    }
                    else
                    {
                        procEntry->lightingMode = RD_LIGHTMODE_DIFFUSE;
                        procEntry->light_level_static = v67;
                    }
                }

                surfaceFlags = v65->surfaceFlags;
                if ( (surfaceFlags & SITH_SURFACE_HORIZON_SKY) != 0 )
                {
                    sithRenderSky_TransformHorizontal(procEntry, &v65->surfaceInfo, num_vertices);
                }
                else if ( (surfaceFlags & SITH_SURFACE_CEILING_SKY) != 0 )
                {
                    sithRenderSky_TransformVertical(procEntry, &v65->surfaceInfo, sithRender_aVerticesTmp, num_vertices);
                }
                v57 = v65->surfaceInfo.face.type;
                procEntry->wallCel = v65->surfaceInfo.face.wallCel;
                v58 = v65->surfaceInfo.face.material;
                procEntry->extralight = v65->surfaceInfo.face.extraLight;
                procEntry->material = v58;
                v59 = procEntry->geometryMode;
                procEntry->light_flags = 0;
                procEntry->type = v57;
                rend_flags = 1;
                if ( v59 >= 4 )
                    rend_flags = 3;
                if ( procEntry->lightingMode >= RD_LIGHTMODE_GOURAUD)
                    rend_flags |= 4u;

                rdCache_AddProcFace(0, num_vertices, rend_flags);
                continue;
            }

            v74 = 0;
            v76 = v65->surfaceInfo.face.numVertices - 2;
            if (v76 > 0)
            {
                v18 = v65->surfaceInfo.face.numVertices - 1;
                v71 = 1;
                v19 = 0;
                while ( 2 )
                {
                    v20 = rdCache_GetProcEntry();
                    if ( !v20 )
                        goto LABEL_92;
                    v21 = v65->surfaceInfo.face.geometryMode;
                    if ( v21 >= sithRender_geoMode )
                        v21 = sithRender_geoMode;
                    v20->geometryMode = v21;
                    lightMode2 = v65->surfaceInfo.face.lightingMode;
                    if ( sithRender_lightingIRMode )
                    {
                        if ( lightMode2 >= RD_LIGHTMODE_DIFFUSE)
                            lightMode2 = RD_LIGHTMODE_DIFFUSE;
                    }
                    else if ( lightMode2 >= sithRender_lightMode )
                    {
                        lightMode2 = sithRender_lightMode;
                    }
                    v23 = sithRender_texMode;
                    v20->lightingMode = lightMode2;
                    v24 = v65->surfaceInfo.face.textureMode;
                    if ( v24 >= v23 )
                        v24 = v23;
                    v20->textureMode = v24;
                    v78[0] = v65->surfaceInfo.face.vertexPosIdx[v19];
                    v78[1] = v65->surfaceInfo.face.vertexPosIdx[v71];
                    v78[2] = v65->surfaceInfo.face.vertexPosIdx[v18];
                    if ( v20->geometryMode >= RD_GEOMODE_TEXTURED)
                    {
                        v79[0] = v65->surfaceInfo.face.vertexUVIdx[v19];
                        v79[1] = v65->surfaceInfo.face.vertexUVIdx[v71];
                        v79[2] = v65->surfaceInfo.face.vertexUVIdx[v18];
                    }
                    meshinfo_out.verticesProjected = sithRender_aVerticesTmp;
                    sithRender_idxInfo.numVertices = 3;
                    meshinfo_out.vertexUVs = v20->vertexUVs;
                    sithRender_idxInfo.vertexPosIdx = v78;
                    meshinfo_out.paDynamicLight = v20->vertexIntensities;
#ifdef RGB_THING_LIGHTS
					meshinfo_out.paDynamicLightR = v20->paRedIntensities;
					meshinfo_out.paDynamicLightG = v20->paGreenIntensities;
					meshinfo_out.paDynamicLightB = v20->paBlueIntensities;
#endif
                    sithRender_idxInfo.vertexUVIdx = v79;
                    
                    // MOTS added
                    if (rdGetVertexColorMode() == 0) {
                        v80[0] = v65->surfaceInfo.intensities[v19];
                        v80[1] = v65->surfaceInfo.intensities[v71];
                        v80[2] = v65->surfaceInfo.intensities[v18];
                        sithRender_idxInfo.intensities = v80;
                        rdPrimit3_ClipFace(level_idk->clipFrustum, 
                                           v20->geometryMode, 
                                           v20->lightingMode, 
                                           v20->textureMode, 
                                           &sithRender_idxInfo, 
                                           &meshinfo_out, 
                                           &v65->surfaceInfo.face.clipIdk);
                    }
                    else {
                        

                        if ((v65->surfaceFlags & SITH_SURFACE_1000000) == 0) 
                        {
                            v80[0] = v65->surfaceInfo.intensities[v19];
                            v80[1] = v65->surfaceInfo.intensities[v71];
                            v80[2] = v65->surfaceInfo.intensities[v18];

                            memcpy(tmpBlue, v80, sizeof(flex_t) * 3);
                            memcpy(tmpGreen, v80, sizeof(flex_t) * 3);
                        }
                        else {
                            v80[0] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 1) + v19];
                            v80[1] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 1) + v71];
                            v80[2] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 1) + v18];

                            tmpGreen[0] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 2) + v19];
                            tmpGreen[1] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 2) + v71];
                            tmpGreen[2] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 2) + v18];

                            tmpBlue[0] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 3) + v19];
                            tmpBlue[1] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 3) + v71];
                            tmpBlue[2] = v65->surfaceInfo.intensities[(v65->surfaceInfo.face.numVertices * 3) + v18];
                        }

                        sithRender_idxInfo.paRedIntensities = v80;
                        sithRender_idxInfo.paGreenIntensities = tmpGreen;
                        sithRender_idxInfo.paBlueIntensities = tmpBlue;

                        meshinfo_out.paRedIntensities = v20->paRedIntensities;
                        meshinfo_out.paGreenIntensities = v20->paGreenIntensities;
                        meshinfo_out.paBlueIntensities = v20->paBlueIntensities;

                        rdPrimit3_ClipFaceRGBLevel
                                  (level_idk->clipFrustum,
                                   v20->geometryMode,
                                   v20->lightingMode,
                                   v20->textureMode, 
                                   &sithRender_idxInfo,
                                   &meshinfo_out,
                                   &(v65->surfaceInfo).face.clipIdk);
                    }

                    // Avoid projecting vertices if they're far away enough, skipping sky
                    // vertices because they're important for aesthetics
#ifdef TARGET_TWL
                    skip_this_surface = 1;
                    surfaceFlags = v65->surfaceFlags;
                    if (!(surfaceFlags & (SITH_SURFACE_HORIZON_SKY | SITH_SURFACE_CEILING_SKY)))
                    {
                        for (int i = 0; i < meshinfo_out.numVertices; i++) {
                            //printf("%f\n", (float)v20->vertices[i].y);
                            if (sithRender_aVerticesTmp[i].y < 2.2) {
                                skip_this_surface = 0;
                                break;
                            }
                        }
                        if (skip_this_surface) {
                            goto LABEL_92;
                        }
                    }
#endif

                    v28 = meshinfo_out.numVertices;
                    if ( meshinfo_out.numVertices < 3u )
                        goto LABEL_92;

#ifdef VIEW_SPACE_GBUFFER
					memcpy(v20->vertexVS, sithRender_aVerticesTmp, sizeof(rdVector3) * meshinfo_out.numVertices);
#endif
                    rdCamera_pCurCamera->fnProjectLst(v20->vertices, sithRender_aVerticesTmp, meshinfo_out.numVertices);

                    if ( sithRender_lightingIRMode )
                    {
#ifdef RGB_AMBIENT
						v29.x = v29.y = v29.z = sithRender_f_83198C;
						v20->light_level_static = 0.0;
						rdVector_Copy3(&v20->ambientLight, &v29);
#else
                        v29 = sithRender_f_83198C;
                        v20->light_level_static = 0.0;
                        v20->ambientLight = v29;
#endif
                    }
                    else
                    {
#ifdef RGB_AMBIENT
						v20->ambientLight.x = v20->ambientLight.y = v20->ambientLight.z = stdMath_Clamp(level_idk->extraLight + sithRender_008d4098, 0.0, 1.0);
#else
                        v20->ambientLight = stdMath_Clamp(level_idk->extraLight + sithRender_008d4098, 0.0, 1.0);
#endif
                    }

#ifdef RGB_AMBIENT
					if (v20->ambientLight.x >= 1.0 && v20->ambientLight.y >= 1.0 && v20->ambientLight.z >= 1.0)
#else
                    if ( v20->ambientLight >= 1.0 )
#endif
                    {
                        if ( v68 )
                        {
                            v20->lightingMode = RD_LIGHTMODE_FULLYLIT;
                        }
                        else
                        {
                            v20->lightingMode = RD_LIGHTMODE_DIFFUSE;
                            v20->light_level_static = 1.0;
                        }
                    }
                    else if ( v20->lightingMode == RD_LIGHTMODE_DIFFUSE)
                    {
                        if ( v20->light_level_static >= 1.0 && v68 )
                        {
                            v20->lightingMode = RD_LIGHTMODE_FULLYLIT;
                        }
                        else if ( v20->light_level_static <= 0.0 )
                        {
                            v20->lightingMode = RD_LIGHTMODE_NOTLIT;
                        }
                    }
                    else if ( (rdGetVertexColorMode() == 0) && v20->lightingMode == RD_LIGHTMODE_GOURAUD )
                    {
                        v31 = v20->vertexIntensities;
                        v32 = 1;
                        v66 = *v31;
                        if ( v28 > 1 )
                        {
                            v33 = v31 + 1;
                            do
                            {
                                v34 = stdMath_Fabs(*v33 - v66);
                                if ( v34 > (1.0/64.0) )
                                    break;
                                ++v32;
                                ++v33;
                            }
                            while ( v32 < v28 );
                        }
                        if ( v32 == v28 )
                        {
                            if ( v66 != 1.0 )
                            {
                                if ( v66 == 0.0 )
                                {
                                    v20->lightingMode = RD_LIGHTMODE_NOTLIT;
                                    v20->light_level_static = 0.0;
                                }
                                else
                                {
                                    v20->lightingMode = RD_LIGHTMODE_DIFFUSE;
                                    v20->light_level_static = v66;
                                }
                            }
                        }
                    }

                    v20->wallCel = v65->surfaceInfo.face.wallCel;
                    v20->extralight = v65->surfaceInfo.face.extraLight;
                    v20->material = v65->surfaceInfo.face.material;
                    v38 = v20->geometryMode;
                    v20->light_flags = 0;
                    v20->type = v65->surfaceInfo.face.type;
                    v39 = 1;
                    if ( v38 >= 4 )
                        v39 = 3;
                    if ( v20->lightingMode >= RD_LIGHTMODE_GOURAUD)
                        v39 |= 4u;
                    rdCache_AddProcFace(0, v28, v39);
LABEL_92:
                    if ( (v74 & 1) != 0 )
                    {
                        v19 = v18;
                        v18--;
                    }
                    else
                    {
                        v19 = v71;
                        ++v71;
                    }
                    if ( ++v74 >= v76 )
                        goto LABEL_150;
                    continue;
                }
           }
LABEL_150:
            ;    
        }

        // Surprisingly, this is a fairly minimal cost to the entire render, 3ms on landing terminal spawn
        rdSetProcFaceUserData(level_idk->id | 0x10000);

        int safeguard = 0;
        for ( i = level_idk->thingsList; i; i = i->nextThing )
        {
            // Added: safeguards
            if (++safeguard >= SITH_MAX_THINGS) {
                break;
            }

            if (!(i->thingflags & SITH_TF_LEVELGEO)) {
                continue;
            }

            if (i->thingflags & (SITH_TF_DISABLED| SITH_TF_INVISIBLE |SITH_TF_WILLBEREMOVED)) {
                continue;
            }

#ifdef FP_LEGS
			if (!sithRender_ShouldRenderCameraThing(i))
				continue;
#else
			if (!((sithCamera_currentCamera->cameraPerspective & 0xFC) != 0 || i != sithCamera_currentCamera->primaryFocus)) {
                continue;
            }
#endif

            if (i->rdthing.type != RD_THINGTYPE_MODEL) {
                continue;
            }

            rdMatrix_TransformPoint34(&i->screenPos, &i->position, &rdCamera_pCurCamera->view_matrix);
            v63 = rdClip_SphereInFrustrum(level_idk->clipFrustum, &i->screenPos, i->rdthing.model3->radius);
            i->rdthing.clippingIdk = v63;
            if ( v63 == 2 ) {
                continue;
            }

#ifdef RGB_AMBIENT
			if (a2.x >= 1.0 && a2.y >= 1.0 && a2.z >= 1.0)
#else
            if ( a2 >= 1.0 )
#endif
				i->rdthing.desiredLightMode = RD_LIGHTMODE_FULLYLIT;

#ifdef QOL_IMPROVEMENTS
			// Added: properly set the geoset to 0
			// todo: we may want the geoset select to work here too, perhaps it needs to be in sithRender_RenderThing
			if (((i->rdthing).type == RD_THINGTYPE_MODEL))
				i->rdthing.model3->geosetSelect = 0;
#endif

            // MOTS added
#ifdef JKM_LIGHTING
            if ((i->archlightIdx != -1) && ((i->rdthing).type == RD_THINGTYPE_MODEL)) {
                rdModel3* iVar22 = i->rdthing.model3;
                for (int k = 0; k < 4; k++) {
                    for (int j = 0; j < iVar22->geosets[k].numMeshes; j++) 
                    {
                        if (rdGetVertexColorMode() == 0) {
                            iVar22->geosets[k].meshes[j].vertices_unk = iVar22->geosets[k].meshes[j].vertices_i;
                            iVar22->geosets[k].meshes[j].vertices_i = sithWorld_pCurrentWorld->aArchlights[i->archlightIdx].aMeshes[j].aMono;
                        }
                        else {
                            iVar22->geosets[k].meshes[j].paRedIntensities = sithWorld_pCurrentWorld->aArchlights[i->archlightIdx].aMeshes[j].aRed;
                            iVar22->geosets[k].meshes[j].paGreenIntensities = sithWorld_pCurrentWorld->aArchlights[i->archlightIdx].aMeshes[j].aGreen;
                            iVar22->geosets[k].meshes[j].paBlueIntensities = sithWorld_pCurrentWorld->aArchlights[i->archlightIdx].aMeshes[j].aBlue;
                        }
                    }
                }
            }
            if ((i->archlightIdx == -1) && (rdGetVertexColorMode() == 1)) {
                rdModel3* iVar13 = i->rdthing.model3;
                for (int k = 0; k < 4; k++) {
                    for (int j = 0; j < iVar13->geosets[k].numMeshes; j++) 
                    {
                        iVar13->geosets[k].meshes[j].paRedIntensities = iVar13->geosets[k].meshes[j].vertices_i;
                        iVar13->geosets[k].meshes[j].paGreenIntensities = iVar13->geosets[k].meshes[j].vertices_i;
                        iVar13->geosets[k].meshes[j].paBlueIntensities = iVar13->geosets[k].meshes[j].vertices_i;
                    }
                }
            }
#endif // JKM_LIGHTING

            if ( sithRender_RenderThing(i) )
                ++sithRender_geoThingsDrawn;

            // MOTS added
#ifdef JKM_LIGHTING
            if (((i->archlightIdx != -1) && (i->rdthing.type == RD_THINGTYPE_MODEL)) && (rdGetVertexColorMode() == 0)) {
                rdModel3* iVar14 = i->rdthing.model3;
                for (int k = 0; k < 4; k++) {
                    for (int j = 0; j < iVar14->geosets[k].numMeshes; j++) 
                    {
                        iVar14->geosets[k].meshes[j].vertices_i = iVar14->geosets[k].meshes[j].vertices_unk;
                    }
                }
            }
#endif
        }

        ++sithRender_sectorsDrawn;
    }
    

#ifndef TARGET_TWL
    // TWL: 5-27ms
    rdCache_Flush();
#endif
    rdCamera_pCurCamera->pClipFrustum = v77;
}
#endif

void sithRender_UpdateAllLights()
{
    sithAdjoin *i; // esi

    for (int j = 0; j < sithRender_numSectors; j++)
    {
        for ( i = sithRender_aSectors[j]->adjoins; i; i = i->next )
        {
            if ( i->sector->renderTick != sithRender_lastRenderTick && (i->flags & 1) != 0 )
            {
                i->sector->clipFrustum = sithRender_aSectors[j]->clipFrustum;
                sithRender_UpdateLights(i->sector, 0.0, i->dist, 0);
            }
        }
    }
}

// Added: recursion depth
void sithRender_UpdateLights(sithSector *sector, flex_t prev, flex_t dist, int depth)
{
    sithThing *i;
    sithAdjoin *j;
    rdVector3 vertex_out;

    // Added: safeguards
    if (depth > SITH_MAX_VISIBLE_SECTORS_2) {
        return;
    }

    if ( sector->renderTick == sithRender_lastRenderTick )
        return;

    sector->renderTick = sithRender_lastRenderTick;
    if ( prev < 2.0 && sithRender_numLights < SITHREND_NUM_LIGHTS)
    {
#ifdef RENDER_DROID2
		for (int i = 0; i < sector->numSurfaces; ++i)
		{
			if(sector->surfaces[i].surfaceInfo.face.material && sector->surfaces[i].surfaceFlags & SITH_SURFACE_EMISSIVE)
				sithRender_AddSurfaceLight(&sector->surfaces[i]);
		}
#endif

        int safeguard = 0;
        for ( i = sector->thingsList; i; i = i->nextThing )
        {
            // Added: safeguards
            if (++safeguard >= SITH_MAX_THINGS) {
                break;
            }

            if ( sithRender_numLights >= SITHREND_NUM_LIGHTS)
                break;

            if ((i->thingflags & SITH_TF_LIGHT) 
                && !(i->thingflags & (SITH_TF_DISABLED|SITH_TF_WILLBEREMOVED)))
            {
                if ( i->light > 0.0 )
                {
                    sithRender_aLights[sithRender_numLights].intensity = i->light;
#ifdef RGB_THING_LIGHTS
					sithRender_aLights[sithRender_numLights].color = i->lightColor;
#endif
#ifdef RENDER_DROID2
					if (i->lightRadius > 0.0)
					{
						rdCamera_AddLightExplicitRadius(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], i->lightRadius, &i->position);
					}
					else
#endif
					{
						rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &i->position);
					}
#ifdef RENDER_DROID2
					if (i->lightAngle > 0.0)
					{
						sithRender_aLights[sithRender_numLights].type = RD_LIGHT_SPOTLIGHT;
						sithRender_aLights[sithRender_numLights].direction = i->lookOrientation.lvec;
						sithRender_aLights[sithRender_numLights].width = i->lightSize;
						rdLight_SetAngles(&sithRender_aLights[sithRender_numLights], i->lightAngle * 0.1, i->lightAngle);
					}
#endif
					++sithRender_numLights;
                }

                if ( (i->type == SITH_THING_ACTOR || i->type == SITH_THING_PLAYER) && sithRender_numLights < SITHREND_NUM_LIGHTS)
                {
                    // Actors all have a small amount of light
                    if ( (i->actorParams.typeflags & SITH_AF_FIELDLIGHT) && i->actorParams.lightIntensity > 0.0 )
                    {
                        rdMatrix_TransformPoint34(&vertex_out, &i->actorParams.lightOffset, &i->lookOrientation);
                        rdVector_Add3Acc(&vertex_out, &i->position);
                        
                        sithRender_aLights[sithRender_numLights].intensity = i->actorParams.lightIntensity;
						rdVector_Set3(&sithRender_aLights[sithRender_numLights].color, 1.0f, 1.0f, 1.0f);
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &vertex_out);
                        ++sithRender_numLights;
                    }
                    
                    // Saber light
                    if ( i->actorParams.timeLeftLengthChange > 0.0 )
                    {
                        sithRender_aLights[sithRender_numLights].intensity = i->actorParams.timeLeftLengthChange;
#ifdef RGB_THING_LIGHTS
						sithRender_GetSaberLightColor(&sithRender_aLights[sithRender_numLights].color, i);
#endif
                        rdCamera_AddLight(rdCamera_pCurCamera, &sithRender_aLights[sithRender_numLights], &i->actorParams.saberBladePos);
                        ++sithRender_numLights;
                    }
                }
            }
        }
    }

    if ( prev < 0.8 )
    {
        if ( sithRender_numSectors2 < SITH_MAX_VISIBLE_SECTORS_2 )
        {
            sithRender_aSectors2[sithRender_numSectors2++] = sector;
        }

	#ifdef RENDER_DROID2
		for (int bucket = 0; bucket < sithWorld_pCurrentWorld->numLightBuckets; ++bucket)
		{
			if(sector->lightBuckets)
				sithWorld_pCurrentWorld->lightBuckets[bucket] |= sector->lightBuckets[bucket];
		}
	#endif
    }

#ifndef TARGET_TWL
    // What is the point of this anyhow besides wasting time?
    for ( j = sector->adjoins; j; j = j->next )
    {
        if ( (j->flags & 1) != 0 && j->sector->renderTick != sithRender_lastRenderTick )
        {
            flex_t nextDist = j->mirror->dist + j->dist + dist + prev;
            if ( nextDist < 0.8 || nextDist < 2.0 ) // Bug?
            {
                j->sector->clipFrustum = sector->clipFrustum;
                sithRender_UpdateLights(j->sector, nextDist, 0.0, ++depth);

                // Added: safeguards
                if (depth >= SITH_MAX_VISIBLE_SECTORS_2) break;
            }
        }
    }
#endif
}

#ifdef USE_LIGHT_JOBS

void sithRender_DynamicLightJob(uint32_t jobIndex, uint32_t groupIndex)
{
	int sectorIdx = jobIndex / rdCamera_pCurCamera->numLights;
	int lightIdx = jobIndex % rdCamera_pCurCamera->numLights;

	if (sectorIdx >= sithRender_numSectors || lightIdx >= rdCamera_pCurCamera->numLights)
		return;

	sithSector* sector = sithRender_aSectors[sectorIdx];
	rdLight* light = rdCamera_pCurCamera->lights[lightIdx];

	flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[light->id], &sector->center);
	if (light->falloffMin + sector->radius <= distCalc)
		return;

	for (int j = 0; j < sector->numVertices; j++)
	{
		int idx = sector->verticeIdxs[j];
		if (sithWorld_pCurrentWorld->verticesDynamicLight[idx] < 1.0)
		//if (sithWorld_pCurrentWorld->alloc_unk9c[idx] != sithRender_lastRenderTick)
		{
			flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[light->id], &sithWorld_pCurrentWorld->vertices[idx]);
			if (distCalc < light->falloffMax)
			{
				float intensity = light->intensity - distCalc * rdCamera_pCurCamera->attenuationMax;
				sithWorld_pCurrentWorld->verticesDynamicLight[idx] += intensity;
#ifdef RGB_THING_LIGHTS
				sithWorld_pCurrentWorld->verticesDynamicLightR[idx] += intensity * light->color.x;
				sithWorld_pCurrentWorld->verticesDynamicLightG[idx] += intensity * light->color.y;
				sithWorld_pCurrentWorld->verticesDynamicLightB[idx] += intensity * light->color.z;
#endif
			}
		}
	}
}
#endif

void sithRender_RenderDynamicLights()
{
#ifdef RENDER_DROID2
	// scan through the light buckets to add static lights
	for (int bucket = 0; bucket < sithWorld_pCurrentWorld->numLightBuckets; ++bucket)
	{
		uint64_t lightOffset = bucket * 64;
		uint64_t bucketBits = sithWorld_pCurrentWorld->lightBuckets[bucket];
		while (bucketBits != 0)
		{
			int bitIndex = stdMath_FindLSB64(bucketBits);
			bucketBits ^= 1ull << bitIndex;

			int lightIndex = bitIndex + lightOffset;
			++sithRender_numStaticLights;
			rdAddLight(&sithWorld_pCurrentWorld->lights[lightIndex].rdlight, &sithWorld_pCurrentWorld->lights[lightIndex].pos);

			//rdCamera_AddLight(rdCamera_pCurCamera, &sithWorld_pCurrentWorld->lights[lightIndex].rdlight, &sithWorld_pCurrentWorld->lights[lightIndex].pos);
		}
	}

	// this is now done on the GPU
	for (int i = 0; i < rdCamera_pCurCamera->numLights; i++)
		rdAddLight(rdCamera_pCurCamera->lights[i], &rdCamera_pCurCamera->lightPositions[i]);

#else

#ifdef USE_LIGHT_JOBS
	if (!sithRender_numSectors)
		return;

	for (int k = 0; k < sithRender_numSectors; k++)
	{
		sithSector* sectorIter = sithRender_aSectors[k];
		for (int j = 0; j < sectorIter->numVertices; j++)
		{
			int idx = sectorIter->verticeIdxs[j];
			if (sithWorld_pCurrentWorld->alloc_unk9c[idx] != sithRender_lastRenderTick)
			{
				sithWorld_pCurrentWorld->verticesDynamicLight[idx] = 0.0;
#ifdef RGB_THING_LIGHTS
				sithWorld_pCurrentWorld->verticesDynamicLightR[idx] = 0.0;
				sithWorld_pCurrentWorld->verticesDynamicLightG[idx] = 0.0;
				sithWorld_pCurrentWorld->verticesDynamicLightB[idx] = 0.0;
#endif
				sithWorld_pCurrentWorld->alloc_unk9c[idx] = sithRender_lastRenderTick;
			}
		}
	}

	stdJob_Dispatch(sithRender_numSectors * rdCamera_pCurCamera->numLights, 8, sithRender_DynamicLightJob);
	//stdJob_Wait();
#else
    rdLight *tmpLights[RDCAMERA_MAX_LIGHTS];

    if (!sithRender_numSectors)
        return;

    for (int k = 0; k < sithRender_numSectors; k++)
    {
		sithSector* sectorIter = sithRender_aSectors[k];
        
		rdLight** curCamera_lights = rdCamera_pCurCamera->lights;
        
        //sithRender_RenderDebugLight(10.0, &sectorIter->center);
        
		unsigned int numSectorLights = 0;
        for (int i = 0; i < rdCamera_pCurCamera->numLights; i++)
        {
            //sithRender_RenderDebugLight(10.0, &rdCamera_pCurCamera->lightPositions[i]);
        
            flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[i], &sectorIter->center);
            if ( curCamera_lights[i]->falloffMin + sectorIter->radius > distCalc)
            {
                tmpLights[numSectorLights++] = curCamera_lights[i];
            }
        }

        for (int j = 0; j < sectorIter->numVertices; j++)
        {
            int idx = sectorIter->verticeIdxs[j];
            if ( sithWorld_pCurrentWorld->alloc_unk9c[idx] != sithRender_lastRenderTick )
            {
                sithWorld_pCurrentWorld->verticesDynamicLight[idx] = 0.0;
			#ifdef RGB_THING_LIGHTS
				sithWorld_pCurrentWorld->verticesDynamicLightR[idx] = 0.0;
				sithWorld_pCurrentWorld->verticesDynamicLightG[idx] = 0.0;
				sithWorld_pCurrentWorld->verticesDynamicLightB[idx] = 0.0;
			#endif

                for (int i = 0; i < numSectorLights; i++)
                {
                    int id = tmpLights[i]->id;
                    flex_t distCalc = rdVector_Dist3(&rdCamera_pCurCamera->lightPositions[id], &sithWorld_pCurrentWorld->vertices[idx]);

                    // Light is within distance of the vertex
                    if ( distCalc < tmpLights[i]->falloffMax )
					{
						float intensity = tmpLights[i]->intensity - distCalc * rdCamera_pCurCamera->attenuationMax;
                        sithWorld_pCurrentWorld->verticesDynamicLight[idx] += intensity;
					#ifdef RGB_THING_LIGHTS
						sithWorld_pCurrentWorld->verticesDynamicLightR[idx] += intensity * tmpLights[i]->color.x;
						sithWorld_pCurrentWorld->verticesDynamicLightG[idx] += intensity * tmpLights[i]->color.y;
						sithWorld_pCurrentWorld->verticesDynamicLightB[idx] += intensity * tmpLights[i]->color.z;
					#endif
					}

                    // This vertex is as lit as it can be, stop adding lights to it
                    if ( sithWorld_pCurrentWorld->verticesDynamicLight[idx] >= 1.0
					#ifdef RGB_THING_LIGHTS
						&& sithWorld_pCurrentWorld->verticesDynamicLightR[idx] >= 1.0
						&& sithWorld_pCurrentWorld->verticesDynamicLightG[idx] >= 1.0
						&& sithWorld_pCurrentWorld->verticesDynamicLightB[idx] >= 1.0
					#endif
					)
                        break;
                }
                sithWorld_pCurrentWorld->alloc_unk9c[idx] = sithRender_lastRenderTick;
            }
        }
    }
#endif // JOB_SYSTEM
#endif
}

// MoTS altered
void sithRender_RenderThings()
{
    sithSector *v1; // ebp
    flex_d_t v2; // st7
    sithThing *thingIter; // esi
    flex_t radius; // edx
    int clippingVal; // eax
    sithWorld *curWorld; // edx
    rdModel3 *model3; // ecx
    int texMode; // ecx
    int texMode2; // eax
    rdLightMode_t lightMode; // eax
    flex_t v12; // [esp-Ch] [ebp-28h]
#ifdef RGB_AMBIENT
	rdVector3 a2;
#else
    flex_t a2; // [esp+8h] [ebp-14h]
#endif
	flex_t clipRadius; // [esp+Ch] [ebp-10h]
    uint32_t i; // [esp+14h] [ebp-8h]
    BOOL v16; // [esp+18h] [ebp-4h]

#ifdef QOL_IMPROVEMENTS
	sithRender_alphaDrawThing = NULL; // list of things to render after with zwrite off
#endif

    // MoTS added
    sithThing* lastDrawn = NULL;
    if (sithRender_008d1668) {
        rdSetCullFlags(0);
    }

    rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
    rdSetOcclusionMethod(0);
    rdSetVertexColorMode(0);

#ifdef RENDER_DROID2
	rdSortOrder(0);//1);
	rdSetDecalMode(RD_DECALS_DISABLED);
#endif

    for ( i = 0; i < sithRender_numSectors2; i++ )
    {
        v1 = sithRender_aSectors2[i];
        if ( sithRender_lightingIRMode )
        {
#ifdef RGB_AMBIENT
			a2.x = a2.y = a2.z = sithRender_f_831990;
#else
            a2 = sithRender_f_831990;
#endif
        }
        else
        {
#ifdef RGB_AMBIENT
			v2 = v1->extraLight + sithRender_008d4098;
			a2.x = stdMath_Clamp(v1->ambientRGB.x + v2, 0.0, 1.0);
			a2.y = stdMath_Clamp(v1->ambientRGB.y + v2, 0.0, 1.0);
			a2.z = stdMath_Clamp(v1->ambientRGB.z + v2, 0.0, 1.0);
#else
			v2 = v1->ambientLight + v1->extraLight + sithRender_008d4098;
            a2 = stdMath_Clamp(v2, 0.0, 1.0);
#endif
        }
        rdColormap_SetCurrent(v1->colormap);
        thingIter = v1->thingsList;
        v16 = v1->colormap == sithWorld_pCurrentWorld->colormaps;

		//rdScissorMode(RD_SCISSOR_ENABLED);
		//rdScissorf(v1->clipFrustum->x, v1->clipFrustum->y, v1->clipFrustum->width, v1->clipFrustum->height);

        int safeguard = 0;
        for (; thingIter; thingIter = thingIter->nextThing)
        {
            // Added: safeguards
            if (++safeguard >= SITH_MAX_THINGS) {
                break;
            }

		#ifdef RENDER_DROID2
			// decals are rendered before everything else in sithRender_RenderOccludersAndDecals
			if (thingIter->rdthing.type == RD_THINGTYPE_DECAL)
				continue;
		#endif

            if ( (thingIter->thingflags & (SITH_TF_DISABLED| SITH_TF_INVISIBLE |SITH_TF_WILLBEREMOVED)) == 0
              && (thingIter->thingflags & SITH_TF_LEVELGEO) == 0
			  //&& ((sithCamera_currentCamera->cameraPerspective & 0xFC) != 0 || thingIter != sithCamera_currentCamera->primaryFocus)
			  && sithRender_ShouldRenderCameraThing(thingIter)
			)
            {
			    rdMatrix_TransformPoint34(&thingIter->screenPos, &thingIter->position, &rdCamera_pCurCamera->view_matrix);
                
                //printf("%f %f %f ; %f %f %f\n", thingIter->screenPos.x, thingIter->screenPos.y, thingIter->screenPos.z, thingIter->position.x, thingIter->position.y, thingIter->position.z);
                
                if ( rdroid_curAcceleration > 0 || thingIter->rdthing.type != RD_THINGTYPE_SPRITE3 || sithRender_82F4B4 < 8 )
                {
                    clipRadius = 0.0f;
                    switch ( thingIter->rdthing.type )
                    {
                        case RD_THINGTYPE_MODEL:
                            radius = thingIter->rdthing.model3->radius;
                            clipRadius = radius;
                            clippingVal = rdClip_SphereInFrustrum(v1->clipFrustum, &thingIter->screenPos, clipRadius);
                            break;

                        case RD_THINGTYPE_SPRITE3:
                            clipRadius = thingIter->rdthing.sprite3->radius;
                            ++sithRender_82F4B4;
                            clippingVal = rdClip_SphereInFrustrum(v1->clipFrustum, &thingIter->screenPos, clipRadius);
                            break;

                        case RD_THINGTYPE_PARTICLECLOUD:
                            clipRadius = thingIter->rdthing.particlecloud->cloudRadius;
                            clippingVal = rdClip_SphereInFrustrum(v1->clipFrustum, &thingIter->screenPos, clipRadius);
                            break;

                        case RD_THINGTYPE_POLYLINE:
                            radius = thingIter->rdthing.polyline->length;
                            clipRadius = radius;
                            clippingVal = rdClip_SphereInFrustrum(v1->clipFrustum, &thingIter->screenPos, clipRadius);
                            break;

                        default:
                            clippingVal = rdClip_SphereInFrustrum(v1->clipFrustum, &thingIter->screenPos, clipRadius);
                            break;
                    }
                    thingIter->rdthing.clippingIdk = clippingVal;
                    if ( clippingVal == 2 || sithRender_008d1668) // MoTS added: sithRender_008d1668
                        continue;
                    curWorld = sithWorld_pCurrentWorld;

                    flex_t yval = thingIter->screenPos.y;

                    // MoTS added
                    if (sithCamera_currentCamera->zoomScale != 1.0) {
                        yval = sithCamera_currentCamera->invZoomScale * (thingIter->screenPos).y;
                    }

                    if ( thingIter->rdthing.type == RD_THINGTYPE_MODEL )
                    {
                        model3 = thingIter->rdthing.model3;

                        switch ( model3->numGeosets )
                        {
                            case 1:
                                break;
                            case 2:
                                if ( yval < (flex_d_t)sithWorld_pCurrentWorld->lodDistance.y )
                                {
                                    model3->geosetSelect = 0;
                                }
                                else
                                {
                                    model3->geosetSelect = 1;
                                }
                                break;
                            case 3:
                                if ( yval < (flex_d_t)sithWorld_pCurrentWorld->lodDistance.x )
                                {
                                    model3->geosetSelect = 0;
                                    
                                }
                                else if ( yval >= (flex_d_t)sithWorld_pCurrentWorld->lodDistance.y )
                                {
                                    model3->geosetSelect = 2;
                                }
                                else
                                {
                                    model3->geosetSelect = 1;
                                }

                                break;
                            default:
                                if ( yval < (flex_d_t)sithWorld_pCurrentWorld->lodDistance.x )
                                {
                                    model3->geosetSelect = 0;
                                }
                                else if ( yval < (flex_d_t)sithWorld_pCurrentWorld->lodDistance.y )
                                    model3->geosetSelect = 1;
                                else if ( yval >= (flex_d_t)sithWorld_pCurrentWorld->lodDistance.z )
                                    model3->geosetSelect = 3;
                                else
                                    model3->geosetSelect = 2;
                                break;
                        }

					#ifdef QOL_IMPROVEMENTS
						// todo: why is this setting the geoset on the actual model instance instead of rdthing???
						model3->geosetSelect += jkPlayer_lodBias;
						model3->geosetSelect = stdMath_ClampInt(model3->geosetSelect, 0, model3->numGeosets-1);
					#endif

                    }
                    
                    texMode = thingIter->rdthing.desiredTexMode;
                    if ( yval >= (flex_d_t)curWorld->perspectiveDistance )
                    {
                        thingIter->rdthing.curTexMode = texMode > RD_TEXTUREMODE_AFFINE ? RD_TEXTUREMODE_AFFINE : texMode;
                    }
                    else
                    {
                        texMode2 = RD_TEXTUREMODE_PERSPECTIVE;
                        if ( texMode <= RD_TEXTUREMODE_PERSPECTIVE)
                            texMode2 = thingIter->rdthing.desiredTexMode;
                        thingIter->rdthing.curTexMode = texMode2;
                    }
                    if ( yval >= (flex_d_t)curWorld->perspectiveDistance )
                    {
                        thingIter->rdthing.curTexMode = texMode > RD_TEXTUREMODE_AFFINE ? RD_TEXTUREMODE_AFFINE : texMode;
                    }
                    else
                    {
                        if ( texMode > RD_TEXTUREMODE_PERSPECTIVE)
                            texMode = RD_TEXTUREMODE_PERSPECTIVE;
                        thingIter->rdthing.curTexMode = texMode;
                    }
#ifndef RENDER_DROID2
					if ( (thingIter->thingflags & SITH_TF_LIGHT) != 0
                      && thingIter->light > 0.0
					#ifdef RGB_AMBIENT
						&& (jkPlayer_enableShadows || a2.x <= stdMath_Clamp(thingIter->light, 0.0, 1.0) && a2.y <= stdMath_Clamp(thingIter->light, 0.0, 1.0) && a2.z <= stdMath_Clamp(thingIter->light, 0.0, 1.0))
					#else
                      && a2 <= stdMath_Clamp(thingIter->light, 0.0, 1.0)
					#endif 
					)
                    {
#ifdef RGB_AMBIENT
						rdVector3 lightColor;
						rdVector_Zero3(&lightColor);
						rdVector_MultAcc3(&lightColor, &thingIter->lightColor, thingIter->light);
						lightColor.x = stdMath_Clamp(lightColor.x, 0.0, 1.0);
						lightColor.y = stdMath_Clamp(lightColor.y, 0.0, 1.0);
						lightColor.z = stdMath_Clamp(lightColor.z, 0.0, 1.0);
						rdCamera_SetAmbientLight(rdCamera_pCurCamera, &lightColor);
						rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &thingIter->sector->ambientSH);

#else
                        rdCamera_SetAmbientLight(rdCamera_pCurCamera, stdMath_Clamp(thingIter->light, 0.0, 1.0));
#endif    
                    }
                    else
#endif
					{
#ifdef RGB_AMBIENT
						rdCamera_SetAmbientLight(rdCamera_pCurCamera, &a2);
						rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &thingIter->sector->ambientSH);
#else
						rdCamera_SetAmbientLight(rdCamera_pCurCamera, a2);
#endif
                    }

#ifdef RENDER_DROID2
					lightMode = thingIter->rdthing.desiredLightMode;
#else
#ifdef RGB_AMBIENT
					if (!jkPlayer_enableShadows && a2.x >= 1.0 && a2.y >= 1.0 && a2.z >= 1.0)
#else
					if ( a2 >= 1.0 )
#endif
					{
                        lightMode = thingIter->rdthing.desiredLightMode;
                        if ( v16 )
                        {
                            lightMode = lightMode > RD_LIGHTMODE_FULLYLIT ? RD_LIGHTMODE_FULLYLIT : lightMode;
                        }
                        else
                        {
                            if ( lightMode > RD_LIGHTMODE_DIFFUSE)
                                lightMode = RD_LIGHTMODE_DIFFUSE;
                        }
                    }
                    else
#endif
					if ( (thingIter->thingflags & SITH_TF_IGNOREGOURAUDDISTANCE) == 0 && yval >= (flex_d_t)sithWorld_pCurrentWorld->gouradDistance )
                    {
                        lightMode = thingIter->rdthing.desiredLightMode;
                        if ( lightMode > RD_LIGHTMODE_DIFFUSE)
                            lightMode = RD_LIGHTMODE_DIFFUSE;
                    }
                    else
                    {
                        lightMode = thingIter->rdthing.desiredLightMode;
					#ifdef RENDER_DROID2
						if (lightMode > RD_LIGHTMODE_SUBSURFACE)
							lightMode = RD_LIGHTMODE_SUBSURFACE;
					#elif defined(SPECULAR_LIGHTING)
						if (lightMode > RD_LIGHTMODE_SPECULAR)
							lightMode = RD_LIGHTMODE_SPECULAR;
					#else
                        if ( lightMode > RD_LIGHTMODE_GOURAUD)
                            lightMode = RD_LIGHTMODE_GOURAUD;
					#endif
                    }
                    thingIter->rdthing.curLightMode = lightMode;
                    if (thingIter->thingflags & SITH_TF_80000000) {
                        lastDrawn = thingIter;
                        continue;
                    }

#ifdef QOL_IMPROVEMENTS
					// Added: special things drawn last with no z write
					// for now, only polyline so it draws in draw order
					// but can be used for additive and alpha to prevent
					// issues with z clipping
					if (thingIter->rdthing.type == RD_THINGTYPE_POLYLINE // polyline renders no zwrite, draw order
						|| thingIter->thingflags & SITH_TF_RENDERWEAPON // render weapon may contain a polyline (saber)
					)
					{
						if(!sithRender_alphaDrawThing)
						{
							thingIter->nextDrawThing = NULL;
							sithRender_alphaDrawThing = thingIter;
						}
						else
						{
							thingIter->nextDrawThing = sithRender_alphaDrawThing;
							sithRender_alphaDrawThing = thingIter;
						}

						// draw as usual for renderweapon
						if(!(thingIter->thingflags & SITH_TF_RENDERWEAPON))
							continue;
					}
#endif

                    if (sithRender_RenderThing(thingIter) ) // MOTS added: flag check
                        ++sithRender_nongeoThingsDrawn;
                }
            }
        }
    }
	
    // DSi doesn't really have Z buffer options, so just batch everything
#ifndef TARGET_TWL
    rdCache_Flush("sithRender_RenderThings");
#endif

#ifdef RENDER_DROID2
	rdScissorMode(RD_SCISSOR_DISABLED);
	rdSetDecalMode(jkPlayer_enableDecals ? RD_DECALS_ENABLED : RD_DECALS_DISABLED);
	rdSortOrder(0);
#endif

    // MoTS added
    if (lastDrawn) 
    {
        if (sithRender_RenderThing(lastDrawn)) {
            ++sithRender_nongeoThingsDrawn;
        }
    }
    // DSi doesn't really have Z buffer options, so just batch everything
#ifndef TARGET_TWL
    rdCache_Flush("sithRender_RenderThings:LastDrawn");
#endif

    if (sithRender_008d1668) {
        rdSetCullFlags(1);
    }
}

int sithRender_RenderThing(sithThing *pThing)
{
    int ret;

    if (!(pThing->thingflags & SITH_TF_SIGHTED) && !(g_debugmodeFlags & DEBUGFLAG_NOCLIP)) // Added: don't send sighted stuff in noclip
    {
        if (pThing->thingflags & SITH_TF_CAPTURED) {
            sithCog_SendMessageFromThing(pThing, 0, SITH_MESSAGE_SIGHTED);
        }

        if (pThing->controlType == SITH_CT_AI && pThing->actor)
        {
            pThing->actor->flags &= ~SITHAI_MODE_SLEEPING;
        }
        pThing->thingflags |= SITH_TF_SIGHTED;
    }

    pThing->lastRenderedTickIdx = jkPlayer_currentTickIdx;
    pThing->lookOrientation.scale = pThing->position;

#ifdef RENDER_DROID2
	if (pThing->sector->flags & SITH_SECTOR_UNDERWATER)
		rdAmbientFlags(sithRender_aoFlags | RD_AMBIENT_CAUSTICS);

	//if ((pThing->sector->flags & SITH_SECTOR_UNDERWATER) && !(sithCamera_currentCamera->sector->flags & SITH_SECTOR_UNDERWATER))
	//{
	//	rdVector4 fog = { pThing->sector->tint.x, pThing->sector->tint.y, pThing->sector->tint.z, 1.0f };
	//
	//	rdSetFogMode(RD_FOG_ENABLED);
	//
	//	rdVector3 halfFog;
	//	halfFog.x = fog.x * 0.5f;
	//	halfFog.y = fog.y * 0.5f;
	//	halfFog.z = fog.z * 0.5f;
	//
	//	fog.x = fog.x - (halfFog.z + halfFog.y);
	//	fog.y = fog.y - (halfFog.x + halfFog.y);
	//	fog.z = fog.z - (halfFog.x + halfFog.z);
	//
	//	rdFogColorf(fog.x, fog.y, fog.z, fog.w);
	//	rdFogRange(0.0f, 5.0f);
	//	rdFogAnisotropy(0.35f);
	//}
	//else
	//{
	//	sithRender_SetCameraFog();
	//}
#endif
#ifdef TARGET_TWL
    int skip_this_thing = 0;
    if (pThing->screenPos.y - (pThing->rdthing.type == RD_THINGTYPE_MODEL ? pThing->rdthing.model3->radius : (flex_t)0.0) > 2.0) {
        skip_this_thing = 1;
    }
#endif

    ret = rdThing_Draw(&pThing->rdthing, &pThing->lookOrientation);
    rdVector_Zero3(&pThing->lookOrientation.scale);
#ifdef QOL_IMPROVEMENTS
	if (sithRender_weaponRenderOpaqueHandle && (pThing->thingflags & SITH_TF_RENDERWEAPON))
	{
#ifdef FP_LEGS
		if (!pThing->rdthing.hideWeaponMesh)
#endif
			sithRender_weaponRenderOpaqueHandle(pThing);
	}
#else
    if (sithRender_weaponRenderHandle && (pThing->thingflags & SITH_TF_RENDERWEAPON)) {
#ifdef FP_LEGS
		if(!pThing->rdthing.hideWeaponMesh)
#endif
			sithRender_weaponRenderHandle(pThing);
    }
#endif // QOL_IMPROVEMENTS
#ifdef TARGET_TWL
    }
#endif

    if (pThing->type == SITH_THING_EXPLOSION && (pThing->explosionParams.typeflags & SITHEXPLOSION_FLAG_FLASH_BLINDS_THINGS))
    {
        flex_t cameraDist = stdMath_Dist3D1(pThing->screenPos.x, pThing->screenPos.y, pThing->screenPos.z);
        uint32_t flashG = pThing->explosionParams.flashG;
        uint32_t flashR = pThing->explosionParams.flashR;
        uint32_t flashB = pThing->explosionParams.flashB;
        flex_t flashMagnitude = ((flex_d_t)(flashB + flashR + flashG) * 0.013020833 - rdCamera_pCurCamera->attenuationMin * cameraDist) * 0.1;
        if ( flashMagnitude > 0.0 ) {
            sithPlayer_AddDyamicAdd((__int64)((flex_d_t)flashR * flashMagnitude - -0.5), (__int64)((flex_d_t)flashG * flashMagnitude - -0.5), (__int64)((flex_d_t)flashB * flashMagnitude - -0.5));
        }
        pThing->explosionParams.typeflags &= ~SITHEXPLOSION_FLAG_FLASH_BLINDS_THINGS;
    }

#ifdef PUPPET_PHYSICS
	if (jkPlayer_puppetShowConstraints)
		sithConstraint_DebugDrawConstraints(pThing);

	if (jkPlayer_puppetShowJoints)
		sithRagdoll_DebugDrawPhysicsJoints(pThing);

	if (jkPlayer_puppetShowBodies)
		sithRagdoll_DebugDrawPhysicsBodies(pThing);

	if (jkPlayer_puppetShowJointNames)
		sithPuppet_DebugDrawJointNames(pThing);
#endif

#ifdef QOL_IMPROVEMENTS
	if (jkPlayer_showThingInfo)
		sithRender_DebugDrawThingName(pThing);
#endif

#ifdef RENDER_DROID2
	rdAmbientFlags(sithRender_aoFlags);
#endif

	// position debug
#if 0
		rdSprite debugSprite;
		rdSprite_NewEntry(&debugSprite, "dbgragoll", 0, "sabergreen0.mat", 0.05f, 0.05f, RD_GEOMODE_TEXTURED, RD_LIGHTMODE_FULLYLIT, RD_TEXTUREMODE_AFFINE, 1.0f, &rdroid_zeroVector3);

		rdThing debug;
		rdThing_NewEntry(&debug, pThing);
		rdThing_SetSprite3(&debug, &debugSprite);
		rdMatrix34 mat;
		rdMatrix_BuildTranslate34(&mat, &pThing->position);

		rdSprite_Draw(&debug, &mat);

		rdSprite_FreeEntry(&debugSprite);
		rdThing_FreeEntry(&debug);
#endif

    return ret;
}

#ifdef RENDER_DROID2

void sithRender_RenderAlphaSurfaces()
{
	rdCache_Flush("sithRender_RenderAlphaSurfaces:Start");
	//rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
	rdSetOcclusionMethod(0);
	rdSetSortingMethod(2);
	//rdSetBlendEnabled(RD_TRUE);

	extern void std3D_BlitFrame();
	std3D_BlitFrame();

	for (int i = 0; i < sithRender_numSurfaces; i++)
	{
		sithSurface* surface = sithRender_aSurfaces[i];
		sithSector* sector = surface->parent_sector;

		//rdScissorMode(RD_SCISSOR_ENABLED);
		//rdScissorf(sector->clipFrustum->x, sector->clipFrustum->y, sector->clipFrustum->width, sector->clipFrustum->height);

		//if ((sector->flags & SITH_SECTOR_UNDERWATER) && !(sithCamera_currentCamera->sector->flags & SITH_SECTOR_UNDERWATER))
		//{
		//	rdVector4 fog = { sector->tint.x, sector->tint.y, sector->tint.z, 1.0f };
		//
		//	rdSetFogMode(RD_FOG_ENABLED);
		//
		//	rdVector3 halfFog;
		//	halfFog.x = fog.x * 0.5f;
		//	halfFog.y = fog.y * 0.5f;
		//	halfFog.z = fog.z * 0.5f;
		//
		//	fog.x = fog.x - (halfFog.z + halfFog.y);
		//	fog.y = fog.y - (halfFog.x + halfFog.y);
		//	fog.z = fog.z - (halfFog.x + halfFog.z);
		//
		//	rdFogColorf(fog.x, fog.y, fog.z, fog.w);
		//	rdFogRange(0.0f, 5.0f);
		//	rdFogAnisotropy(0.35f);
		//}
		//else
		//{
		//	sithRender_SetCameraFog();
		//}

		rdColormap_SetCurrent(sector->colormap);
		rdSortDistance(rdVector_Dist3(&sithCamera_currentCamera->vec3_1, &sithWorld_pCurrentWorld->vertices[*surface->surfaceInfo.face.vertexPosIdx]));
		sithRender_DrawSurface(surface);
	}

	rdCache_Flush("sithRender_RenderAlphaSurfaces:End");
	rdSetBlendEnabled(RD_FALSE);
	rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
	rdScissorMode(RD_SCISSOR_DISABLED);
}
#else
void sithRender_RenderAlphaSurfaces()
{
    sithSurface *v0; // edi
    sithSector *v1; // esi
 #ifdef RGB_AMBIENT
	rdVector3 v2;
 #else
	flex_d_t v2; // st7
 #endif
	unsigned int v4; // ebp
    int v7; // eax
    rdProcEntry *v9; // esi
    flex_t *v20; // eax
    unsigned int v21; // ecx
    flex_t *v22; // edx
    char v23; // bl
    flex_t v31; // [esp+4h] [ebp-10h]
    sithSector *surfaceSector; // [esp+Ch] [ebp-8h]

#ifdef SDL2_RENDER
    rdCache_Flush();
    rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
#else
    rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
#endif
    rdSetOcclusionMethod(0);
    rdSetSortingMethod(2);

    for (int i = 0; i < sithRender_numSurfaces; i++)
    {
        v0 = sithRender_aSurfaces[i];
        v1 = v0->parent_sector;
        surfaceSector = v1;
        if ( sithRender_lightingIRMode )
        {
#ifdef RGB_AMBIENT
			rdVector3 amb;
			amb.x = amb.y = amb.z = sithRender_f_83198C;
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &amb);
			rdAmbient_Zero(&rdCamera_pCurCamera->ambientSH);
#else
            rdCamera_SetAmbientLight(rdCamera_pCurCamera, sithRender_f_83198C);
#endif
        }
        else
        {
#ifdef RGB_AMBIENT
			v2.x = v2.y = v2.z = v1->extraLight + sithRender_008d4098;
			rdVector_Add3Acc(&v2, &v1->ambientRGB);
			v2.x = stdMath_Clamp(v2.x, 0.0, 1.0);
			v2.y = stdMath_Clamp(v2.y, 0.0, 1.0);
			v2.z = stdMath_Clamp(v2.z, 0.0, 1.0);
			rdCamera_SetAmbientLight(rdCamera_pCurCamera, &v2);
			rdCamera_SetDirectionalAmbientLight(rdCamera_pCurCamera, &v1->ambientSH);
#else
            v2 = v1->extraLight + v1->ambientLight + sithRender_008d4098;
            rdCamera_SetAmbientLight(rdCamera_pCurCamera, stdMath_Clamp(v2, 0.0, 1.0));
#endif
        }
        rdColormap_SetCurrent(v1->colormap);

        if ( v0->field_4 != sithRender_lastRenderTick )
        {
            for (v4 = 0; v4 < v0->surfaceInfo.face.numVertices; v4++)
            {
                v7 = v0->surfaceInfo.face.vertexPosIdx[v4];
                if ( sithWorld_pCurrentWorld->alloc_unk98[v7] != sithRender_lastRenderTick )
                {
                    rdMatrix_TransformPoint34(&sithWorld_pCurrentWorld->verticesTransformed[v7], &sithWorld_pCurrentWorld->vertices[v7], &rdCamera_pCurCamera->view_matrix);
                    sithWorld_pCurrentWorld->alloc_unk98[v7] = sithRender_lastRenderTick;
                }
            }
            v0->field_4 = sithRender_lastRenderTick;
        }
        
		// very messy copy from sithRender_RenderLevelGeometry() for colored light
#ifdef RGB_THING_LIGHTS
		int v74 = 0;
		int v76 = v0->surfaceInfo.face.numVertices - 2;
		int lightMode2;
		if (v76 > 0)
		{
			int v68 = surfaceSector->colormap == sithWorld_pCurrentWorld->colormaps;

			int v18 = v0->surfaceInfo.face.numVertices - 1;
			int v71 = 1;
			int v19 = 0;
			int v24;
			int v78[3];
			int v79[3];
			float v80[3];
			float tmpGreen[3];
			float tmpBlue[3];
			int v28;
			float* v31;
			int v32;
			float* v33;
			float v34;
			float v66;
			int v38;
			int v39;
#ifdef RGB_AMBIENT
			rdVector3 v29;
#else
			float v29;
#endif
			while (2)
			{
				v9 = rdCache_GetProcEntry();
				if (!v9)
					goto LABEL_92;
				v21 = v0->surfaceInfo.face.geometryMode;
				if (v21 >= sithRender_geoMode)
					v21 = sithRender_geoMode;
				v9->geometryMode = v21;
				lightMode2 = v0->surfaceInfo.face.lightingMode;
				if (sithRender_lightingIRMode)
				{
					if (lightMode2 >= RD_LIGHTMODE_DIFFUSE)
						lightMode2 = RD_LIGHTMODE_DIFFUSE;
				}
				else if (lightMode2 >= sithRender_lightMode)
				{
					lightMode2 = sithRender_lightMode;
				}
				v23 = sithRender_texMode;
				v9->lightingMode = lightMode2;
				v24 = v0->surfaceInfo.face.textureMode;
				if (v24 >= v23)
					v24 = v23;
				v9->textureMode = v24;
				v78[0] = v0->surfaceInfo.face.vertexPosIdx[v19];
				v78[1] = v0->surfaceInfo.face.vertexPosIdx[v71];
				v78[2] = v0->surfaceInfo.face.vertexPosIdx[v18];
				if (v9->geometryMode >= RD_GEOMODE_TEXTURED)
				{
					v79[0] = v0->surfaceInfo.face.vertexUVIdx[v19];
					v79[1] = v0->surfaceInfo.face.vertexUVIdx[v71];
					v79[2] = v0->surfaceInfo.face.vertexUVIdx[v18];
				}
				meshinfo_out.verticesProjected = sithRender_aVerticesTmp;
				sithRender_idxInfo.numVertices = 3;
				meshinfo_out.vertexUVs = v9->vertexUVs;
				sithRender_idxInfo.vertexPosIdx = v78;
				meshinfo_out.paDynamicLight = v9->vertexIntensities;
#ifdef RGB_THING_LIGHTS
				meshinfo_out.paDynamicLightR = v9->paRedIntensities;
				meshinfo_out.paDynamicLightG = v9->paGreenIntensities;
				meshinfo_out.paDynamicLightB = v9->paBlueIntensities;
#endif
				sithRender_idxInfo.vertexUVIdx = v79;

				// MOTS added
				if (rdGetVertexColorMode() == 0)
				{
					v80[0] = v0->surfaceInfo.intensities[v19];
					v80[1] = v0->surfaceInfo.intensities[v71];
					v80[2] = v0->surfaceInfo.intensities[v18];
					sithRender_idxInfo.intensities = v80;
					rdPrimit3_ClipFace(surfaceSector->clipFrustum,
									   v9->geometryMode,
									   v9->lightingMode,
									   v9->textureMode,
									   &sithRender_idxInfo,
									   &meshinfo_out,
									   &v0->surfaceInfo.face.clipIdk);
				}
				else
				{


					if ((v0->surfaceFlags & SITH_SURFACE_1000000) == 0)
					{
						v80[0] = v0->surfaceInfo.intensities[v19];
						v80[1] = v0->surfaceInfo.intensities[v71];
						v80[2] = v0->surfaceInfo.intensities[v18];

						memcpy(tmpBlue, v80, sizeof(float) * 3);
						memcpy(tmpGreen, v80, sizeof(float) * 3);
					}
					else
					{
						v80[0] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 1) + v19];
						v80[1] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 1) + v71];
						v80[2] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 1) + v18];

						tmpGreen[0] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 2) + v19];
						tmpGreen[1] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 2) + v71];
						tmpGreen[2] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 2) + v18];

						tmpBlue[0] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 3) + v19];
						tmpBlue[1] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 3) + v71];
						tmpBlue[2] = v0->surfaceInfo.intensities[(v0->surfaceInfo.face.numVertices * 3) + v18];
					}

					sithRender_idxInfo.paRedIntensities = v80;
					sithRender_idxInfo.paGreenIntensities = tmpGreen;
					sithRender_idxInfo.paBlueIntensities = tmpBlue;

					meshinfo_out.paRedIntensities = v9->paRedIntensities;
					meshinfo_out.paGreenIntensities = v9->paGreenIntensities;
					meshinfo_out.paBlueIntensities = v9->paBlueIntensities;

					rdPrimit3_ClipFaceRGBLevel
					(surfaceSector->clipFrustum,
					 v9->geometryMode,
					 v9->lightingMode,
					 v9->textureMode,
					 &sithRender_idxInfo,
					 &meshinfo_out,
					 &(v0->surfaceInfo).face.clipIdk);
				}

				v28 = meshinfo_out.numVertices;
				if (meshinfo_out.numVertices < 3u)
					goto LABEL_92;

#ifdef VIEW_SPACE_GBUFFER
				memcpy(v9->vertexVS, sithRender_aVerticesTmp, sizeof(rdVector3) * meshinfo_out.numVertices);
#endif
				rdCamera_pCurCamera->fnProjectLst(v9->vertices, sithRender_aVerticesTmp, meshinfo_out.numVertices);

				if (sithRender_lightingIRMode)
				{
#ifdef RGB_AMBIENT
					v29.x = v29.y = v29.z = sithRender_f_83198C;
					v9->light_level_static = 0.0;
					rdVector_Copy3(&v9->ambientLight, &v29);
#else
					v29 = sithRender_f_83198C;
					v9->light_level_static = 0.0;
					v9->ambientLight = v29;
#endif
				}
				else
				{
#ifdef RGB_AMBIENT
					v9->ambientLight.x = v9->ambientLight.y = v9->ambientLight.z = stdMath_Clamp(surfaceSector->extraLight + sithRender_008d4098, 0.0, 1.0);
#else
					v9->ambientLight = stdMath_Clamp(surfaceSector->extraLight + sithRender_008d4098, 0.0, 1.0);
#endif
				}

#ifdef RGB_AMBIENT
				if (v9->ambientLight.x >= 1.0 && v9->ambientLight.y >= 1.0 && v9->ambientLight.z >= 1.0)
#else
				if (v9->ambientLight >= 1.0)
#endif
				{
					if (v68)
					{
						v9->lightingMode = RD_LIGHTMODE_FULLYLIT;
					}
					else
					{
						v9->lightingMode = RD_LIGHTMODE_DIFFUSE;
						v9->light_level_static = 1.0;
					}
				}
				else if (v9->lightingMode == RD_LIGHTMODE_DIFFUSE)
				{
					if (v9->light_level_static >= 1.0 && v68)
					{
						v9->lightingMode = RD_LIGHTMODE_FULLYLIT;
					}
					else if (v9->light_level_static <= 0.0)
					{
						v9->lightingMode = RD_LIGHTMODE_NOTLIT;
					}
				}
				else if ((rdGetVertexColorMode() == 0) && v9->lightingMode == RD_LIGHTMODE_GOURAUD)
				{
					v31 = v9->vertexIntensities;
					v32 = 1;
					v66 = *v31;
					if (v28 > 1)
					{
						v33 = v31 + 1;
						do
						{
							v34 = fabs(*v33 - v66);
							if (v34 > 0.015625)
								break;
							++v32;
							++v33;
						} while (v32 < v28);
					}
					if (v32 == v28)
					{
						if (v66 != 1.0)
						{
							if (v66 == 0.0)
							{
								v9->lightingMode = RD_LIGHTMODE_NOTLIT;
								v9->light_level_static = 0.0;
							}
							else
							{
								v9->lightingMode = RD_LIGHTMODE_DIFFUSE;
								v9->light_level_static = v66;
							}
						}
					}
				}
				rdSetProcFaceUserData(surfaceSector->id);
				v9->wallCel = v0->surfaceInfo.face.wallCel;
				v9->extralight = v0->surfaceInfo.face.extraLight;
				v9->material = v0->surfaceInfo.face.material;
				v38 = v9->geometryMode;
				v9->light_flags = 0;
				v9->type = v0->surfaceInfo.face.type;
				v39 = 1;
				if (v38 >= 4)
					v39 = 3;
				if (v9->lightingMode >= RD_LIGHTMODE_GOURAUD)
					v39 |= 4u;
				rdCache_AddProcFace(0, v28, v39);
			LABEL_92:
				if ((v74 & 1) != 0)
				{
					v19 = v18;
					v18--;
				}
				else
				{
					v19 = v71;
					++v71;
				}
				if (++v74 >= v76)
					goto LABEL_150;
				continue;
			}
		}
	LABEL_150:
		;
	}
#else
        v9 = rdCache_GetProcEntry();
        if ( !v9 )
        {
            continue;
        }
        
        v9->geometryMode = sithRender_geoMode;
        if ( v0->surfaceInfo.face.geometryMode < v9->geometryMode )
        {
            v9->geometryMode = v0->surfaceInfo.face.geometryMode;
        }

        v9->lightingMode = sithRender_lightMode;
        if ( v0->surfaceInfo.face.lightingMode < v9->lightingMode )
        {
            v9->lightingMode = v0->surfaceInfo.face.lightingMode;
        }
        
        v9->textureMode = v0->surfaceInfo.face.textureMode;
        if (sithRender_texMode <= v9->textureMode)
            v9->textureMode = sithRender_texMode;

        sithRender_idxInfo.intensities = v0->surfaceInfo.intensities;
        meshinfo_out.vertexUVs = v9->vertexUVs;
        meshinfo_out.paDynamicLight = v9->vertexIntensities;
        sithRender_idxInfo.numVertices = v0->surfaceInfo.face.numVertices;
        sithRender_idxInfo.vertexPosIdx = v0->surfaceInfo.face.vertexPosIdx;
        sithRender_idxInfo.vertexUVIdx = v0->surfaceInfo.face.vertexUVIdx;
        meshinfo_out.verticesProjected = sithRender_aVerticesTmp;

        // Added: Just in case
        if (!sithRender_idxInfo.vertexUVIdx && v9->geometryMode > RD_GEOMODE_SOLIDCOLOR) {
            v9->geometryMode = RD_GEOMODE_SOLIDCOLOR;
        }

#ifdef RGB_THING_LIGHTS
		// fixme: there's some weird flickering happening on alpha surfaces now...
		meshinfo_out.paDynamicLightR = v9->paRedIntensities;
		meshinfo_out.paDynamicLightG = v9->paGreenIntensities;
		meshinfo_out.paDynamicLightB = v9->paBlueIntensities;
		rdPrimit3_ClipFace(surfaceSector->clipFrustum, v9->geometryMode, v9->lightingMode, v9->textureMode, &sithRender_idxInfo, &meshinfo_out, &v0->surfaceInfo.face.clipIdk);
		
		// fixme: disabled for now, results in black lighting
		/*if (rdGetVertexColorMode() == 0)
		{
			sithRender_idxInfo.intensities = v0->surfaceInfo.intensities;
			rdPrimit3_ClipFace(surfaceSector->clipFrustum, v9->geometryMode, v9->lightingMode, v9->textureMode, &sithRender_idxInfo, &meshinfo_out, &v0->surfaceInfo.face.clipIdk);
		}
		else
		{
			if ((v0->surfaceFlags & SITH_SURFACE_1000000) == 0)
			{
				sithRender_idxInfo.paRedIntensities = (v0->surfaceInfo).intensities;
				sithRender_idxInfo.paGreenIntensities = sithRender_idxInfo.paRedIntensities;
				sithRender_idxInfo.paBlueIntensities = sithRender_idxInfo.paRedIntensities;
			}
			else
			{
				sithRender_idxInfo.paRedIntensities =
					(v0->surfaceInfo).intensities +
					sithRender_idxInfo.numVertices;

				sithRender_idxInfo.paGreenIntensities =
					sithRender_idxInfo.paRedIntensities +
					sithRender_idxInfo.numVertices;

				sithRender_idxInfo.paBlueIntensities =
					sithRender_idxInfo.paGreenIntensities +
					sithRender_idxInfo.numVertices;
			}

			meshinfo_out.paGreenIntensities = v9->paGreenIntensities;
			meshinfo_out.paRedIntensities = v9->paRedIntensities;
			meshinfo_out.paBlueIntensities = v9->paBlueIntensities;
			rdPrimit3_ClipFaceRGB(surfaceSector->clipFrustum, v9->geometryMode, v9->lightingMode, v9->textureMode, &sithRender_idxInfo, &meshinfo_out, &v0->surfaceInfo.face.clipIdk);
		}*/
#else
		rdPrimit3_ClipFace(surfaceSector->clipFrustum, v9->geometryMode, v9->lightingMode, v9->textureMode, &sithRender_idxInfo, &meshinfo_out, &v0->surfaceInfo.face.clipIdk);
#endif
		if ( meshinfo_out.numVertices < 3u )
        {
            continue;
        }
#ifdef VIEW_SPACE_GBUFFER
		memcpy(v9->vertexVS, sithRender_aVerticesTmp, sizeof(rdVector3)* meshinfo_out.numVertices);
#endif
        rdCamera_pCurCamera->fnProjectLst(v9->vertices, sithRender_aVerticesTmp, meshinfo_out.numVertices);
        
#ifdef RGB_AMBIENT
		v9->ambientLight.x = v9->ambientLight.y = v9->ambientLight.z = stdMath_Clamp(surfaceSector->extraLight + sithRender_008d4098, 0.0, 1.0);
#else
        v9->ambientLight = stdMath_Clamp(surfaceSector->extraLight + sithRender_008d4098, 0.0, 1.0);
#endif

#ifdef RGB_AMBIENT
		if (v9->ambientLight.x < 1.0 || v9->ambientLight.y < 1.0 || v9->ambientLight.z < 1.0)
#else
		if ( v9->ambientLight < 1.0 )
#endif
		{
            if ( v9->lightingMode == RD_LIGHTMODE_DIFFUSE)
            {
                if ( v9->light_level_static >= 1.0 && surfaceSector->colormap == sithWorld_pCurrentWorld->colormaps )
                {
                    v9->lightingMode = RD_LIGHTMODE_FULLYLIT;
                }
                else if ( v9->light_level_static <= 0.0 )
                {
                    v9->lightingMode = RD_LIGHTMODE_NOTLIT;
                }
            }
            else if ( v9->lightingMode == RD_LIGHTMODE_GOURAUD)
            {
                v20 = v9->vertexIntensities;
                v21 = 1;
                v31 = *v20;
                if ( meshinfo_out.numVertices > 1 )
                {
                    v22 = v20 + 1;
                    do
                    {
                        if ( *v22 != v31 )
                            break;
                        ++v21;
                        ++v22;
                    }
                    while ( v21 < meshinfo_out.numVertices );
                }
                if ( v21 != meshinfo_out.numVertices )
                {

                }
                else if ( v31 != 1.0 )
                {
                    if ( v31 == 0.0 )
                    {
                        v9->lightingMode = RD_LIGHTMODE_NOTLIT;
                        v9->light_level_static = 0.0;
                    }
                    else
                    {
                        v9->lightingMode = RD_LIGHTMODE_DIFFUSE;
                        v9->light_level_static = v31;
                    }
                }
                else if ( surfaceSector->colormap != sithWorld_pCurrentWorld->colormaps )
                {
                    v9->lightingMode = RD_LIGHTMODE_DIFFUSE;
                    v9->light_level_static = 1.0;
                }
                else
                {
                    v9->lightingMode = RD_LIGHTMODE_FULLYLIT;
                }
            }
        }
        else
        {
            if ( surfaceSector->colormap != sithWorld_pCurrentWorld->colormaps )
            {
                v9->lightingMode = RD_LIGHTMODE_DIFFUSE;
                v9->light_level_static = 1.0;
            }
            else
            {
                v9->lightingMode = RD_LIGHTMODE_FULLYLIT;
            }
        }

        v23 = 1;
        if ( v9->geometryMode >= RD_GEOMODE_TEXTURED)
            v23 = 3;
        if ( v9->lightingMode >= RD_LIGHTMODE_GOURAUD)
            v23 |= 4u;

        v9->type = v0->surfaceInfo.face.type;
        v9->extralight = v0->surfaceInfo.face.extraLight;
        v9->wallCel = v0->surfaceInfo.face.wallCel;
        v9->light_flags = 0;
        v9->material = v0->surfaceInfo.face.material;
        rdSetProcFaceUserData(surfaceSector->id);
        rdCache_AddProcFace(0, meshinfo_out.numVertices, v23);
    }
#endif

#if !defined(QOL_IMPROVEMENTS) || defined(TARGET_TWL) // moved outside of function to handle alpha things
    // DSi doesn't really have Z buffer options, so just batch everything
#ifndef TARGET_TWL
    rdCache_Flush();
#endif
#ifdef SDL2_RENDER
    rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
#endif
#endif
}
#endif

#ifdef QOL_IMPROVEMENTS
int sithRender_SetRenderWeaponOpaqueHandle(sithRender_weapRendFunc_t a1)
{
	sithRender_weaponRenderOpaqueHandle = a1;
	return 1;
}

int sithRender_SetRenderWeaponAlphaHandle(sithRender_weapRendFunc_t a1)
{
	sithRender_weaponRenderAlphaHandle = a1;
	return 1;
}

#else
int sithRender_SetRenderWeaponHandle(sithRender_weapRendFunc_t*a1)
{
    sithRender_weaponRenderHandle = a1;
    return 1;
}
#endif

// MoTS Added
void sithRender_WorldFlash(flex_t arg1,flex_t arg2)
{
  if ((arg1 != 0.0) && ((uint16_t)((uint16_t)(arg2 < 0.0) << 8 | (uint16_t)(arg2 == 0.0) << 0xe) == 0)) {
    sithRender_008d4094 = 1;
    sithRender_008d4098 = arg1;
    sithRender_008d409c = arg2;
  }
}


