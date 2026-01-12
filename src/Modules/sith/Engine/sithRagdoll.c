#include "sithRagdoll.h"

#include "Engine/sithPuppet.h"
#include "World/sithWorld.h"
#include "General/stdString.h"
#include "General/stdHashTable.h"
#include "World/sithModel.h"
#include "Modules/sith/Engine/sithConstraint.h"
#include "Primitives/rdQuat.h"
#include "Engine/rdClip.h"
#include "Engine/sithPhysics.h"
#include "General/stdMath.h"
#include "General/stdFont.h"
#include "General/stdString.h"
#include "Engine/sithCollision.h"
#include "Engine/sithIntersect.h"

#include "jk.h"

#ifdef PUPPET_PHYSICS

int sithRagdoll_activeRagdolls = 0;
int sithRagdoll_restingRagdolls = 0;

stdHashTable* sithRagdoll_hashtable = NULL;

int sithRagdoll_ConstraintTypeFromName(const char* name)
{
	static const char* sithRagdoll_constraintNames[SITH_CONSTRAINT_COUNT] =
	{
		"ballsocket",
		"cone",
		"hinge"
	};

	for (int i = 0; i < SITH_CONSTRAINT_COUNT; ++i)
	{
		if(stricmp(sithRagdoll_constraintNames[i], name) == 0)
			return i;
	}
	return -1;
}

int sithRagdoll_FindJointForBodypart(sithRagdoll* ragdoll, const char* name)
{
	for (int i = 0; i < ARRAY_SIZE(ragdoll->bodypart); ++i)
	{
		if (strnicmp(name, ragdoll->bodypart[i].name, 32) == 0)
			return i;
	}
	return -1;
}

int sithRagdoll_Load(sithWorld *world, int a2)
{
    if ( a2 )
        return 0;

    stdConffile_ReadArgs();
    if ( _strcmp(stdConffile_entry.args[0].value, "world") || _strcmp(stdConffile_entry.args[1].value, "ragdolls") )
        return 0;

	int num_ragdolls = _atoi(stdConffile_entry.args[2].value);
    if ( !num_ragdolls)
        return 1;

	sithRagdoll* ragdolls = (sithRagdoll*)pSithHS->alloc(sizeof(sithRagdoll) * num_ragdolls);
    world->ragdolls = ragdolls;
    if ( !ragdolls)
        return 0;

    world->numRagdolls = num_ragdolls;
    world->numRagdollsLoaded = 0;
    _memset(ragdolls, 0, sizeof(sithRagdoll) * num_ragdolls);
    while ( stdConffile_ReadArgs() )
    {
        if ( !_strcmp(stdConffile_entry.args[0].value, "end") )
            break;

        if ( !stdHashTable_GetKeyVal(sithRagdoll_hashtable, stdConffile_entry.args[1].value) )
        {
            if ( sithWorld_pLoading->numRagdollsLoaded != sithWorld_pLoading->numRagdolls )
            {
                sithRagdoll* ragdoll = &sithWorld_pLoading->ragdolls[sithWorld_pLoading->numRagdollsLoaded];
                _memset(ragdoll, 0, sizeof(sithRagdoll));
                _strncpy(ragdoll->name, stdConffile_entry.args[1].value, 0x1Fu);
				ragdoll->name[31] = 0;

				char rag_path[128];
				stdString_snprintf(rag_path, 128, "%s%c%s", "misc\\rag", 92, stdConffile_entry.args[1].value);
                if ( sithRagdoll_LoadRagdollEntry(ragdoll, rag_path) )
                {
                    ++sithWorld_pLoading->numRagdollsLoaded;
                    stdHashTable_SetKeyVal(sithRagdoll_hashtable, ragdoll->name, ragdoll);
                }
            }
        }
    }
    return 1;
}

sithRagdoll* sithRagdoll_LoadEntry(char *a1)
{
    int v3; // ecx
    sithRagdoll *v4; // esi
    char v6[128]; // [esp+10h] [ebp-80h] BYREF
	
	if (!sithWorld_pLoading->ragdolls)
	{
		sithWorld_pLoading->ragdolls = (sithRagdoll*)pSithHS->alloc(sizeof(sithRagdoll) * 128);
		if (!sithWorld_pLoading->ragdolls)
		{
			stdPrintf(pSithHS->errorPrint, ".\\Engine\\sithRagdoll.c", 163, "Memory error while loading ragdooll %s.\n", a1);
			return 0;
		}

		sithWorld_pLoading->numRagdolls = 128;
		sithWorld_pLoading->numRagdollsLoaded = 0;
		_memset(sithWorld_pLoading->ragdolls, 0, sizeof(sithRagdoll) * 128);
	}

    sithRagdoll* result = (sithRagdoll*)stdHashTable_GetKeyVal(sithRagdoll_hashtable, a1);
    if ( !result )
    {
        v3 = sithWorld_pLoading->numRagdollsLoaded;
		if (sithWorld_pLoading->numRagdollsLoaded < sithWorld_pLoading->numRagdolls)
		{
			v4 = &sithWorld_pLoading->ragdolls[v3];
			_memset(v4, 0, sizeof(sithAnimclass));
			_strncpy(v4->name, a1, 0x1Fu);
			v4->name[31] = 0,
			_sprintf(v6, "%s%c%s", "misc\\rag", 92, a1);
			if ( sithRagdoll_LoadRagdollEntry(v4, v6) )
			{
				++sithWorld_pLoading->numRagdollsLoaded;
				stdHashTable_SetKeyVal(sithRagdoll_hashtable, v4->name, v4);
				result = v4;
			}
        }
    }
    return result;
}

int sithRagdoll_LoadRagdollEntry(sithRagdoll* ragdoll, char* fpath)
{
    int mode; // ebx
    int bodypart_idx; // esi
    int joint_idx; // eax
    intptr_t animNameIdx; // ebp
    sithWorld *world; // esi
    char *key_fname; // edi
    rdKeyframe *v10; // eax
    unsigned int v12; // eax
    rdKeyframe *keyframe; // edi
    int lowpri; // [esp+4h] [ebp-8Ch]
    int flags; // [esp+8h] [ebp-88h] BYREF
    int hipri; // [esp+Ch] [ebp-84h]
    char keyframe_fpath[128]; // [esp+10h] [ebp-80h] BYREF
	rdModel3* model = NULL;
	int namedBodypart = 0;

	float mass = 1.0f / JOINTTYPE_NUM_JOINTS;
	float buoyancy = 1.0f;
	float health = 1000.0f;
	float damage = 1.0f;
	ragdoll->root = -1;

    mode = 0;
    if (!stdConffile_OpenRead(fpath))
        return 0;

	_memset(ragdoll->bodypart, 0x0, sizeof(ragdoll->bodypart));
	ragdoll->jointToBodypart = NULL;
	while ( stdConffile_ReadArgs() )
    {
        if ( !stdConffile_entry.numArgs )
            continue;

		if (!_strcmp(stdConffile_entry.args[0].key, "model"))
		{
			model = sithModel_LoadEntry(stdConffile_entry.args[0].value, 0);
			ragdoll->jointToBodypart = (int*)rdroid_pHS->alloc(sizeof(int) * model->numHierarchyNodes);
			if(ragdoll->jointToBodypart)
				_memset(ragdoll->jointToBodypart, -1, sizeof(int) * model->numHierarchyNodes);
		}
		else if (!_strcmp(stdConffile_entry.args[0].key, "flags"))
		{
			_sscanf(stdConffile_entry.args[0].value, "%x", &ragdoll->flags);
		}
        else if ( !_strcmp(stdConffile_entry.args[0].value, "joints") )
        {
			if (!model)
			{
				stdPrintf(pSithHS->errorPrint, ".\\Engine\\sithRagdoll.c", 163, "Model not defined for ragdoll %s.\n", fpath);
				break;
			}

			bodypart_idx = 0;
            while ( stdConffile_ReadArgs() )
            {
                if ( !stdConffile_entry.numArgs || !_strcmp(stdConffile_entry.args[0].key, "end") )
                    break;
				
				if (bodypart_idx >= ARRAY_SIZE(ragdoll->bodypart))
				{
					stdPrintf(pSithHS->errorPrint, ".\\Engine\\sithRagdoll.c", 163, "Too many parts for ragdoll %s.\n", fpath);
					break;
				}
				
				strncpy(ragdoll->bodypart[bodypart_idx].name, stdConffile_entry.args[0].key, 32);

				if(stdConffile_entry.numArgs > 1)
				{

					joint_idx = -1;
					for (int node = 0; node < model->numHierarchyNodes; ++node)
					{
						if (!stricmp(model->hierarchyNodes[node].name, stdConffile_entry.args[1].key))
						{
							joint_idx = node;
							if(ragdoll->jointToBodypart)
								ragdoll->jointToBodypart[node] = bodypart_idx;
							break;
						}
					}

					if (stdConffile_entry.numArgs <= 2)
						flags = 0;
					else
						_sscanf(stdConffile_entry.args[2].key, "%x", &flags);

					if (stdConffile_entry.numArgs <= 3)
						mass = 1.0;
					else
						mass = _atof(stdConffile_entry.args[3].key);

					if (stdConffile_entry.numArgs <= 4)
						buoyancy = 1.0;
					else
						buoyancy = _atof(stdConffile_entry.args[4].key);

					if (stdConffile_entry.numArgs <= 5)
						health = 1000.0f;
					else
						health = _atof(stdConffile_entry.args[5].key);
					
					if (stdConffile_entry.numArgs <= 6)
						damage = 1.0;
					else
						damage = _atof(stdConffile_entry.args[6].key);
				}				

				ragdoll->bodypart[bodypart_idx].nodeIdx = joint_idx;
				if(joint_idx >= 0)
				{
					ragdoll->jointBits |= (1ull << bodypart_idx);
					if (flags & JOINTFLAGS_PHYSICS)
						ragdoll->physicsJointBits |= (1ull << bodypart_idx);
					ragdoll->bodypart[bodypart_idx].flags = flags;
					ragdoll->bodypart[bodypart_idx].mass = mass;
					ragdoll->bodypart[bodypart_idx].buoyancy = buoyancy;
					ragdoll->bodypart[bodypart_idx].health = health;
					ragdoll->bodypart[bodypart_idx].damage = damage;
					if (flags & JOINTFLAGS_ROOT)
						ragdoll->root = bodypart_idx;
				}
				++bodypart_idx;
			}
        }
		else if (!_strcmp(stdConffile_entry.args[0].value, "constraints"))
		{
			while (stdConffile_ReadArgs())
			{
				if (!stdConffile_entry.numArgs || !_strcmp(stdConffile_entry.args[0].key, "end"))
					break;

				int type, targetJoint, constrainedJoint;
				if (model && stdConffile_entry.numArgs > 2)
				{
					type = sithRagdoll_ConstraintTypeFromName(stdConffile_entry.args[0].key);
					if(type != -1)
					{
						constrainedJoint = sithRagdoll_FindJointForBodypart(ragdoll, stdConffile_entry.args[1].key);
						targetJoint = sithRagdoll_FindJointForBodypart(ragdoll, stdConffile_entry.args[2].key);
				
						if (targetJoint != -1 && constrainedJoint != -1)
						{
							sithRagdollConstraint* constraint = (sithRagdollConstraint*)rdroid_pHS->alloc(sizeof(sithRagdollConstraint));
							memset(constraint, 0, sizeof(sithRagdollConstraint));
							constraint->type = type;
							constraint->jointB = constrainedJoint;
							constraint->jointA = targetJoint;
							constraint->next = ragdoll->constraints;
							ragdoll->constraints = constraint;
					
							if (stdConffile_entry.numArgs > 3)
							{
								if (_sscanf(stdConffile_entry.args[3].key,
											"(%f/%f/%f)",
											&constraint->axisB.x,
											&constraint->axisB.y,
											&constraint->axisB.z) != 3
									)
								{
									continue;
								}
							}
							if (stdConffile_entry.numArgs > 4)
							{
								if (_sscanf(stdConffile_entry.args[4].key,
											"(%f/%f/%f)",
											&constraint->axisA.x,
											&constraint->axisA.y,
											&constraint->axisA.z) != 3
									)
								{
									continue;
								}
							}
							if (stdConffile_entry.numArgs > 5)
								constraint->angle0 = atof(stdConffile_entry.args[5].key);

							if (stdConffile_entry.numArgs > 6)
								constraint->angle1 = atof(stdConffile_entry.args[6].key);
						}
					}
				}
			}
		}
    }
    stdConffile_Close();
    return 1;
}

void sithRagdoll_Free(sithWorld *world)
{
    unsigned int v1; // edi
    int v2; // ebx

    if ( world->numRagdolls )
    {
        v1 = 0;
        if ( world->numRagdollsLoaded )
        {
            v2 = 0;
            do
            {
				if(world->ragdolls[v2].jointToBodypart)
					rdroid_pHS->free(world->ragdolls[v2].jointToBodypart);
				
				sithRagdollConstraint* constraint = world->ragdolls[v2].constraints;
				while (constraint)
				{
					sithRagdollConstraint* next = constraint->next;
					if(constraint)
						rdroid_pHS->free(constraint);
					constraint = next;
				}

                stdHashTable_FreeKey(sithRagdoll_hashtable, world->ragdolls[v2].name);
                ++v1;
                ++v2;
            }
            while ( v1 < world->numRagdollsLoaded);
        }
        pSithHS->free(world->ragdolls);
        world->ragdolls = 0;
        world->numRagdollsLoaded = 0;
        world->numRagdolls = 0;
    }
}

void sithRagdoll_FreeInstance(sithThing* puppet)
{
	if (puppet->ragdoll)
	{
		sithRagdoll_StopPhysics(puppet);
		pSithHS->free(puppet->ragdoll);
		puppet->ragdoll = 0;
	}
}

void sithRagdoll_AddBallSocketConstraint(sithThing* pThing, int joint, int target)
{
	uint64_t hasJoint = pThing->animclass->ragdoll->jointBits & (1ull << joint);
	uint64_t hasTarget = pThing->animclass->ragdoll->jointBits & (1ull << target);
	if (!hasJoint || !hasTarget)
		return;

	sithThing* targetThing = &pThing->ragdoll->joints[target].thing;
	sithThing* jointThing = &pThing->ragdoll->joints[joint].thing;

	rdHierarchyNode* pNode = &pThing->rdthing.model3->hierarchyNodes[pThing->animclass->ragdoll->bodypart[joint].nodeIdx];

	targetThing->lookOrientation.scale = targetThing->position;
	jointThing->lookOrientation.scale = jointThing->position;

	rdVector3 anchorB = pNode->pivot;
	rdVector_Neg3Acc(&anchorB);

	rdMatrix34 invTargetMat;
	rdMatrix_InvertOrtho34(&invTargetMat, &targetThing->lookOrientation);

	rdMatrix34 localJointMat;
	rdMatrix_Multiply34(&localJointMat, &invTargetMat, &jointThing->lookOrientation);

	rdVector3 anchorA;
	rdMatrix_TransformPoint34(&anchorA, &anchorB, &localJointMat);

	targetThing->lookOrientation.scale = rdroid_zeroVector3;
	jointThing->lookOrientation.scale = rdroid_zeroVector3;

	sithConstraint_AddBallSocketConstraint(pThing, jointThing, targetThing, &anchorA, &anchorB, 0.0f);
}

void sithRagdoll_AddConeConstraint(sithThing* pThing, int joint, int target, const rdVector3* axis, const rdVector3* targetAxis, flex_t angle, flex_t twistAngle)
{
	uint64_t hasJoint = pThing->animclass->ragdoll->jointBits & (1ull << joint);
	uint64_t hasTarget = pThing->animclass->ragdoll->jointBits & (1ull << target);
	if (!hasJoint || !hasTarget)
		return;

	sithThing* targetThing = &pThing->ragdoll->joints[target].thing;
	sithThing* jointThing = &pThing->ragdoll->joints[joint].thing;

	rdHierarchyNode* pNode = &pThing->rdthing.model3->hierarchyNodes[pThing->animclass->ragdoll->bodypart[joint].nodeIdx];
	sithConstraint_AddConeConstraint(pThing, jointThing, targetThing, targetAxis, angle, axis, twistAngle);
}

void sithRagdoll_AddHingeConstraint(sithThing* pThing, int joint, int target, const rdVector3* axis, const rdVector3* targetAxis, flex_t minAngle, flex_t maxAngle)
{
	uint64_t hasJoint = pThing->animclass->ragdoll->jointBits & (1ull << joint);
	uint64_t hasTarget = pThing->animclass->ragdoll->jointBits & (1ull << target);
	if (!hasJoint || !hasTarget)
		return;

	sithThing* targetThing = &pThing->ragdoll->joints[target].thing;
	sithThing* jointThing = &pThing->ragdoll->joints[joint].thing;

	rdHierarchyNode* pNode = &pThing->rdthing.model3->hierarchyNodes[pThing->animclass->ragdoll->bodypart[joint].nodeIdx];

	sithConstraint_AddHingeConstraint(pThing, jointThing, targetThing, targetAxis, axis, minAngle, maxAngle);
}

void sithRagdoll_AllocConstraints(sithThing* pThing)
{
	pThing->numTotalConstraints = 0;
	sithRagdollConstraint* constraints = pThing->animclass->ragdoll->constraints;
	for (; constraints; constraints = constraints->next)
	{
		if (constraints->jointA < 0 || constraints->jointB < 0)
			continue;
		++pThing->numTotalConstraints;
	}
	if (pThing->constraints)
	{
		pSithHS->free(pThing->constraints);
	}
	pThing->constraints = pSithHS->alloc(sizeof(sithConstraint) * pThing->numTotalConstraints);
	pThing->numConstraints = 0;
}

void sithRagdoll_SetupJointThing(sithThing* pThing, sithThing* pJointThing, sithRagdollPart* pBodyPart, rdHierarchyNode* pNode, int jointIdx, const rdVector3* pInitialVel)
{
	extern int sithThing_bInitted2;

	sithThing_DoesRdThingInit(pJointThing);
	pJointThing->rdthing.curGeoMode = 0;
	pJointThing->rdthing.desiredGeoMode = 0;
	pJointThing->thingflags = SITH_TF_INVISIBLE;
	pJointThing->thingIdx = (pThing->thingIdx << 16) + jointIdx;
	pJointThing->signature = sithThing_bInitted2++;
	pJointThing->thing_id = -1;
	pJointThing->type = SITH_THING_CORPSE;
	pJointThing->moveType = SITH_MT_PHYSICS;
	pJointThing->prev_thing = pThing;
	pJointThing->child_signature = pThing->signature;

	stdString_SafeStrCopy(pJointThing->template_name, pBodyPart->name, 32);

	// initialize the position velocity using the animation frames
	rdVector_Copy3(&pJointThing->position, &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx].scale);

	// orient to the joint matrix
	rdMatrix_Copy34(&pJointThing->lookOrientation, &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx]);
	rdVector_Zero3(&pJointThing->lookOrientation.scale);

	pJointThing->moveSize = pJointThing->collideSize = 0.01f;

	rdMesh* pMesh = NULL;
	if (pNode->meshIdx >= 0)
	{
		pMesh = &pThing->rdthing.model3->geosets[0].meshes[pNode->meshIdx];
		flex_t avgDist = (pMesh->minRadius + pMesh->maxRadius) * 0.5f;
		pJointThing->moveSize = avgDist;
		pJointThing->collideSize = avgDist;
	}

	pJointThing->jointPivotOffset = pNode->pivot;
	rdVector_Neg3Acc(&pJointThing->jointPivotOffset);

	if (pBodyPart->flags & JOINTFLAGS_PHYSICS)
		pJointThing->collide = SITH_COLLIDE_SPHERE;
	else
		pJointThing->collide = SITH_COLLIDE_NONE;

	if (pBodyPart->flags & JOINTFLAGS_PHYSICS)
	{
		// setup physics params
		_memcpy(&pJointThing->physicsParams, &pThing->physicsParams, sizeof(sithThingPhysParams));
		pJointThing->physicsParams.mass *= pBodyPart->mass;
		// note: physicsParams.buoyancy is applied to gravity directly
		// for an upward force, we need it to be negative
		pJointThing->physicsParams.buoyancy = -sithPhysics_BuoyancyFromRadius(pJointThing->moveSize);
		pJointThing->physicsParams.height = 0.0f;// pMesh ? pMesh->maxRadius : 0.0f;

		pJointThing->physicsParams.physflags = SITH_PF_FEELBLASTFORCE;
		pJointThing->physicsParams.physflags |= SITH_PF_ANGIMPULSE;

		// todo: it would probably be better to simply attach the root
		// and constrain the hip as a fixed joint and let everything else follow
		pJointThing->physicsParams.physflags |= SITH_PF_FLOORSTICK;

		// todo: do we want to propagate fly flags?
		if (pThing->physicsParams.physflags & SITH_PF_USEGRAVITY)
			pJointThing->physicsParams.physflags |= SITH_PF_USEGRAVITY;

		if (pThing->physicsParams.physflags & SITH_PF_PARTIALGRAVITY)
			pJointThing->physicsParams.physflags |= SITH_PF_PARTIALGRAVITY;

		if (pThing->physicsParams.physflags & SITH_PF_NOTHRUST)
			pJointThing->physicsParams.physflags |= SITH_PF_NOTHRUST;

		if (pBodyPart->flags & JOINTFLAGS_BOUNCE)
			pJointThing->physicsParams.physflags |= SITH_PF_SURFACEBOUNCE;

		pJointThing->physicsParams.staticDrag = fmax(pJointThing->physicsParams.staticDrag, 0.01f);
		pJointThing->physicsParams.surfaceDrag *= 2.0f;
		pJointThing->physicsParams.airDrag *= 2.0f;

		rdVector3 vel;
		rdMatrix_TransformVector34(&vel, &pThing->rdthing.paHierarchyNodeVelocities[pNode->idx], &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx]);
		rdVector_Copy3(&pJointThing->physicsParams.vel, &vel);
		rdVector_Add3Acc(&pJointThing->physicsParams.vel, pInitialVel);
		//rdVector_Copy3(&pJointThing->physicsParams.angVel, &pThing->rdthing.paHierarchyNodeAngularVelocities[pNode->idx]);
		sithPhysics_AnglesToAngularVelocity(&pJointThing->physicsParams.rotVel, &pThing->rdthing.paHierarchyNodeAngularVelocities[pNode->idx], &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx]);
	}

	// enter the things sector to start physics
	//sithThing_EnterSector(pJointThing, pThing->sector, 1, 0);

	// try to place the joint at the sector it's inside of
	sithSector* pJointSector = sithCollision_GetSectorLookAt(pThing->sector, &pThing->position, &pJointThing->position, 0.0f);
	sithThing_EnterSector(pJointThing, pJointSector, 1, 0);
}

void sithRagdoll_StartPhysics(sithThing* pThing, rdVector3* pInitialVel, flex_t deltaSeconds)
{
	if (!pThing->animclass || !pThing->animclass->ragdoll || pThing->rdthing.type != RD_THINGTYPE_MODEL || !pThing->rdthing.model3 || !pThing->puppet || !pThing->rdthing.puppet || (g_debugmodeFlags & DEBUGFLAG_NO_PUPPETS))
		return;

	if (pThing->ragdoll)
		sithRagdoll_StopPhysics(pThing);

	sithRagdollInstance* result = (sithRagdollInstance*)pSithHS->alloc(sizeof(sithRagdollInstance));
	if (!result)
		return;
	_memset(result, 0, sizeof(sithRagdollInstance));

	pThing->ragdoll = result;

	if (pThing->rdthing.paHiearchyNodeMatrixOverrides)
		memset(pThing->rdthing.paHiearchyNodeMatrixOverrides, NULL, sizeof(rdMatrix34*) * pThing->rdthing.model3->numHierarchyNodes);

	rdVector3 thingVel;
	rdVector_Scale3(&thingVel, pInitialVel, deltaSeconds);

	// give the animation some time to prime just in case this was trigger between animations
	//rdPuppet_UpdateTracks(pThing->rdthing.puppet, deltaSeconds);

	if (pThing->rdthing.frameTrue != rdroid_frameTrue)
	{
		rdVector_Copy3(&pThing->lookOrientation.scale, &pThing->position);
		rdPuppet_BuildJointMatrices(&pThing->rdthing, &pThing->lookOrientation);
		rdVector_Zero3(&pThing->lookOrientation.scale);
	}

	if (pThing->rdthing.paHierarchyNodeMatricesPrev) // todo: only do this when needed
		_memcpy(pThing->rdthing.paHierarchyNodeMatricesPrev, pThing->rdthing.hierarchyNodeMatrices, sizeof(rdMatrix34) * pThing->rdthing.model3->numHierarchyNodes);

	// todo: do we really need all the joints or just the physicalized ones?
	uint64_t jointBits = pThing->animclass->ragdoll->jointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollPart* pBodyPart = &pThing->animclass->ragdoll->bodypart[jointIdx];
		rdHierarchyNode* pNode = &pThing->rdthing.model3->hierarchyNodes[pBodyPart->nodeIdx];
		//if (pNode->meshIdx < 0)
			//continue;

		sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];

		pJoint->localMat = pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx];
		sithRagdoll_SetupJointThing(pThing, &pJoint->thing, pBodyPart, pNode, jointIdx, pInitialVel);
	}

	// build constraints
	sithRagdoll_AllocConstraints(pThing);

	sithRagdollConstraint* constraints = pThing->animclass->ragdoll->constraints;
	for (; constraints; constraints = constraints->next)
	{
		if (constraints->jointA < 0 || constraints->jointB < 0)
			continue;

		switch (constraints->type)
		{
		case SITH_CONSTRAINT_BALLSOCKET:
			sithRagdoll_AddBallSocketConstraint(pThing, constraints->jointB, constraints->jointA);
			break;
		case SITH_CONSTRAINT_CONE:
			sithRagdoll_AddConeConstraint(pThing, constraints->jointB, constraints->jointA, &constraints->axisB, &constraints->axisA, constraints->angle0, constraints->angle1);
			break;
		case SITH_CONSTRAINT_HINGE:
			sithRagdoll_AddHingeConstraint(pThing, constraints->jointB, constraints->jointA, &constraints->axisB, &constraints->axisA, constraints->angle0, constraints->angle1);
			break;
		default:
			break;
		}
	}
}

void sithRagdoll_StopPhysics(sithThing* pThing)
{
	if (pThing->animclass && pThing->animclass->ragdoll && pThing->ragdoll)
	{
		uint64_t jointBits = pThing->animclass->ragdoll->jointBits;
		while (jointBits != 0)
		{
			int jointIdx = stdMath_FindLSB64(jointBits);
			jointBits ^= 1ull << jointIdx;

			sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
			sithThing_FreeEverything(&pJoint->thing);
		}
		pSithHS->free(pThing->ragdoll);
		pThing->ragdoll = 0;
		sithPhysics_ThingStop(pThing);
	}
	if (pThing->rdthing.paHiearchyNodeMatrixOverrides)
		memset(pThing->rdthing.paHiearchyNodeMatrixOverrides, NULL, sizeof(rdMatrix34*) * pThing->rdthing.model3->numHierarchyNodes);
}

static void sithRagdoll_UpdateJointMatrices(sithThing* thing)
{
	rdMatrix34 invMat;
	rdMatrix_InvertOrtho34(&invMat, &thing->lookOrientation);

	uint64_t jointBits = thing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollPart* pBodyPart = &thing->animclass->ragdoll->bodypart[jointIdx];
		if (pBodyPart->nodeIdx < thing->rdthing.rootJoint || thing->rdthing.amputatedJoints[pBodyPart->nodeIdx])
		{
			// make sure this is cleared
			thing->rdthing.paHiearchyNodeMatrixOverrides[pBodyPart->nodeIdx] = NULL;
			continue;
		}

		sithRagdollJoint* pJoint = &thing->ragdoll->joints[jointIdx];
		rdHierarchyNode* pNode = &thing->rdthing.model3->hierarchyNodes[pBodyPart->nodeIdx];

		// smoothly interpolate the matrix changes as the physics system can be a little jittery
		const flex_t interpolation = 0.3f; // should maybe be a cvar

		rdVector3 oldPos = pJoint->localMat.scale; // keep this around so we don't lose it

		// interpolate the orientation via quaternion
		rdQuat q, q0, q1;
		rdQuat_BuildFrom34(&q0, &pJoint->localMat);
		rdQuat_BuildFrom34(&q1, &pJoint->thing.lookOrientation);
		rdQuat_Slerp(&q, &q0, &q1, interpolation);
		rdQuat_ToMatrix(&pJoint->localMat, &q);

		// interpolate the position + pivot offset
		rdVector3 pos, pivot;
		rdMatrix_TransformVector34(&pivot, &pJoint->thing.jointPivotOffset, &pJoint->thing.lookOrientation);
		rdVector_Add3(&pos, &pJoint->thing.position, &pivot);
		rdVector_Lerp3(&pJoint->localMat.scale, &oldPos, &pJoint->thing.position, interpolation);

		thing->rdthing.paHiearchyNodeMatrixOverrides[pBodyPart->nodeIdx] = &pJoint->localMat;
	}
}

static inline void sithRagdoll_UpdateJointThing(sithThing* pThing, sithThing* pJointThing, sithRagdollPart* pBodyPart, flex_t deltaSeconds)
{
	// don't collide if the node is amputated or lower than the root joint
	// (but update the position for sector traversal)
	int collide = pJointThing->collide;
	if (pBodyPart->nodeIdx < pThing->rdthing.rootJoint || pThing->rdthing.amputatedJoints[pBodyPart->nodeIdx])
		pJointThing->collide = SITH_COLLIDE_NONE;

	// apply animation velocities
	//rdVector3 vel;
	//rdMatrix_TransformVector34(&vel, &pThing->rdthing.paHierarchyNodeVelocities[pBodyPart->nodeIdx], &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx]);
	//rdVector_MultAcc3(&pJointThing->physicsParams.vel, &vel, deltaSeconds);
	//
	//rdVector3 rotVel;
	//sithPhysics_AnglesToAngularVelocity(&rotVel, &pThing->rdthing.paHierarchyNodeAngularVelocities[pBodyPart->nodeIdx], &pThing->rdthing.hierarchyNodeMatrices[pBodyPart->nodeIdx]);
	//rdVector_MultAcc3(&pJointThing->physicsParams.rotVel, &rotVel, deltaSeconds);

	sithPhysics_ThingTick(pJointThing, deltaSeconds);
	sithThing_TickPhysics(pJointThing, deltaSeconds);

	// reset collision
	pJointThing->collide = collide;
}

static void sithRagdoll_UpdateJoints(sithThing* pThing, flex_t deltaSeconds)
{
	uint64_t jointBits = pThing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollPart* pBodyPart = &pThing->animclass->ragdoll->bodypart[jointIdx];
		sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
		sithRagdoll_UpdateJointThing(pThing, &pJoint->thing, pBodyPart, deltaSeconds);
	}
}

static void sithRagdoll_StopAll(sithThing* pThing)
{
	sithPhysics_ThingStop(pThing);

	uint64_t jointBits = pThing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
		sithPhysics_ThingStop(&pJoint->thing);
	}
}

static void sithRagdoll_UpdateRestingData(sithThing* thing, flex_t deltaSeconds)
{
	// already resting
	if (thing->physicsParams.physflags & SITH_PF_RESTING)
		return;

	// only update if we're about to start a new test
	if (thing->physicsParams.restTimer > 0.0)
		return;

	// start accumulating the timer
	thing->physicsParams.restTimer += deltaSeconds;

	// update the last position and orientation for comparing
	uint64_t jointBits = thing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &thing->ragdoll->joints[jointIdx];
		pJoint->thing.physicsParams.lastPos = pJoint->thing.position;
		pJoint->thing.physicsParams.lastOrient = pJoint->thing.lookOrientation;
	}
}

static int sithRagdoll_CheckForStillBodies(sithThing* thing, flex_t deltaSeconds)
{
	// only test every so often so that we capture changes across a larger time step
	if (thing->physicsParams.restTimer <= 1.0)
	{
		// not ready yet
		thing->physicsParams.restTimer += deltaSeconds;
		return 0;
	}

	// reset the timer for another test
	thing->physicsParams.restTimer = 0.0f;

	// check for the largest motion of all joints
	flex_t maxDistSq = 0.0f;
	flex_t maxAngle = 0.0f;

	uint64_t jointBits = thing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &thing->ragdoll->joints[jointIdx];

		// position difference
		flex_t distSq = rdVector_DistSquared3(&pJoint->thing.physicsParams.lastPos, &pJoint->thing.position);
		if (maxDistSq < distSq)
			maxDistSq = distSq;

		// orientation difference
		rdMatrix34 invLastOrient;
		rdMatrix_InvertOrtho34(&invLastOrient, &pJoint->thing.physicsParams.lastOrient);

		rdMatrix34 localMat;
		rdMatrix_Multiply34(&localMat, &invLastOrient, &pJoint->thing.lookOrientation);

		// todo: this seems jank, maybe test PYR?
		rdVector3 axis;
		flex_t angle;
		rdMatrix_ExtractAxisAngle34(&localMat, &axis, &angle);
		if (maxAngle < angle)
			maxAngle = angle;
	}

	// if there wasn't substantial movement among the joints during the rest period, we can rest
	return (maxDistSq < 0.025f && maxAngle < 15.0f);
}

static int sithRagdoll_CheckVelocities(sithThing* thing, flex_t deltaSeconds)
{
	// must not rest if any of the bodies have significant enough velocity
	uint64_t jointBits = thing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &thing->ragdoll->joints[jointIdx];

		flex_t velLenSq = rdVector_Dot3(&pJoint->thing.physicsParams.vel, &pJoint->thing.physicsParams.vel);
		if (velLenSq > 0.005f)
			return 0;

		flex_t rotVelLenSq = rdVector_Dot3(&pJoint->thing.physicsParams.rotVel, &pJoint->thing.physicsParams.rotVel);
		if (rotVelLenSq > 0.01f)
			return 0;
	}
	return 1;
}

static int sithRagdoll_IsAtRest(sithThing* thing, flex_t deltaSeconds)
{
	if (thing->physicsParams.physflags & SITH_PF_RESTING)
		return 1;

	// just rest if we haven't been visible for a long time
	int frames = jkPlayer_currentTickIdx - thing->lastRenderedTickIdx;
	if (frames > 10000)
		return 1;

	// assume in free fall if the root joint isn't attached
	int rootJoint = thing->animclass->ragdoll->root < 0 ? 0 : thing->animclass->ragdoll->root;
	sithRagdollJoint* pJoint = &thing->ragdoll->joints[rootJoint];
	if (!pJoint->thing.attach_flags)
		return 0;

	// if all of the joints are still, go to rest
	if (sithRagdoll_CheckForStillBodies(thing, deltaSeconds))
		return 1;

	// finally check the velocities, if they're very small we can rest
	return sithRagdoll_CheckVelocities(thing, deltaSeconds);
}

static void sithRagdoll_UpdatePhysicsParent(sithThing* thing)
{
	// pin the thing to the root joint
	int rootJoint = thing->animclass->ragdoll->root < 0 ? 0 : thing->animclass->ragdoll->root;
	sithRagdollJoint* pJoint = &thing->ragdoll->joints[rootJoint];
	rdVector_Copy3(&thing->position, &pJoint->thing.position);
	sithThing_MoveToSector(thing, pJoint->thing.sector, 1);
}

// todo: just update when root or amputatedJoints is changed?
static void sithRagdoll_ValidateConstraints(sithThing* thing)
{
	//sithConstraint* constraint = thing->constraints;	
	//for (; constraint; constraint = constraint->next)
	for (int i = 0; i < thing->numConstraints; ++i)
	{
		sithConstraint* constraint = &thing->constraints[i];
		int idxA = constraint->targetThing->thingIdx & 0xFFFF;
		int idxB = constraint->constrainedThing->thingIdx & 0xFFFF;

		if (thing->animclass->ragdoll->bodypart[idxA].nodeIdx < thing->rdthing.rootJoint
			|| thing->rdthing.amputatedJoints[thing->animclass->ragdoll->bodypart[idxA].nodeIdx]
			|| thing->animclass->ragdoll->bodypart[idxB].nodeIdx < thing->rdthing.rootJoint
			|| thing->rdthing.amputatedJoints[thing->animclass->ragdoll->bodypart[idxB].nodeIdx])
		{
			constraint->flags |= SITH_CONSTRAINT_DISABLED;
		}
		else
		{
			constraint->flags &= ~SITH_CONSTRAINT_DISABLED;
		}
	}
}

void sithRagdoll_ThingTick(sithThing* thing, flex_t deltaSeconds)
{
	if (thing->physicsParams.physflags & SITH_PF_RESTING)
	{
		++sithRagdoll_restingRagdolls;
		return;
	}

	if (!thing->ragdoll)
		sithRagdoll_StartPhysics(thing, &thing->physicsParams.vel, deltaSeconds);

	++sithRagdoll_activeRagdolls;
	sithRagdoll_UpdateJoints(thing, deltaSeconds);
	sithRagdoll_UpdateJointMatrices(thing);
	sithRagdoll_UpdatePhysicsParent(thing);
	sithRagdoll_UpdateRestingData(thing, deltaSeconds);
	if (sithRagdoll_IsAtRest(thing, deltaSeconds))
	{
		thing->physicsParams.physflags |= SITH_PF_RESTING;
		sithRagdoll_StopAll(thing);
	}
}

void sithRagdoll_DebugDrawPhysicsBodies(sithThing* pThing)
{
	if (pThing->rdthing.type != RD_THINGTYPE_MODEL || !pThing->animclass || !pThing->puppet || !pThing->ragdoll)
		return;

	// could actually do all this by giving the joint things themselves some draw data
	uint64_t jointBits = pThing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
		rdVector3 offset = { 0, -pJoint->thing.moveSize, 0 };

		rdSprite debugSprite;
		rdSprite_NewEntry(&debugSprite, "dbgragoll", 0, "saberblue0.mat", pJoint->thing.moveSize * 2.0f, pJoint->thing.moveSize * 2.0f, RD_GEOMODE_TEXTURED, RD_LIGHTMODE_FULLYLIT, RD_TEXTUREMODE_AFFINE, 1.0f, &offset);

		rdThing debug;
		rdThing_NewEntry(&debug, pThing);
		rdThing_SetSprite3(&debug, &debugSprite);
		rdMatrix34 mat;
		rdMatrix_BuildTranslate34(&mat, &pJoint->thing.position);

		rdSprite_Draw(&debug, &mat);

		rdSprite_FreeEntry(&debugSprite);
		rdThing_FreeEntry(&debug);
	}
}

void sithRagdoll_DebugDrawPhysicsJoints(sithThing* pThing)
{
	if (pThing->rdthing.type != RD_THINGTYPE_MODEL || !pThing->animclass || !pThing->puppet || !pThing->ragdoll)
		return;

	// could actually do all this by giving the joint things themselves some draw data
	uint64_t jointBits = pThing->animclass->ragdoll->physicsJointBits;
	while (jointBits != 0)
	{
		int jointIdx = stdMath_FindLSB64(jointBits);
		jointBits ^= 1ull << jointIdx;

		sithRagdollJoint* pJoint = &pThing->ragdoll->joints[jointIdx];
		rdVector3 offset = { 0, -pJoint->thing.moveSize * 0.5f, 0 };

		rdVector3 jointPivotOffset;
		rdMatrix_TransformVector34(&jointPivotOffset, &pJoint->thing.jointPivotOffset, &pJoint->thing.lookOrientation);

		rdVector3 pos;
		rdVector_Add3(&pos, &pJoint->thing.position, &jointPivotOffset);

		rdSprite debugSprite;
		rdSprite_NewEntry(&debugSprite, "dbgragoll", 0, "sabergreen0.mat", 0.005f, 0.005f, RD_GEOMODE_TEXTURED, RD_LIGHTMODE_FULLYLIT, RD_TEXTUREMODE_AFFINE, 1.0f, &offset);

		rdThing debug;
		rdThing_NewEntry(&debug, pThing);
		rdThing_SetSprite3(&debug, &debugSprite);
		rdMatrix34 mat;
		rdMatrix_BuildTranslate34(&mat, &pos);

		rdSprite_Draw(&debug, &mat);

		rdSprite_FreeEntry(&debugSprite);
		rdThing_FreeEntry(&debug);
	}
}


#endif
