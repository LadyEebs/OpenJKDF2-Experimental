#include "rdThing.h"

#include "Engine/rdroid.h"
#include "Engine/rdPuppet.h"
#include "Primitives/rdMatrix.h"

rdThing* rdThing_New(sithThing *parent)
{
    rdThing *thing;

    thing = (rdThing*)rdroid_pHS->alloc(sizeof(rdThing));
    if ( !thing )
        return 0;
    rdThing_NewEntry(thing, parent);
    return thing;
}

int rdThing_NewEntry(rdThing *thing, sithThing *parent)
{
    thing->model3 = 0;
    thing->type = 0;
    thing->puppet = 0;
    thing->field_18 = 0;
    thing->frameTrue = 0;
    thing->geosetSelect = -1;
    thing->wallCel = -1;
    thing->hierarchyNodeMatrices = 0;
    thing->desiredGeoMode = RD_GEOMODE_TEXTURED;
	thing->rootJoint = 0; // Added
	thing->spriteScale = 1.0f; // Added
	thing->spriteRot = 0.0f; // Added
#ifdef RENDER_DROID2
	thing->desiredLightMode = RD_LIGHTMODE_SUBSURFACE;
#elif defined(SPECULAR_LIGHTING)
	thing->desiredLightMode = RD_LIGHTMODE_SPECULAR;
#else
    thing->desiredLightMode = RD_LIGHTMODE_GOURAUD;
#endif
    thing->desiredTexMode = RD_TEXTUREMODE_PERSPECTIVE;
    thing->curGeoMode = RD_GEOMODE_TEXTURED;
#ifdef RENDER_DROID2
	thing->curLightMode = RD_LIGHTMODE_SUBSURFACE;
#elif defined(SPECULAR_LIGHTING)
	thing->curLightMode = RD_LIGHTMODE_SPECULAR;
#else
	thing->curLightMode = RD_LIGHTMODE_GOURAUD;
#endif
	thing->curTexMode = RD_TEXTUREMODE_PERSPECTIVE;
    thing->parentSithThing = parent;
#ifdef VERTEX_COLORS
	thing->color.x = thing->color.y = thing->color.z = 1.0f;
#endif
#ifdef PUPPET_PHYSICS
	thing->paHierarchyNodeMatricesPrev = 0;
	thing->paHierarchyNodeVelocities = 0;
	thing->paHiearchyNodeMatrixOverrides = 0;
#endif
    return 1;
}

void rdThing_Free(rdThing *thing)
{
    if ( thing )
    {
        rdThing_FreeEntry(thing);
        rdroid_pHS->free(thing);
    }
}

void rdThing_FreeEntry(rdThing *thing)
{
    if (thing->type == RD_THINGTYPE_MODEL)
    {
        if ( thing->hierarchyNodeMatrices )
        {
            rdroid_pHS->free(thing->hierarchyNodeMatrices);
            thing->hierarchyNodeMatrices = 0;
        }
        if ( thing->hierarchyNodes2 )
        {
            rdroid_pHS->free((void *)thing->hierarchyNodes2); // Possible OOB write in this
            thing->hierarchyNodes2 = 0;
        }
        if ( thing->amputatedJoints )
        {
            rdroid_pHS->free(thing->amputatedJoints);
            thing->amputatedJoints = 0;
        }
#ifdef PUPPET_PHYSICS
		if (thing->paHierarchyNodeMatricesPrev)
		{
			rdroid_pHS->free(thing->paHierarchyNodeMatricesPrev);
			thing->paHierarchyNodeMatricesPrev = 0;
		}
		if (thing->paHierarchyNodeVelocities)
		{
			rdroid_pHS->free(thing->paHierarchyNodeVelocities);
			thing->paHierarchyNodeVelocities = 0;
		}
		if (thing->paHierarchyNodeAngularVelocities)
		{
			rdroid_pHS->free(thing->paHierarchyNodeAngularVelocities);
			thing->paHierarchyNodeAngularVelocities = 0;
		}
		if (thing->paHiearchyNodeMatrixOverrides)
		{
			rdroid_pHS->free(thing->paHiearchyNodeMatrixOverrides);
			thing->paHiearchyNodeMatrixOverrides = 0;
		}
#endif

#ifdef MOTION_BLUR
		if (thing->paPrevMatrices)
		{
			rdroid_pHS->free(thing->paPrevMatrices);
			thing->paPrevMatrices = 0;
		}
#endif
    }
    if ( thing->puppet )
    {
        rdPuppet_Free(thing->puppet);
        thing->puppet = 0;
    }
}

int rdThing_SetModel3(rdThing *thing, rdModel3 *model)
{
    thing->type = RD_THINGTYPE_MODEL;
    thing->model3 = model;
    thing->geosetSelect = -1;
	thing->rootJoint = 0; // Added
#ifdef FP_LEGS
	thing->hiddenJoint = -1;
	thing->hideWeaponMesh = 0;
#endif

#ifdef STDPLATFORM_HEAP_SUGGESTIONS
    int prevSuggest = pSithHS->suggestHeap(HEAP_FAST);
#endif
    thing->hierarchyNodeMatrices = (rdMatrix34*)rdroid_pHS->alloc(sizeof(rdMatrix34) * model->numHierarchyNodes);
#ifdef STDPLATFORM_HEAP_SUGGESTIONS
    pSithHS->suggestHeap(prevSuggest);
#endif

    // moved
    if (!thing->hierarchyNodeMatrices)
        return 0;

    thing->hierarchyNodes2 = (rdVector3*)rdroid_pHS->alloc(sizeof(rdVector3) * model->numHierarchyNodes);
    // memset used to be here??

    // thing->hierarchyNodeMatrices check used to be here??
    
    if (!thing->hierarchyNodes2)
        return 0;
    
    _memset(thing->hierarchyNodes2, 0, sizeof(rdVector3) * model->numHierarchyNodes);

    thing->amputatedJoints = (int *)rdroid_pHS->alloc(sizeof(int) * model->numHierarchyNodes);
    if (!thing->amputatedJoints)
        return 0;
	_memset(thing->amputatedJoints, 0, sizeof(int) * model->numHierarchyNodes);

#ifdef PUPPET_PHYSICS
	thing->paHierarchyNodeMatricesPrev = (rdMatrix34*)rdroid_pHS->alloc(sizeof(rdMatrix34) * model->numHierarchyNodes);
	if (!thing->paHierarchyNodeMatricesPrev)
		return 0;
	_memset(thing->paHierarchyNodeMatricesPrev, 0, sizeof(rdMatrix34) * model->numHierarchyNodes);

	thing->paHierarchyNodeVelocities = (rdVector3*)rdroid_pHS->alloc(sizeof(rdVector3) * model->numHierarchyNodes);
	if (!thing->paHierarchyNodeVelocities)
		return 0;
	_memset(thing->paHierarchyNodeVelocities, 0, sizeof(rdVector3) * model->numHierarchyNodes);

	thing->paHierarchyNodeAngularVelocities = (rdVector3*)rdroid_pHS->alloc(sizeof(rdVector3) * model->numHierarchyNodes);
	if (!thing->paHierarchyNodeAngularVelocities)
		return 0;
	_memset(thing->paHierarchyNodeAngularVelocities, 0, sizeof(rdVector3) * model->numHierarchyNodes);

	thing->paHiearchyNodeMatrixOverrides = (rdMatrix34**)rdroid_pHS->alloc(sizeof(rdMatrix34*) * model->numHierarchyNodes);
	if (!thing->paHiearchyNodeMatrixOverrides)
		return 0;
	_memset(thing->paHiearchyNodeMatrixOverrides, 0, sizeof(rdMatrix34*) * model->numHierarchyNodes);
#endif

#ifdef MOTION_BLUR
	thing->paPrevMatrices = rdroid_pHS->alloc(sizeof(rdMatrix34) * model->geosets[0].numMeshes);
#endif

    rdHierarchyNode* iter = model->hierarchyNodes;
    for (int i = 0; i < model->numHierarchyNodes; i++)
    {
        rdMatrix_Build34(&iter->posRotMatrix, &iter->rot, &iter->pos);
        iter++;
    }
    return 1;
}

int rdThing_SetCamera(rdThing *thing, rdCamera *camera)
{
    thing->type = RD_THINGTYPE_CAMERA;
    thing->camera = camera;
    return 1;
}

int rdThing_SetLight(rdThing *thing, rdLight *light)
{
    thing->type = RD_THINGTYPE_LIGHT;
    thing->light = light;
    return 1;
}

int rdThing_SetSprite3(rdThing *thing, rdSprite *sprite)
{
    thing->type = RD_THINGTYPE_SPRITE3;
    thing->sprite3 = sprite;
    thing->wallCel = -1;
    return 1;
}

int rdThing_SetPolyline(rdThing *thing, rdPolyLine *polyline)
{
    thing->type = RD_THINGTYPE_POLYLINE;
    thing->polyline = polyline;
    thing->wallCel = -1;
    return 1;
}

int rdThing_SetParticleCloud(rdThing *thing, rdParticle *particle)
{
    thing->type = RD_THINGTYPE_PARTICLECLOUD;
    thing->particlecloud = particle;
    return 1;
}

#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
int rdThing_SetDecal(rdThing* thing, rdDecal* decal)
{
	thing->type = RD_THINGTYPE_DECAL;
	thing->decal = decal;
	thing->decalScale.x = thing->decalScale.y = thing->decalScale.z = 1.0f;
}

int rdDecal_Draw(rdThing* thing, rdMatrix34* m);
#endif

int rdThing_Draw(rdThing *thing, rdMatrix34 *m)
{
    if (!rdroid_curGeometryMode)
        return 0;

    switch ( thing->type )
    {
        case RD_THINGTYPE_0:
        case RD_THINGTYPE_CAMERA:
        case RD_THINGTYPE_LIGHT:
            return 0;
        case RD_THINGTYPE_MODEL:
            return rdModel3_Draw(thing, m);
        case RD_THINGTYPE_SPRITE3:
            return rdSprite_Draw(thing, m);
        case RD_THINGTYPE_PARTICLECLOUD:
            return rdParticle_Draw(thing, m);
        case RD_THINGTYPE_POLYLINE:
            return rdPolyLine_Draw(thing, m);
#if defined(DECAL_RENDERING) || defined(RENDER_DROID2)
		case RD_THINGTYPE_DECAL:
			return rdDecal_Draw(thing, m);
#endif
    }
    
    // aaaaaaaaaaaaaaaaaa original game returns undefined for other types, this is to replicate that
    return (intptr_t)thing;
}

void rdThing_AccumulateMatrices(rdThing *thing, rdHierarchyNode *node, rdMatrix34 *acc)
{
    rdHierarchyNode *childIter;
    rdVector3 negPivot;
    rdMatrix34 matrix;

#ifdef PUPPET_PHYSICS
	if (thing->paHiearchyNodeMatrixOverrides[node->idx]) // use override if available
	{
		// invert the parent matrix
//		rdMatrix34 invMat;
//		rdMatrix_InvertOrtho34(&invMat, acc);
//
//		// undo the parent pivot transform
//		if (node->parent)
//			rdMatrix_PostTranslate34(&invMat, &node->parent->pivot);
//	
//		// move to local space
//		rdMatrix_Multiply34(&matrix, &invMat, thing->paHiearchyNodeMatrixOverrides[node->idx]);
//		
//		// remove the local pivot
//		rdVector_Neg3(&negPivot, &node->pivot);
//		rdMatrix_PreTranslate34(&matrix, &negPivot);
//
//	//	rdMatrix_Multiply34(&thing->hierarchyNodeMatrices[node->idx], acc, &matrix);
//		rdMatrix_Copy34(&thing->hierarchyNodeMatrices[node->idx], &matrix);
//
//		//if (node->parent)
//		//	rdMatrix_PreTranslate34(&thing->hierarchyNodeMatrices[node->idx], &node->parent->pivot);
//
//	//	rdVector_Neg3(&negPivot, &node->pivot);
////		rdMatrix_PostTranslate34(&thing->hierarchyNodeMatrices[node->idx], &negPivot);

		rdMatrix_Copy34(&thing->hierarchyNodeMatrices[node->idx], thing->paHiearchyNodeMatrixOverrides[node->idx]);
	
		//rdMatrix_Copy34(&matrix, thing->paHiearchyNodeMatrixOverrides[node->idx]);
		//rdMatrix_Multiply34(&thing->hierarchyNodeMatrices[node->idx], acc, &matrix);
	}
	else
#endif
	{
		rdMatrix_BuildTranslate34(&matrix, &node->pivot);
		rdMatrix_PostMultiply34(&matrix, &thing->hierarchyNodeMatrices[node->idx]);
		if ( node->parent )
		{
			rdVector_Neg3(&negPivot, &node->parent->pivot);
			rdMatrix_PostTranslate34(&matrix, &negPivot);
		}
		rdMatrix_Multiply34(&thing->hierarchyNodeMatrices[node->idx], acc, &matrix);
	}

    if (!node->numChildren)
        return;
    
    childIter = node->child;
    for (int i = 0; i < node->numChildren; i++)
    {
        if ( !thing->amputatedJoints[childIter->idx])
            rdThing_AccumulateMatrices(thing, childIter, &thing->hierarchyNodeMatrices[node->idx]);
        childIter = childIter->nextSibling;
    }
}
