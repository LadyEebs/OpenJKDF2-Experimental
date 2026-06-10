#include "sithPhysics.h"

#include "General/stdMath.h"
#include "Primitives/rdMath.h"
#include "Engine/sithCollision.h"
#include "World/sithSurface.h"
#include "World/sithThing.h"
#include "World/sithSector.h"
#include "World/jkPlayer.h"
#include "jk.h"

void sithPhysics_FindFloor(sithThing *pThing, int a3)
{
    int v4; // ecx
    sithCollisionSearchEntry *v5; // eax
    flex_d_t v8; // st7
    flex_d_t v9; // st7
    sithCollisionSearchEntry *i; // esi
    sithThing *v11; // edi
    rdFace *v12; // eax
    int searchFlags; // [esp+10h] [ebp-20h]
    flex_t range; // [esp+14h] [ebp-1Ch]
    rdVector3 direction; // [esp+18h] [ebp-18h] BYREF
    rdVector3 a1; // [esp+24h] [ebp-Ch] BYREF
    flex_t thinga; // [esp+34h] [ebp+4h]

    // Added: noclip
    if ((g_debugmodeFlags & DEBUGFLAG_NOCLIP) && pThing == sithPlayer_pLocalPlayerThing)
    {
        pThing->physicsParams.physflags &= ~SITH_PF_USEGRAVITY;
        pThing->physicsParams.physflags |= SITH_PF_FLY;

        sithThing_DetachThing(pThing);
        return;
    }

    range = 0.0;
    searchFlags = 0;
    if (!pThing->sector)
        return;

    if (pThing->sector->flags & SITH_SECTOR_UNDERWATER && pThing->type == SITH_THING_PLAYER)
    {
        sithCollision_SearchRadiusForThings(pThing->sector, pThing, &pThing->position, &rdroid_zVector3, 0.05, 0.0, SITH_RAYCAST_IGNORE_THINGS);
        v5 = sithCollision_NextSearchResult();
        if ( v5 )
        {
            while ( (v5->hitType & SITHCOLLISION_ADJOINCROSS) == 0 || (v5->surface->adjoin->sector->flags & SITH_SECTOR_UNDERWATER) != 0 )
            {
                v5 = sithCollision_NextSearchResult();
                if ( !v5 )
                    goto LABEL_8;
            }
            pThing->field_48 = v5->distance;
            pThing->physicsParams.physflags |= SITH_PF_WATERSURFACE;
            sithCollision_SearchClose();
        }
        else
        {
LABEL_8:
            sithCollision_SearchClose();
            pThing->physicsParams.physflags &= ~SITH_PF_WATERSURFACE;
        }
    }
    else
    {
        if ( (pThing->physicsParams.physflags & SITH_PF_WALLSTICK) == 0 )
        {
            direction.x = -0.0;
            direction.y = direction.x;
            direction.z = -1.0;
            searchFlags = SITH_RAYCAST_IGNORE_FLOOR;
        }
        else
        {
            rdVector_Neg3(&direction, &pThing->lookOrientation.uvec);
        }

        if ( a3 || pThing->attach_flags )
        {
            v9 = pThing->physicsParams.height;
            if ( v9 == 0.0 )
            {
                if ( pThing->rdthing.type == RD_THINGTYPE_MODEL )
                    v9 = pThing->rdthing.model3->insertOffset.z;
                thinga = pThing->moveSize - -0.005;
                if ( v9 <= thinga )
                    v9 = thinga;
            }
            if ( (pThing->physicsParams.physflags & (SITH_PF_FLOORSTICK|SITH_PF_WALLSTICK)) != 0 )
                v8 = v9 + v9;
            else
                v8 = v9 * 1.1;
        }
        else
        {
            v8 = pThing->moveSize - -0.005;
        }

        if ( v8 > 0.0 )
        {
            sithCollision_SearchRadiusForThings(pThing->sector, 0, &pThing->position, &direction, v8, 0.0, searchFlags | SITH_RAYCAST_ONLY_COG_THINGS | RAYCAST_800 | SITH_RAYCAST_IGNORE_ADJOINS);
            while ( 1 )
            {
                for ( i = sithCollision_NextSearchResult(); i; i = sithCollision_NextSearchResult() )
                {
                    if ( (i->hitType & SITHCOLLISION_WORLD) != 0 )
                    {
                        //printf("Attach to new surface? %x\n", i->surface->field_0);
                        sithThing_AttachToSurface(pThing, i->surface, a3);
                        sithCollision_SearchClose();
                        return;
                    }
                    if ( (i->hitType & SITHCOLLISION_THING) != 0 )
                    {
                        v11 = i->receiver;
                        if ( v11 != pThing )
                        {
                            v12 = i->face;
                            if ( !v12 || !i->sender )
                            {
                                sithCollision_SearchClose();
                                return;
                            }
                            
                            // Track thing that can move
                            if ( (searchFlags & SITH_RAYCAST_IGNORE_FLOOR) == 0
                              || (rdMatrix_TransformVector34(&a1, &v12->normal, &v11->lookOrientation), rdVector_Dot3(&a1, &rdroid_zVector3) >= 0.6) )
                            {
                                sithThing_LandThing(pThing, v11, i->face, i->sender->vertices, a3);
                                sithCollision_SearchClose();
                                return;
                            }
                        }
                    }
                }
                sithCollision_SearchClose();
                if ( range != 0.0 )
                    break;

                if ( pThing->type != SITH_THING_ACTOR && pThing->type != SITH_THING_PLAYER )
                    break;
                if ( pThing->moveSize == 0.0 )
                    break;
                range = pThing->moveSize;
                sithCollision_SearchRadiusForThings(pThing->sector, 0, &pThing->position, &direction, v8, range, searchFlags | SITH_RAYCAST_ONLY_COG_THINGS | RAYCAST_800 | SITH_RAYCAST_IGNORE_ADJOINS);
            }
        }
        if ( pThing->attach_flags )
            sithThing_DetachThing(pThing);
    }
}

//#define QUAKE_PLAYER
#ifdef QUAKE_PLAYER
// Quake style velocity normalization
void sithPhysics_NormalizeVelocity(rdVector3* currentVel, rdVector3* targetVel, flex_t deltaTime)
{
	flex_t targetSpeed = rdVector_Normalize3Acc(targetVel);
	flex_t currentSpeed = rdVector_Dot3(currentVel, targetVel);
	flex_t addedSpeed = targetSpeed - currentSpeed;
	if (addedSpeed <= 0)
	{
		rdVector_Zero3(targetVel);
	}
	else
	{
		flex_t accelspeed = deltaTime * targetSpeed;
		if (accelspeed > addedSpeed)
			accelspeed = addedSpeed;

		rdVector_Scale3Acc(targetVel, accelspeed);
	}
}

// Quake style acceleration normalization
flex_t sithPhysics_GetThrustAdjustment(sithThing* pThing)
{
	rdVector3 thrust = pThing->physicsParams.acceleration;
	if (pThing->attach_flags & (SITH_ATTACH_THINGSURFACE | SITH_ATTACH_WORLDSURFACE))
		thrust.z = 0.0f;

	flex_t dx = stdMath_Fabs(thrust.x);
	flex_t dy = stdMath_Fabs(thrust.y);
	flex_t dz = stdMath_Fabs(thrust.z);
	flex_t maxThrust = fmax(fmax(x, y), z);
	if (!maxThrust)
		return 0.0f;

	return 1.5 * maxThrust / rdVector_Len3(&thrust);
}
#endif

// Inlined func
void sithPhysics_ThingTick(sithThing *pThing, flex_t deltaSecs)
{
    if (!pThing->sector)
        return;

    rdVector_Zero3(&pThing->physicsParams.velocityMaybe);
    rdVector_Zero3(&pThing->physicsParams.addedVelocity);

    if ((pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER) 
        && (pThing->actorParams.typeflags & SITH_AF_COMBO_FREEZE))
    {
        rdVector_Zero3(&pThing->physicsParams.acceleration);
    }


#ifdef PUPPET_PHYSICS
	 // the mass to unit ratio is really high so scale the radius to a consistent unit
	flex_t adjustedSize = pThing->moveSize * 2.0f;
	pThing->physicsParams.inertia = (2.0 / 5.0) * pThing->physicsParams.mass * adjustedSize * adjustedSize;
	pThing->physicsParams.inertia = fmax(pThing->physicsParams.inertia, 0.0001f);
#endif
    if (pThing->attach_flags & (SITH_ATTACH_THINGSURFACE | SITH_ATTACH_WORLDSURFACE))
    {
        sithPhysics_ThingPhysAttached(pThing, deltaSecs);
    }
    else if (pThing->sector->flags & SITH_SECTOR_UNDERWATER)
    {
        sithPhysics_ThingPhysUnderwater(pThing, deltaSecs);
    }
#ifdef QOL_IMPROVEMENTS
    else if ( pThing->type == SITH_THING_PLAYER && (jkPlayer_bUseOldPlayerPhysics || sithNet_isMulti))
    {
#ifdef FIXED_TIMESTEP_PHYS
        if ((NEEDS_STEPPED_PHYS) && !jkPlayer_bUseOldPlayerPhysics) {
            // time stepping is handled elsewhere
            sithPhysics_ThingPhysGeneral(pThing, deltaSecs);
        }
        else
        {
            sithPhysics_ThingPhysPlayer(pThing, deltaSecs);
        }
#else
        sithPhysics_ThingPhysPlayer(pThing, deltaSecs);
#endif
    }
#else
    else if ( pThing->type == SITH_THING_PLAYER )
    {
        sithPhysics_ThingPhysPlayer(pThing, deltaSecs);
    }
#endif
    else
    {
        sithPhysics_ThingPhysGeneral(pThing, deltaSecs);
    }
}

#ifdef PUPPET_PHYSICS
// thing: thing to apply force to
// contact point: the position on the thing (usually a point on its collide sphere) to apply the force
// impulse: the impulse force to apply to the thing to cause rotation
void sithPhysics_ThingApplyRotForce(sithThing* pThing, const rdVector3* contactPoint, const rdVector3* impulse)
{   
	// Added: noclip
	if (pThing == sithPlayer_pLocalPlayerThing && (g_debugmodeFlags & DEBUGFLAG_NOCLIP))
		return;

	if (pThing->moveType != SITH_MT_PHYSICS || pThing->physicsParams.mass <= 0.0)
		return;

	rdVector3 leverArm;
	rdVector_Sub3(&leverArm, contactPoint, &pThing->position);
	rdVector_ClipPrecision3(&leverArm);
	if (rdVector_IsZero3(&leverArm))
		return;

	rdVector3 torque;
	rdVector_Cross3(&torque, &leverArm, impulse);

	rdVector3 angAccel;
	rdVector_InvScale3(&angAccel, &torque, pThing->physicsParams.inertia);
	rdVector_ClipPrecision3(&angAccel);
	if (rdVector_IsZero3(&angAccel))
		return;

	rdVector_Add3Acc(&pThing->physicsParams.rotVel, &angAccel);

	sithPhysics_ThingWake(pThing);
	if (pThing->constraintRoot)
	{
		if (rdVector_Len3(&angAccel) > 0.1)
			sithPhysics_ThingWake(pThing->constraintRoot);
	}
}

#endif

void sithPhysics_ThingApplyForce(sithThing *pThing, rdVector3 *forceVec)
{
    // Added: noclip
    if (pThing == sithPlayer_pLocalPlayerThing && (g_debugmodeFlags & DEBUGFLAG_NOCLIP)) {
        return;
    }

    if ( pThing->moveType == SITH_MT_PHYSICS && pThing->physicsParams.mass > 0.0 )
    {
        flex_t invMass = 1.0 / pThing->physicsParams.mass;

		if ( forceVec->z * invMass > 0.5 ) // TODO verify
            sithThing_DetachThing(pThing);

        rdVector_MultAcc3(&pThing->physicsParams.vel, forceVec, invMass);
        pThing->physicsParams.physflags |= SITH_PF_HAS_FORCE;

#ifdef PUPPET_PHYSICS
		sithPhysics_ThingWake(pThing);
		if(pThing->constraintRoot)
		{
			if (rdVector_Len3(forceVec) * invMass > 0.1)
				sithPhysics_ThingWake(pThing->constraintRoot);
		}
#endif
    }
}

void sithPhysics_ThingSetLook(sithThing *pThing, const rdVector3 *lookat, flex_t a3)
{
    flex_d_t v4; // st7
    flex_d_t v20; // st7

	// Added
	rdVector3 look = *lookat;
#ifdef PUPPET_PHYSICS
	// hack, rotate bodies
	if (pThing->type == SITH_THING_CORPSE && !rdVector_IsZero3(&pThing->actorParams.eyePYR))
		rdVector_Rotate3Acc(&look, &pThing->actorParams.eyePYR);
#endif
    v4 = stdMath_ClipPrecision(1.0 - rdVector_Dot3(&pThing->lookOrientation.uvec, &look));
    if ( v4 == 0.0 )
    {
        pThing->physicsParams.physflags |= SITH_PF_ATTACHED;
    }
    else if ( a3 == 0.0 )
    {
        // TODO: rdMatrix? Or are they just manually doing basis vectors?
        pThing->lookOrientation.uvec.x = look.x;
        pThing->lookOrientation.uvec.y = look.y;
        pThing->lookOrientation.uvec.z = look.z;
        pThing->lookOrientation.rvec.x = (pThing->lookOrientation.lvec.y * pThing->lookOrientation.uvec.z) - (pThing->lookOrientation.lvec.z * pThing->lookOrientation.uvec.y);
        pThing->lookOrientation.rvec.y = (pThing->lookOrientation.lvec.z * pThing->lookOrientation.uvec.x) - (pThing->lookOrientation.lvec.x * pThing->lookOrientation.uvec.z);
        pThing->lookOrientation.rvec.z = (pThing->lookOrientation.lvec.x * pThing->lookOrientation.uvec.y) - (pThing->lookOrientation.lvec.y * pThing->lookOrientation.uvec.x);
        rdVector_Normalize3Acc(&pThing->lookOrientation.rvec);
        pThing->lookOrientation.lvec.x = (pThing->lookOrientation.rvec.z * pThing->lookOrientation.uvec.y) - (pThing->lookOrientation.rvec.y * pThing->lookOrientation.uvec.z);
        pThing->lookOrientation.lvec.y = (pThing->lookOrientation.rvec.x * pThing->lookOrientation.uvec.z) - (pThing->lookOrientation.rvec.z * pThing->lookOrientation.uvec.x);
        pThing->lookOrientation.lvec.z = (pThing->lookOrientation.rvec.y * pThing->lookOrientation.uvec.x) - (pThing->lookOrientation.rvec.x * pThing->lookOrientation.uvec.y);
        

        pThing->physicsParams.physflags |= SITH_PF_ATTACHED;
    }
    else
    {
        // TODO: rdMatrix? Or are they just manually doing basis vectors?
        v20 = a3 * 10.0;
        pThing->lookOrientation.uvec.x = look.x * v20 + pThing->lookOrientation.uvec.x;
        pThing->lookOrientation.uvec.y = look.z * v20 + pThing->lookOrientation.uvec.y;
        pThing->lookOrientation.uvec.z = look.y * v20 + pThing->lookOrientation.uvec.z;
        rdVector_Normalize3Acc(&pThing->lookOrientation.uvec);
        pThing->lookOrientation.lvec.x = (pThing->lookOrientation.rvec.z * pThing->lookOrientation.uvec.y) - (pThing->lookOrientation.rvec.y * pThing->lookOrientation.uvec.z);
        pThing->lookOrientation.lvec.y = (pThing->lookOrientation.rvec.x * pThing->lookOrientation.uvec.z) - pThing->lookOrientation.rvec.z * pThing->lookOrientation.uvec.x;
        pThing->lookOrientation.lvec.z = (pThing->lookOrientation.rvec.y * pThing->lookOrientation.uvec.x) - (pThing->lookOrientation.rvec.x * pThing->lookOrientation.uvec.y);
        rdVector_Normalize3Acc(&pThing->lookOrientation.lvec);
        pThing->lookOrientation.rvec.x = (pThing->lookOrientation.lvec.y * pThing->lookOrientation.uvec.z) - (pThing->lookOrientation.lvec.z * pThing->lookOrientation.uvec.y);
        pThing->lookOrientation.rvec.y = (pThing->lookOrientation.lvec.z * pThing->lookOrientation.uvec.x) - (pThing->lookOrientation.lvec.x * pThing->lookOrientation.uvec.z);
        pThing->lookOrientation.rvec.z = (pThing->lookOrientation.lvec.x * pThing->lookOrientation.uvec.y) - (pThing->lookOrientation.lvec.y * pThing->lookOrientation.uvec.x);
    }

#ifdef PUPPET_PHYSICS
	// hack, rotate bodies
	//if (pThing->type == SITH_THING_CORPSE && !rdVector_IsZero3(&pThing->actorParams.eyePYR))
		//rdMatrix_PreRotate34(&pThing->lookOrientation, &pThing->actorParams.eyePYR);
#endif
}

void sithPhysics_ApplyDrag(rdVector3 *vec, flex_t drag, flex_t mag, flex_t deltaSecs)
{
    if (mag == 0.0 || rdVector_Len3(vec) >= mag)
    {
        if (drag != 0.0)
        {
            flex_d_t scaled = deltaSecs * drag;
            if (scaled > 1.0)
                scaled = 1.0;

            rdVector_MultAcc3(vec, vec, -scaled);
            
            rdMath_ClampVector(vec, 0.00001);
        }
    }
    else
    {
        rdVector_Zero3(vec);
    }
}

int sithPhysics_LoadThingParams(stdConffileArg *arg, sithThing *pThing, int param)
{
    flex32_t tmp;
    int tmpInt;
    flex32_t velx, vely, velz;

    switch ( param )
    {
        case THINGPARAM_SURFDRAG:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 )
                return 0;
            pThing->physicsParams.surfaceDrag = tmp;
            return 1;
        case THINGPARAM_AIRDRAG:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 )
                return 0;
            pThing->physicsParams.airDrag = tmp;
            return 1;
        case THINGPARAM_STATICDRAG:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 )
                return 0;
            pThing->physicsParams.staticDrag = tmp;
            return 1;
        case THINGPARAM_MASS:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 )
                return 0;
            pThing->physicsParams.mass = tmp;
            return 1;
        case THINGPARAM_HEIGHT:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 )
                return 0;
            pThing->physicsParams.height = tmp;
            return 1;
        case THINGPARAM_PHYSFLAGS:
            if ( _sscanf(arg->value, "%x", &tmpInt) != 1 )
                return 0;
            pThing->physicsParams.physflags = tmpInt;
            return 1;
        case THINGPARAM_MAXROTVEL:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 || pThing->moveType != SITH_MT_PHYSICS )
                return 0;
            pThing->physicsParams.maxRotVel = tmp;
            return 1;
        case THINGPARAM_MAXVEL:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 || pThing->moveType != SITH_MT_PHYSICS )
                return 0;
            pThing->physicsParams.maxVel = tmp;
            return 1;
        case THINGPARAM_VEL:
            if (_sscanf(
                      arg->value,
                      "(%f/%f/%f)",
                      &velx, &vely, &velz) != 3)
                return 0;
            pThing->physicsParams.vel.x = velx;
            pThing->physicsParams.vel.y = vely;
            pThing->physicsParams.vel.z = velz;
            return 1;
        case THINGPARAM_ANGVEL:
            if (_sscanf(
                      arg->value,
                      "(%f/%f/%f)",
                      &velx, &vely, &velz) != 3)
                return 0;
            pThing->physicsParams.angVel.x = velx;
            pThing->physicsParams.angVel.y = vely;
            pThing->physicsParams.angVel.z = velz;
            return 1;
        case THINGPARAM_ORIENTSPEED:
            tmp = _atof(arg->value);
            if ( tmp < 0.0 || pThing->moveType != SITH_MT_PHYSICS )
                return 0;
            pThing->physicsParams.orientSpeed = tmp;
            return 1;
        case THINGPARAM_BUOYANCY:
            tmp = _atof(arg->value);
            pThing->physicsParams.buoyancy = tmp;
            return 1;
        default:
            return 0;
    }
}

void sithPhysics_ThingStop(sithThing *pThing)
{
    rdVector_Zero3(&pThing->physicsParams.vel);
    rdVector_Zero3(&pThing->physicsParams.angVel);
    rdVector_Zero3(&pThing->physicsParams.field_1F8);
    rdVector_Zero3(&pThing->physicsParams.acceleration);
    rdVector_Zero3(&pThing->physicsParams.velocityMaybe);
    rdVector_Zero3(&pThing->field_268);
#ifdef PUPPET_PHYSICS
	rdVector_Zero3(&pThing->physicsParams.rotVel);
#endif
}

flex_t sithPhysics_ThingGetInsertOffsetZ(sithThing *pThing)
{
    flex_d_t result; // st7
    flex_t v2; // [esp+4h] [ebp+4h]

    result = pThing->physicsParams.height;
    if ( result == 0.0 )
    {
        if ( pThing->rdthing.type == RD_THINGTYPE_MODEL )
            result = pThing->rdthing.model3->insertOffset.z;
        v2 = pThing->moveSize - -0.005;
        if ( result <= v2 )
            result = v2;
    }
    return result;
}

// MOTS altered
void sithPhysics_ThingPhysGeneral(sithThing *pThing, flex_t deltaSeconds)
{
    rdVector3 a1a;
    rdVector3 a3;
    rdMatrix34 a;
    int bOverrideIdk = 0;
    flex_t zOverride = 0.0;

    rdVector_Zero3(&pThing->physicsParams.addedVelocity);
    rdVector_Zero3(&a1a);

    if (pThing->physicsParams.physflags & SITH_PF_ANGTHRUST)
    {
        if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
        {
            sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
        }

        rdVector_MultAcc3(&pThing->physicsParams.angVel, &pThing->physicsParams.field_1F8, deltaSeconds);
        
        rdMath_ClampVectorRange(&pThing->physicsParams.angVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);
        rdMath_ClampVector(&pThing->physicsParams.angVel, 0.00001);
    }
#ifdef PUPPET_PHYSICS
	else if (pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
	{
		if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
		{
			sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
		}
	}
#endif

    if (rdVector_IsZero3(&pThing->physicsParams.angVel))
    {
        rdVector_Zero3(&a3);
    }
    else
    {
        rdVector_Scale3(&a3, &pThing->physicsParams.angVel, deltaSeconds);
    }

    // MOTS added: weapon tracking?
#ifdef JKM_PARAMS
    if (pThing->type == SITH_THING_WEAPON && pThing->weaponParams.pTargetThing && pThing->weaponParams.field_38 != 0.0) {
        rdVector3 tmp;
        rdMatrix34 local_60;
        rdVector3 local_6c, local_78;

        rdVector_Sub3(&tmp, &pThing->weaponParams.pTargetThing->position, &pThing->position);
        flex_t fVar3 = deltaSeconds * pThing->weaponParams.field_38;

        if (-0.03 <= tmp.z) {
            if (tmp.z > 0.03) {
                zOverride = 1.0;
            }
        }
        else {
            zOverride = -1.0;
        }
        rdVector_Normalize3Acc(&tmp);
        rdMatrix_BuildFromLook34(&local_60,&tmp);
        rdMatrix_ExtractAngles34(&local_60,&local_6c);
        rdMatrix_Copy34(&local_60, &pThing->lookOrientation);
        rdMatrix_ExtractAngles34(&local_60,&local_78);
        tmp.y = local_6c.y - local_78.y;
        tmp.x = -local_78.x;
        tmp.z = -local_78.z;
        if (tmp.y > 180.0) {
            tmp.y = tmp.y - 360.0;
        }
        else if (tmp.y < -180.0) {
            tmp.y = tmp.y - -360.0;
        }
        flex_t fVar6 = tmp.y;
        if (tmp.y < 0.0) {
            fVar6 = -tmp.y;
        }
        if (fVar6 > fVar3) {
            fVar6 = tmp.y;
            if (tmp.y < 0.0) {
                fVar6 = -tmp.y;
            }
            tmp.y = (fVar3 / fVar6) * tmp.y;
        }
        bOverrideIdk = 1;
        rdVector_Copy3(&a3, &tmp);
    }
#endif

    if (!rdVector_IsZero3(&a3))
    {
        rdMatrix_BuildRotate34(&a, &a3);
        sithCollision_sub_4E7670(pThing, &a);

        if (pThing->physicsParams.physflags & SITH_PF_FLY)
            rdMatrix_TransformVector34Acc(&pThing->physicsParams.vel, &a);

        if ( ((jkPlayer_currentTickIdx + (pThing->thingIdx & 0xFF)) & 7) == 0 )
            rdMatrix_Normalize34(&pThing->lookOrientation);
    }

#ifdef PUPPET_PHYSICS
	if (!rdVector_IsZero3(&pThing->physicsParams.rotVel))
	{
		sithPhysics_ApplyDrag(&pThing->physicsParams.rotVel, pThing->physicsParams.airDrag, 0.0001, deltaSeconds);
		//rdMath_ClampVectorRange(&pThing->physicsParams.rotVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);

		rdMatrix34 invMat;
		rdMatrix_InvertOrtho34(&invMat, &pThing->lookOrientation);

		rdVector3 localRotVel;
		rdMatrix_TransformVector34(&localRotVel, &pThing->physicsParams.rotVel, &invMat);

		rdVector3 axis;
		flex_t angle = rdVector_Normalize3(&axis, &localRotVel) * deltaSeconds * (180.0f / M_PI);
		rdMatrix_BuildFromAxisAngle34(&a, &axis, angle);
		sithCollision_sub_4E7670(pThing, &a);
		//if (((bShowInvisibleThings + (pThing->thingIdx & 0xFF)) & 7) == 0)
			//rdMatrix_Normalize34(&pThing->lookOrientation);
	}
#endif


    if ( pThing->physicsParams.airDrag != 0.0 )
        sithPhysics_ApplyDrag(&pThing->physicsParams.vel, pThing->physicsParams.airDrag, 0.0, deltaSeconds);

    if (pThing->physicsParams.physflags & SITH_PF_USESTHRUST)
    {
        if (!(pThing->physicsParams.physflags & SITH_PF_FLY))
        {
            rdVector_Scale3Acc(&pThing->physicsParams.acceleration, 0.3);
        }
        rdVector_Scale3(&a1a, &pThing->physicsParams.acceleration, deltaSeconds);
        rdMatrix_TransformVector34Acc(&a1a, &pThing->lookOrientation);
    }

    if (pThing->physicsParams.mass != 0.0 
        && (pThing->sector->flags & SITH_SECTOR_HASTHRUST) 
        && !(pThing->physicsParams.physflags & SITH_PF_NOTHRUST))
    {
        rdVector_MultAcc3(&a1a, &pThing->sector->thrust, deltaSeconds);
    }

    if (pThing->physicsParams.mass != 0.0 
        && pThing->physicsParams.physflags & SITH_PF_USEGRAVITY
        && !(pThing->sector->flags & SITH_SECTOR_NOGRAVITY))
    {
        flex_t gravity = sithWorld_pCurrentWorld->worldGravity * deltaSeconds;
        if ( (pThing->physicsParams.physflags & SITH_PF_PARTIALGRAVITY) != 0 )
            gravity *= 0.5;
        a1a.z = a1a.z - gravity;
        pThing->physicsParams.addedVelocity.z = -gravity;
    }

    rdVector_Add3Acc(&pThing->physicsParams.vel, &a1a);
#ifdef JKM_PARAMS
    if (bOverrideIdk) {
        pThing->physicsParams.vel.z = zOverride;
    }
#endif
    rdMath_ClampVector(&pThing->physicsParams.vel, 0.00001);

    if (!rdVector_IsZero3(&pThing->physicsParams.vel))
    {
        rdVector_Scale3(&pThing->physicsParams.velocityMaybe, &pThing->physicsParams.vel, deltaSeconds);
    }
}

// MOTS altered
void sithPhysics_ThingPhysPlayer(sithThing *player, flex_t deltaSeconds)
{
    rdMatrix34 a;
    rdVector3 a3;
    rdVector3 a1a;
    //int bOverrideIdk = 0; // Remove compiler warns
    flex_t zOverride = 0.0;

    rdVector_Zero3(&player->physicsParams.addedVelocity);
    if (player->physicsParams.physflags & SITH_PF_ANGTHRUST)
    {
        if (!rdVector_IsZero3(&player->physicsParams.angVel))
        {
            sithPhysics_ApplyDrag(&player->physicsParams.angVel, player->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
        }

        rdVector_MultAcc3(&player->physicsParams.angVel, &player->physicsParams.field_1F8, deltaSeconds);

        rdMath_ClampVectorRange(&player->physicsParams.angVel, -player->physicsParams.maxRotVel, player->physicsParams.maxRotVel);
        rdMath_ClampVector(&player->physicsParams.angVel, 0.00001);
    }
#ifdef PUPPET_PHYSICS
	else if (player->physicsParams.physflags & SITH_PF_ANGIMPULSE)
	{
		if (!rdVector_IsZero3(&player->physicsParams.angVel))
		{
			sithPhysics_ApplyDrag(&player->physicsParams.angVel, player->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
		}
	}
#endif

    if (rdVector_IsZero3(&player->physicsParams.angVel))
    {
        rdVector_Zero3(&a3);
    }
    else
    {
        rdVector_Scale3(&a3, &player->physicsParams.angVel, deltaSeconds);
    }

// MOTS added: weapon tracking? why is this here lol
#ifdef JKM_PARAMS
    if (player->type == SITH_THING_WEAPON && player->weaponParams.pTargetThing && player->weaponParams.field_38 != 0.0) {
        rdVector3 tmp;
        rdMatrix34 local_60;
        rdVector3 local_6c, local_78;

        rdVector_Sub3(&tmp, &player->weaponParams.pTargetThing->position, &player->position);
        flex_t fVar3 = deltaSeconds * player->weaponParams.field_38;

        rdVector_Normalize3Acc(&tmp);
        rdMatrix_BuildFromLook34(&local_60,&tmp);
        rdMatrix_ExtractAngles34(&local_60,&local_6c);
        rdMatrix_Copy34(&local_60, &player->lookOrientation);
        rdMatrix_ExtractAngles34(&local_60,&local_78);
        tmp.y = local_6c.y - local_78.y;
        tmp.x = -local_78.x;
        tmp.z = -local_78.z;
        if (tmp.y > 180.0) {
            tmp.y = tmp.y - 360.0;
        }
        else if (tmp.y < -180.0) {
            tmp.y = tmp.y - -360.0;
        }
        flex_t fVar6 = tmp.y;
        if (tmp.y < 0.0) {
            fVar6 = -tmp.y;
        }
        if (fVar6 > fVar3) {
            fVar6 = tmp.y;
            if (tmp.y < 0.0) {
                fVar6 = -tmp.y;
            }
            tmp.y = (fVar3 / fVar6) * tmp.y;
        }
        //bOverrideIdk = 1; // Remove compiler warns
        rdVector_Copy3(&a3, &tmp);
    }
#endif

    if (!rdVector_IsZero3(&a3))
    {
        rdMatrix_BuildRotate34(&a, &a3);
        sithCollision_sub_4E7670(player, &a);

        if (player->physicsParams.physflags & SITH_PF_FLY)
            rdMatrix_TransformVector34Acc(&player->physicsParams.vel, &a);

        if ( ((jkPlayer_currentTickIdx + (player->thingIdx & 0xFF)) & 7) == 0 )
            rdMatrix_Normalize34(&player->lookOrientation);
    }

#ifdef PUPPET_PHYSICS
	if (!rdVector_IsZero3(&player->physicsParams.rotVel))
	{
		sithPhysics_ApplyDrag(&player->physicsParams.rotVel, player->physicsParams.airDrag, 0.0001, deltaSeconds);
		//rdMath_ClampVectorRange(&pThing->physicsParams.rotVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);

		rdMatrix34 invMat;
		rdMatrix_InvertOrtho34(&invMat, &player->lookOrientation);

		rdVector3 localRotVel;
		rdMatrix_TransformVector34(&localRotVel, &player->physicsParams.rotVel, &invMat);

		rdVector3 axis;
		flex_t angle = rdVector_Normalize3(&axis, &localRotVel) * deltaSeconds * (180.0f / M_PI);
		rdMatrix_BuildFromAxisAngle34(&a, &axis, angle);
		sithCollision_sub_4E7670(player, &a);
		//if (((bShowInvisibleThings + (pThing->thingIdx & 0xFF)) & 7) == 0)
			//rdMatrix_Normalize34(&pThing->lookOrientation);
	}
#endif

    if (!(player->physicsParams.physflags & SITH_PF_FLY))
    {
        rdVector_Scale3Acc(&player->physicsParams.acceleration, 0.3);
    }

    // I think all of this is specifically for multiplayer, so that player things
    // sync better between clients.
    flex_t rolloverCombine = deltaSeconds + player->physicsParams.physicsRolloverFrames;

    flex_t framesToApply = rolloverCombine * OLDSTEP_TARGET_FPS; // get number of 50FPS steps passed
    player->physicsParams.physicsRolloverFrames = rolloverCombine - (flex_d_t)(unsigned int)(int)framesToApply * OLDSTEP_DELTA_50FPS;

    for (int i = (int)framesToApply; i > 0; i--)
    {
        rdVector_Zero3(&a1a);
        if ( player->physicsParams.airDrag != 0.0 )
        {
            sithPhysics_ApplyDrag(&player->physicsParams.vel, player->physicsParams.airDrag, 0.0, OLDSTEP_DELTA_50FPS);
        }

        if (player->physicsParams.physflags & SITH_PF_USESTHRUST)
        {
            rdVector_Scale3(&a1a, &player->physicsParams.acceleration, OLDSTEP_DELTA_50FPS);
            rdMatrix_TransformVector34Acc(&a1a, &player->lookOrientation);
        }

        if ( player->physicsParams.mass != 0.0 )
        {
            if ((player->sector->flags & SITH_SECTOR_HASTHRUST)
                && !(player->physicsParams.physflags & SITH_PF_NOTHRUST))
            {
                rdVector_MultAcc3(&a1a, &player->sector->thrust, OLDSTEP_DELTA_50FPS);
            }
        }

        if ( player->physicsParams.mass != 0.0 
             && (player->physicsParams.physflags & SITH_PF_USEGRAVITY) 
             && !(player->sector->flags & SITH_SECTOR_NOGRAVITY) )
        {
            flex_t gravity = sithWorld_pCurrentWorld->worldGravity * OLDSTEP_DELTA_50FPS;
            if ( (player->physicsParams.physflags & SITH_PF_PARTIALGRAVITY) != 0 )
                gravity = gravity * 0.5;
            a1a.z = a1a.z - gravity;
            player->physicsParams.addedVelocity.z = -gravity;
        }
        rdVector_Add3Acc(&player->physicsParams.vel, &a1a);
        rdVector_MultAcc3(&player->physicsParams.velocityMaybe, &player->physicsParams.vel, OLDSTEP_DELTA_50FPS);
    }
}

// MOTS altered
void sithPhysics_ThingPhysUnderwater(sithThing *pThing, flex_t deltaSeconds)
{
    flex_d_t v35; // st6
    flex_d_t v51; // st7
    rdVector3 a1a; // [esp+24h] [ebp-48h] BYREF
    rdVector3 a3; // [esp+30h] [ebp-3Ch] BYREF
    rdMatrix34 tmpMat; // [esp+3Ch] [ebp-30h] BYREF

    rdVector_Zero3(&a1a);
    rdVector_Zero3(&pThing->physicsParams.addedVelocity);
    if ( (pThing->physicsParams.physflags & SITH_PF_ANGTHRUST) != 0 )
    {
        if ( !rdVector_IsZero3(&pThing->physicsParams.angVel) )
        {
            sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
        }
        rdVector_MultAcc3(&pThing->physicsParams.angVel, &pThing->physicsParams.field_1F8, deltaSeconds);
        rdVector_ClampValue3(&pThing->physicsParams.angVel, pThing->physicsParams.maxRotVel);
        rdVector_ClipPrecision3(&pThing->physicsParams.angVel);
    }
#ifdef PUPPET_PHYSICS
	else if(pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
	{
		if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
		{
			sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
		}
	}
#endif

    if ( rdVector_IsZero3(&pThing->physicsParams.angVel) )
    {
        rdVector_Zero3(&a3);
    }
    else
    {
        rdVector_Scale3(&a3, &pThing->physicsParams.angVel, deltaSeconds);
    }

    if (!rdVector_IsZero3(&a3))
    {
        rdMatrix_BuildRotate34(&tmpMat, &a3);
        sithCollision_sub_4E7670(pThing, &tmpMat);
        if ( (((jkPlayer_currentTickIdx & 0xFF) + (pThing->thingIdx & 0xFF)) & 7) == 0 )
            rdMatrix_Normalize34(&pThing->lookOrientation);
    }
#ifdef PUPPET_PHYSICS
	if (!rdVector_IsZero3(&pThing->physicsParams.rotVel))
	{
		sithPhysics_ApplyDrag(&pThing->physicsParams.rotVel, pThing->physicsParams.airDrag, 0.0001, deltaSeconds);
		//rdMath_ClampVectorRange(&pThing->physicsParams.rotVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);

		rdMatrix34 invMat;
		rdMatrix_InvertOrtho34(&invMat, &pThing->lookOrientation);

		rdVector3 localRotVel;
		rdMatrix_TransformVector34(&localRotVel, &pThing->physicsParams.rotVel, &invMat);

		rdVector3 axis;
		flex_t angle = rdVector_Normalize3(&axis, &localRotVel) * deltaSeconds * (180.0f / M_PI);
		rdMatrix_BuildFromAxisAngle34(&tmpMat, &axis, angle);
		sithCollision_sub_4E7670(pThing, &tmpMat);
		//if (((bShowInvisibleThings + (pThing->thingIdx & 0xFF)) & 7) == 0)
			//rdMatrix_Normalize34(&pThing->lookOrientation);
	}
#endif
    if ( pThing->physicsParams.airDrag != 0.0 )
    {
        sithPhysics_ApplyDrag(&pThing->physicsParams.vel, pThing->physicsParams.airDrag * 4.0, 0.0, deltaSeconds);
    }
    if ( (pThing->physicsParams.physflags & SITH_PF_USESTHRUST) != 0 )
    {
        rdVector_Scale3Acc(&pThing->physicsParams.acceleration, 0.6);
        rdVector_Scale3(&a1a, &pThing->physicsParams.acceleration, deltaSeconds);
        rdMatrix_TransformVector34Acc(&a1a, &pThing->lookOrientation);
    }
    if ( pThing->physicsParams.mass != 0.0 && pThing->sector && (pThing->sector->flags & SITH_SECTOR_HASTHRUST) && !(pThing->physicsParams.physflags & SITH_PF_NOTHRUST) )
    {
        rdVector_MultAcc3(&a1a, &pThing->sector->thrust, deltaSeconds);
    }

    if ( ((pThing->physicsParams.physflags & SITH_PF_WATERSURFACE) == 0 || (pThing->thingflags & SITH_TF_DEAD) != 0) && (pThing->physicsParams.physflags & SITH_PF_USEGRAVITY) != 0 )
    {
        v35 = sithWorld_pCurrentWorld->worldGravity * deltaSeconds * pThing->physicsParams.buoyancy;
        a1a.z -= v35;
        pThing->physicsParams.addedVelocity.z -= v35;
    }
    rdVector_Add3Acc(&pThing->physicsParams.vel, &a1a);

    rdVector_ClipPrecision3(&pThing->physicsParams.vel);
    if ( !rdVector_IsZero3(&pThing->physicsParams.vel) )
    {
        rdVector_Scale3(&pThing->physicsParams.velocityMaybe, &pThing->physicsParams.vel, deltaSeconds);
    }
    if ( (pThing->physicsParams.physflags & SITH_PF_WATERSURFACE) != 0 && pThing->physicsParams.acceleration.z >= 0.0 )
    {
        v51 = pThing->field_48 - 0.01;
        if ( pThing->physicsParams.velocityMaybe.z > 0.0 && pThing->physicsParams.velocityMaybe.z < (flex_d_t)deltaSeconds * 0.2 ) // verify first
            pThing->physicsParams.velocityMaybe.z = 0.0;
        if ( v51 > 0.0 )
        {
            if ( v51 >= deltaSeconds * 0.2 )
                v51 = deltaSeconds * 0.2;
            rdVector_MultAcc3(&pThing->physicsParams.velocityMaybe, &rdroid_zVector3, v51);
        }
    }
}

// MOTS altered
void sithPhysics_ThingPhysAttached(sithThing *pThing, flex_t deltaSeconds)
{   
    flex_t a2a; // [esp+0h] [ebp-94h]
    flex_t v144; // [esp+4h] [ebp-90h]
    flex_t possibly_undef_2; // [esp+1Ch] [ebp-78h]
    flex_t new_z; // [esp+20h] [ebp-74h]
    flex_t new_x; // [esp+24h] [ebp-70h]
    flex_t v158; // [esp+28h] [ebp-6Ch]
    flex_t possibly_undef_1; // [esp+2Ch] [ebp-68h]
    flex_t new_y; // [esp+30h] [ebp-64h]
    flex_t new_ya; // [esp+30h] [ebp-64h]
    rdVector3 vel_change; // [esp+34h] [ebp-60h] BYREF
    rdVector3 attachedNormal; // [esp+40h] [ebp-54h] BYREF
    rdVector3 out; // [esp+4Ch] [ebp-48h] BYREF
    rdVector3 a3; // [esp+58h] [ebp-3Ch] BYREF
    rdMatrix34 a; // [esp+64h] [ebp-30h] BYREF
    int bOverrideIdk = 0;
    flex_t zOverride = 0.0;

    possibly_undef_1 = 0.0;
    possibly_undef_2 = 0.0;


#ifdef DYNAMIC_POV
	pThing->physicsParams.povOffset = 0;
#endif

    rdVector_Zero3(&vel_change);
    v158 = 1.0;
    pThing->physicsParams.physflags &= ~SITH_PF_200000;
    if ( (pThing->attach_flags & SITH_ATTACH_WORLDSURFACE) != 0 )
    {
        attachedNormal = pThing->attachedSufaceInfo->face.normal;
        possibly_undef_1 = rdMath_DistancePointToPlane(&pThing->position, &attachedNormal, &pThing->field_38);
        if ( (pThing->attachedSurface->surfaceFlags & (SITH_SURFACE_ICY|SITH_SURFACE_VERYICY)) != 0 )
        {
            if ( (pThing->attachedSurface->surfaceFlags & SITH_SURFACE_VERYICY) != 0 )
                possibly_undef_2 = 0.1;
            else
                possibly_undef_2 = 0.3;
        }
        else
        {
            possibly_undef_2 = 1.0;
        }
    }
    else if ( (pThing->attach_flags & SITH_ATTACH_THINGSURFACE) != 0 )
    {
        rdMatrix_TransformVector34(&attachedNormal, &pThing->attachedSufaceInfo->face.normal, &pThing->attachedThing->lookOrientation);
        rdMatrix_TransformVector34(&a3, &pThing->field_38, &pThing->attachedThing->lookOrientation);
        possibly_undef_2 = 1.0;
        rdVector_Add3Acc(&a3, &pThing->attachedThing->position);
        possibly_undef_1 = rdMath_DistancePointToPlane(&pThing->position, &attachedNormal, &a3);
    }

    if (pThing->physicsParams.physflags & SITH_PF_NOWALLGRAVITY)
    {
        v158 = rdVector_Dot3(&attachedNormal, &rdroid_zVector3);
        if ( v158 < 1.0 )
            possibly_undef_1 = possibly_undef_1 / v158;
    }

    if (!(pThing->physicsParams.physflags & SITH_PF_ATTACHED))
    {
        if ( (pThing->physicsParams.physflags & SITH_PF_SURFACEALIGN) != 0 )
        {
            sithPhysics_ThingSetLook(pThing, &attachedNormal, pThing->physicsParams.orientSpeed * deltaSeconds);
        }
        else if ( (pThing->physicsParams.physflags & SITH_PF_NOWALLGRAVITY) != 0 )
        {
            sithPhysics_ThingSetLook(pThing, &rdroid_zVector3, pThing->physicsParams.orientSpeed * deltaSeconds);
        }
        else
        {
            pThing->physicsParams.physflags |= SITH_PF_ATTACHED;
        }
    }

    if (pThing->physicsParams.physflags & SITH_PF_ANGTHRUST)
    {
        if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
        {
            sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.surfaceDrag - -0.2, 0.0, deltaSeconds);
        }

        pThing->physicsParams.angVel.y = pThing->physicsParams.field_1F8.y * deltaSeconds + pThing->physicsParams.angVel.y;
        rdVector_ClampValue3(&pThing->physicsParams.angVel, pThing->physicsParams.maxRotVel);
        rdVector_ClipPrecision3(&pThing->physicsParams.angVel);
    }
#ifdef PUPPET_PHYSICS
	else if (pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
	{
		if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
		{
			sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
		}
	}
#endif

    if ( pThing->physicsParams.angVel.y != 0.0 )
    {
        rdVector_Scale3(&a3, &pThing->physicsParams.angVel, deltaSeconds);

// MOTS added: weapon tracking?
#ifdef JKM_PARAMS
    if (pThing->type == SITH_THING_WEAPON && pThing->weaponParams.pTargetThing && pThing->weaponParams.field_38 != 0.0) {
        rdVector3 tmp;
        rdMatrix34 local_60;
        rdVector3 local_6c, local_78;

        rdVector_Sub3(&tmp, &pThing->weaponParams.pTargetThing->position, &pThing->position);
        flex_t fVar3 = deltaSeconds * pThing->weaponParams.field_38;

        if (-0.03 <= tmp.z) {
            if (tmp.z > 0.03) {
                zOverride = 1.0;
            }
        }
        else {
            zOverride = -1.0;
        }
        rdVector_Normalize3Acc(&tmp);
        rdMatrix_BuildFromLook34(&local_60,&tmp);
        rdMatrix_ExtractAngles34(&local_60,&local_6c);
        rdMatrix_Copy34(&local_60, &pThing->lookOrientation);
        rdMatrix_ExtractAngles34(&local_60,&local_78);
        tmp.y = local_6c.y - local_78.y;
        tmp.x = -local_78.x;
        tmp.z = -local_78.z;
        if (tmp.y > 180.0) {
            tmp.y = tmp.y - 360.0;
        }
        else if (tmp.y < -180.0) {
            tmp.y = tmp.y - -360.0;
        }
        flex_t fVar6 = tmp.y;
        if (tmp.y < 0.0) {
            fVar6 = -tmp.y;
        }
        if (fVar6 > fVar3) {
            fVar6 = tmp.y;
            if (tmp.y < 0.0) {
                fVar6 = -tmp.y;
            }
            tmp.y = (fVar3 / fVar6) * tmp.y;
        }
        bOverrideIdk = 1;
        rdVector_Copy3(&a3, &tmp);
    }
#endif

        rdMatrix_BuildRotate34(&a, &a3);
        sithCollision_sub_4E7670(pThing, &a);

#ifdef PUPPET_PHYSICS
		if (!(pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE))
		{
#endif
        if ( possibly_undef_2 >= 1.0 )
        {
            rdMatrix_TransformVector34Acc(&pThing->physicsParams.vel, &a);
        }
        else
        {
            rdMatrix_TransformVector34(&out, &pThing->physicsParams.vel, &a);
            rdVector_Scale3Acc(&pThing->physicsParams.vel, 1.0 - possibly_undef_2);
            rdVector_MultAcc3(&pThing->physicsParams.vel, &out, possibly_undef_2);
        }
#ifdef PUPPET_PHYSICS
		}
#endif
        if ( (((jkPlayer_currentTickIdx & 0xFF) + (pThing->thingIdx & 0xFF)) & 7) == 0 )
            rdMatrix_Normalize34(&pThing->lookOrientation);
    }
#ifdef PUPPET_PHYSICS
	if (!rdVector_IsZero3(&pThing->physicsParams.rotVel))
	{
		sithPhysics_ApplyDrag(&pThing->physicsParams.rotVel, pThing->physicsParams.surfaceDrag, 0.0001, deltaSeconds);
		//rdMath_ClampVectorRange(&pThing->physicsParams.rotVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);

		// clip along attachment plane
		if(!pThing->constraintParent)
		{
			flex_t dotProduct = rdVector_Dot3(&pThing->physicsParams.rotVel, &pThing->attachedSufaceInfo->face.normal);
			if (possibly_undef_2 >= 1.0)
			{
				rdVector_Scale3(&pThing->physicsParams.rotVel, &pThing->attachedSufaceInfo->face.normal, dotProduct);
			}
			else
			{
				rdVector_Scale3(&out, &pThing->attachedSufaceInfo->face.normal, dotProduct);
				rdVector_Scale3Acc(&pThing->physicsParams.rotVel, 1.0 - possibly_undef_2);
				rdVector_MultAcc3(&pThing->physicsParams.rotVel, &out, possibly_undef_2);
			}
		}

		rdMatrix34 invMat;
		rdMatrix_InvertOrtho34(&invMat, &pThing->lookOrientation);

		rdVector3 localRotVel;
		rdMatrix_TransformVector34(&localRotVel, &pThing->physicsParams.rotVel, &invMat);

		rdVector3 axis;
		flex_t angle = rdVector_Normalize3(&axis, &localRotVel) * deltaSeconds * (180.0f / M_PI);
		rdMatrix_BuildFromAxisAngle34(&a, &axis, angle);
		sithCollision_sub_4E7670(pThing, &a);
		//if (((bShowInvisibleThings + (pThing->thingIdx & 0xFF)) & 7) == 0)
			//rdMatrix_Normalize34(&pThing->lookOrientation);
	}
#endif
    if ( possibly_undef_2 < 0.25 )
    {
        possibly_undef_2 = 0.25;
    }
    else if ( possibly_undef_2 > 1.0 )
    {
        possibly_undef_2 = 1.0;
    }

    if (!rdVector_IsZero3(&pThing->physicsParams.vel) && pThing->physicsParams.surfaceDrag != 0.0)
    {
        if ( (pThing->physicsParams.physflags & SITH_PF_HAS_FORCE) == 0 )
        {
            if ( rdVector_IsZero3(&pThing->physicsParams.acceleration)
              && !(pThing->sector->flags & SITH_SECTOR_HASTHRUST)
              && possibly_undef_2 > 0.8 )
            {
                a2a = pThing->physicsParams.surfaceDrag * possibly_undef_2;
                v144 = pThing->physicsParams.staticDrag * possibly_undef_2;
            }
            else
            {
                a2a = pThing->physicsParams.surfaceDrag * possibly_undef_2;
                v144 = 0.0;
            }
            sithPhysics_ApplyDrag(&pThing->physicsParams.vel, a2a, v144, deltaSeconds);
        }
        else
        {
            pThing->physicsParams.physflags &= ~SITH_PF_HAS_FORCE;
        }
    }

    if ( (pThing->physicsParams.physflags & SITH_PF_USESTHRUST) != 0
      && !rdVector_IsZero3(&pThing->physicsParams.acceleration) )
    {
#ifdef QUAKE_PLAYER
		if (pThing->type == SITH_THING_PLAYER)
		{
			flex_t v44 = possibly_undef_2;
			if ((pThing->physicsParams.physflags & SITH_PF_CROUCHING) != 0)
				v44 = 0.8;
				
			// normalize thrust
			v44 *= sithPhysics_GetThrustAdjustment(pThing);

			rdVector_Scale3(&vel_change, &pThing->physicsParams.acceleration, v44);
			rdVector_ClipPrecision3(&vel_change);
			if (!rdVector_IsZero3(&vel_change))
			{
				rdMatrix_TransformVector34Acc(&vel_change, &pThing->lookOrientation);

				// normalize the acceleration velocity
				sithPhysics_NormalizeVelocity(&pThing->physicsParams.vel, &vel_change, deltaSeconds);
			}
		}
		else
#endif
		{
			flex_t v44 = possibly_undef_2 * deltaSeconds;
			if ((pThing->physicsParams.physflags & SITH_PF_CROUCHING) != 0)
				v44 = deltaSeconds * 0.8;

			rdVector_Scale3(&vel_change, &pThing->physicsParams.acceleration, v44);
			rdVector_ClipPrecision3(&vel_change);
			if ( !rdVector_IsZero3(&vel_change) )
				rdMatrix_TransformVector34Acc(&vel_change, &pThing->lookOrientation);
		}
    }

    if (pThing->physicsParams.mass != 0.0 && (pThing->sector->flags & SITH_SECTOR_HASTHRUST) && !(pThing->physicsParams.physflags & SITH_PF_NOTHRUST))
    {
        if ( pThing->sector->thrust.z > sithWorld_pCurrentWorld->worldGravity * pThing->physicsParams.mass )
        {
            sithThing_DetachThing(pThing);
            rdVector_Zero3(&pThing->physicsParams.addedVelocity);
            rdVector_Zero3(&out);
            if ( (pThing->physicsParams.physflags & SITH_PF_ANGTHRUST) != 0 )
            {
                if ( !rdVector_IsZero3(&pThing->physicsParams.angVel) )
                {
                    sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
                }
                rdVector_MultAcc3(&pThing->physicsParams.angVel, &pThing->physicsParams.field_1F8, deltaSeconds);

                rdVector_ClampValue3(&pThing->physicsParams.angVel, pThing->physicsParams.maxRotVel);
                rdVector_ClipPrecision3(&pThing->physicsParams.angVel);
            }
#ifdef PUPPET_PHYSICS
			else if (pThing->physicsParams.physflags & SITH_PF_ANGIMPULSE)
			{
				if (!rdVector_IsZero3(&pThing->physicsParams.angVel))
				{
					sithPhysics_ApplyDrag(&pThing->physicsParams.angVel, pThing->physicsParams.airDrag - -0.2, 0.0, deltaSeconds);
				}
			}
#endif

            if ( rdVector_IsZero3(&pThing->physicsParams.angVel) )
            {
                rdVector_Zero3(&a3);
            }
            else
            {
                rdVector_Scale3(&a3, &pThing->physicsParams.angVel, deltaSeconds);
            }
            if ( !rdVector_IsZero3(&a3) )
            {
                rdMatrix_BuildRotate34(&a, &a3);
                sithCollision_sub_4E7670(pThing, &a);
                if ( (pThing->physicsParams.physflags & SITH_PF_FLY) != 0 )
                    rdMatrix_TransformVector34Acc(&pThing->physicsParams.vel, &a);
                if ( ((jkPlayer_currentTickIdx + (pThing->thingIdx & 0xFF)) & 7) == 0 )
                    rdMatrix_Normalize34(&pThing->lookOrientation);
            }

#ifdef PUPPET_PHYSICS
			if (!rdVector_IsZero3(&pThing->physicsParams.rotVel))
			{
				sithPhysics_ApplyDrag(&pThing->physicsParams.rotVel, pThing->physicsParams.airDrag, 0.0001, deltaSeconds);
				//rdMath_ClampVectorRange(&pThing->physicsParams.rotVel, -pThing->physicsParams.maxRotVel, pThing->physicsParams.maxRotVel);

				rdMatrix34 invMat;
				rdMatrix_InvertOrtho34(&invMat, &pThing->lookOrientation);

				rdVector3 localRotVel;
				rdMatrix_TransformVector34(&localRotVel, &pThing->physicsParams.rotVel, &invMat);

				rdVector3 axis;
				flex_t angle = rdVector_Normalize3(&axis, &localRotVel) * deltaSeconds * (180.0f / M_PI);
				rdMatrix_BuildFromAxisAngle34(&a, &axis, angle);
				sithCollision_sub_4E7670(pThing, &a);
				//if (((bShowInvisibleThings + (pThing->thingIdx & 0xFF)) & 7) == 0)
					//rdMatrix_Normalize34(&pThing->lookOrientation);
			}
#endif

            if ( pThing->physicsParams.airDrag != 0.0 )
                sithPhysics_ApplyDrag(&pThing->physicsParams.vel, pThing->physicsParams.airDrag, 0.0, deltaSeconds);

            if (pThing->physicsParams.physflags & SITH_PF_USESTHRUST)
            {
                if (!(pThing->physicsParams.physflags & SITH_PF_FLY))
                {
                    rdVector_Scale3Acc(&pThing->physicsParams.acceleration, 0.3);
                }
                rdVector_Scale3(&out, &pThing->physicsParams.acceleration, deltaSeconds);
            }

            if ( pThing->physicsParams.mass != 0.0
              && (pThing->sector->flags & SITH_SECTOR_HASTHRUST)
              && !(pThing->physicsParams.physflags & SITH_PF_NOTHRUST))
            {
                rdVector_MultAcc3(&out, &pThing->sector->thrust, deltaSeconds);
            }

            if ( pThing->physicsParams.mass != 0.0 && (pThing->physicsParams.physflags & SITH_PF_USEGRAVITY) != 0 && (pThing->sector->flags & SITH_PF_USEGRAVITY) == 0 )
            {
                flex_t v91 = sithWorld_pCurrentWorld->worldGravity * deltaSeconds;
                if ( (pThing->physicsParams.physflags & SITH_PF_PARTIALGRAVITY) != 0 )
                    v91 = v91 * 0.5;
                out.z -= v91;
                pThing->physicsParams.addedVelocity.z = -v91;
            }
            rdVector_Add3Acc(&pThing->physicsParams.vel, &out);
            rdVector_ClipPrecision3(&pThing->physicsParams.vel);
            if ( !rdVector_IsZero3(&pThing->physicsParams.vel) )
            {
                rdVector_Scale3(&pThing->physicsParams.velocityMaybe, &pThing->physicsParams.vel, deltaSeconds);
            }
            return;
        }
        rdVector_MultAcc3(&vel_change, &pThing->sector->thrust, deltaSeconds);
    }
    rdVector_Add3Acc(&pThing->physicsParams.vel, &vel_change);
    
    // Is the player climbing up/down a slope?
    if ( pThing->type == SITH_THING_PLAYER
      && (pThing->physicsParams.physflags & SITH_PF_USEGRAVITY) != 0
      && v158 <= 1.0
      && (possibly_undef_2 < 0.8 || !rdVector_IsZero3(&pThing->physicsParams.vel)) )
    {
        flex_t v108 = stdMath_Clamp(1.0 - possibly_undef_2, 0.2, 0.8);
        pThing->physicsParams.vel.z -= sithWorld_pCurrentWorld->worldGravity * deltaSeconds * v108;
    }

    if ( !rdVector_IsZero3(&pThing->physicsParams.vel) )
    {
        flex_t v109 = rdVector_Dot3(&attachedNormal, &pThing->physicsParams.vel);

        if ( stdMath_ClipPrecision(v109) != 0.0 )
        {
            // If deltaSeconds is < the canonical phys tickrate, we get these
            // crazy oscillations
#ifdef FIXED_TIMESTEP_PHYS
            // Fix physics being tied to framerate?
            if (NEEDS_STEPPED_PHYS && (deltaSeconds < CANONICAL_PHYS_TICKRATE))
                v109 *= (deltaSeconds / CANONICAL_PHYS_TICKRATE);
#endif
#ifdef EXPERIMENTAL_FIXED_POINT
            if (!(NEEDS_STEPPED_PHYS) && (deltaSeconds < CANONICAL_PHYS_TICKRATE))
                v109 *= (deltaSeconds / CANONICAL_PHYS_TICKRATE);
#endif
            rdVector_MultAcc3(&pThing->physicsParams.vel, &attachedNormal, -v109);
        }
    }

#ifdef JKM_PARAMS
    if (bOverrideIdk) {
        pThing->physicsParams.vel.z = zOverride;
    }
#endif

    rdVector_ClipPrecision3(&pThing->physicsParams.vel);
    if ( !rdVector_IsZero3(&pThing->physicsParams.vel) )
    {
        rdVector_Scale3(&pThing->physicsParams.velocityMaybe, &pThing->physicsParams.vel, deltaSeconds);
    }

    flex_t v131;
    if (pThing->physicsParams.physflags & SITH_PF_CROUCHING)
    {
        v131 = v158 * possibly_undef_1 - (pThing->moveSize - -0.01);
    }
    else
    {
        flex_t v132 = pThing->physicsParams.height;
        if ( v132 == 0.0 )
        {
            if ( pThing->rdthing.type == RD_THINGTYPE_MODEL )
                v132 = pThing->rdthing.model3->insertOffset.z;
            new_ya = pThing->moveSize - -0.005;
            if ( v132 <= new_ya )
                v132 = new_ya;
        }
        v131 = possibly_undef_1 - v132;
    }

    // Slide down slopes
    v131 = stdMath_ClipPrecision(v131);
    if ( v131 != 0.0 )
    {
        // Fix physics being tied to framerate?
        flex_t orig_v131 = stdMath_ClampValue(v131, deltaSeconds * 0.5);
        flex_t new_v131 = v131 * (deltaSeconds / CANONICAL_PHYS_TICKRATE);
        new_v131 = stdMath_ClampValue(new_v131, deltaSeconds * 0.5);

        // If deltaSeconds is < the canonical phys tickrate, we get these
        // crazy oscillations
#ifdef FIXED_TIMESTEP_PHYS
        if (NEEDS_STEPPED_PHYS && deltaSeconds < CANONICAL_PHYS_TICKRATE)
            v131 = new_v131;
        else
            v131 = orig_v131;
#else
        v131 = orig_v131;
#endif
#ifdef EXPERIMENTAL_FIXED_POINT
        if (deltaSeconds < CANONICAL_PHYS_TICKRATE)
            v131 = new_v131;
#endif

        // Added: Fix turret slowly drifting up?
        if ((pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER) 
            && (pThing->actorParams.typeflags & SITH_AF_COMBO_FREEZE))
        {
            v131 = orig_v131;
        }

        if ( (pThing->physicsParams.physflags & SITH_PF_NOWALLGRAVITY) != 0 )
        {
            rdVector_MultAcc3(&pThing->physicsParams.velocityMaybe, &rdroid_zVector3, -v131);
        }
        else
        {
            rdVector_MultAcc3(&pThing->physicsParams.velocityMaybe, &attachedNormal, -v131);
        }

#ifdef DYNAMIC_POV
		pThing->physicsParams.povOffset = v131;
#endif
    }
}

#ifdef PUPPET_PHYSICS

void sithPhysics_AnglesToAngularVelocity(rdVector3* result, const rdVector3* eulerPYR, const rdMatrix34* orientation)
{
	// Local angular velocity in axis terms
	rdVector3 pitchAxis = { 1, 0, 0 };
	rdVector3 yawAxis = { 0, 0, 1 };
	rdVector3 rollAxis = { 0, 1, 0 };

	// Scale by respective angular velocities
	rdVector_Scale3(&pitchAxis, &pitchAxis, eulerPYR->x * (M_PI / 180.0f)); // Convert to radians
	rdVector_Scale3(&yawAxis, &yawAxis, eulerPYR->y * (M_PI / 180.0f));     // Convert to radians
	rdVector_Scale3(&rollAxis, &rollAxis, eulerPYR->z * (M_PI / 180.0f));   // Convert to radians

	// Transform each axis to world space
	rdVector3 pitchWorld, yawWorld, rollWorld;
	rdMatrix_TransformVector34(&pitchWorld, &pitchAxis, orientation);
	rdMatrix_TransformVector34(&yawWorld, &yawAxis, orientation);
	rdMatrix_TransformVector34(&rollWorld, &rollAxis, orientation);

	// Combine them into a single world angular velocity vector
	rdVector_Add3(result, &pitchWorld, &rollWorld);
	rdVector_Add3Acc(result, &yawWorld);
}

void sithPhysics_AngularVelocityToAngles(rdVector3* result, const rdVector3* angularVelocity, const rdMatrix34* orientation)
{
	rdVector3 pitchAxisA = { 1, 0, 0 };  // Local X-axis
	rdVector3 yawAxisA = { 0, 0, 1 };  // Local Z-axis
	rdVector3 rollAxisA = { 0, 1, 0 };  // Local Y-axis

	rdMatrix34 invOrient;
	rdMatrix_InvertOrtho34(&invOrient, &orientation);

	rdVector3 localUnitConstraint;
	rdMatrix_TransformVector34(&localUnitConstraint, angularVelocity, &invOrient);

	result->x = rdVector_Dot3(&localUnitConstraint, &pitchAxisA) * (180.0f / M_PI);  // Pitch
	result->y = rdVector_Dot3(&localUnitConstraint, &yawAxisA) * (180.0f / M_PI);   // Yaw
	result->z = rdVector_Dot3(&localUnitConstraint, &rollAxisA) * (180.0f / M_PI);   // Roll
}

void sithPhysics_ThingWake(sithThing* pThing)
{
	if (pThing->moveType != SITH_MT_PHYSICS && pThing->moveType != SITH_MT_RAGDOLL)
		return;

	pThing->physicsParams.physflags &= ~SITH_PF_RESTING;
	pThing->physicsParams.restTimer = 0.0f;

	//if (pThing->constraintRoot)
		//sithPhysics_ThingWake(pThing->constraintRoot);
}

// calculates a buoyancy value from a radius and the fluid density of water
flex_t sithPhysics_BuoyancyFromRadius(flex_t radius)
{
	// Calculate the volume of the sphere
	flex_t fluidDensity = 1000.0f; // density of water in kg/m^3
	flex_t volume = (4.0f / 3.0f) * M_PI * powf(radius, 3.0f);
	return fluidDensity * volume;
}

#endif