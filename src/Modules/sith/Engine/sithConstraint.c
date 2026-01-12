#include "sithConstraint.h"
#include "Engine/sithCollision.h"
#include "Engine/sithPhysics.h"

#include "General/stdMath.h"
#include "Primitives/rdMath.h"
#include "Primitives/rdQuat.h"

#include "Primitives/rdSprite.h"
#include "Primitives/rdPolyline.h"
#include "Engine/rdThing.h"
#ifdef JOB_SYSTEM
#include "Modules/std/stdJob.h"
#endif

#ifdef PUPPET_PHYSICS

#ifdef JOB_SYSTEM
#define USE_JOBS 0 // not faster atm
#else
#define USE_JOBS 0
#endif

#define CONSTRAINT_VEL_LIMIT 50.0f

extern flex_t jkPlayer_puppetAngBias;
extern flex_t jkPlayer_puppetPosBias;
extern flex_t jkPlayer_puppetFriction;

sithConstraint* sithConstraint_AddConstraint(sithThing* pThing)
{
	if (!pThing->constraints)
	{
		pThing->numTotalConstraints = 8;
		pThing->constraints = pSithHS->alloc(sizeof(sithConstraint) * pThing->numTotalConstraints);
	}

	int nextIdx = pThing->numConstraints++;
	if (nextIdx >= pThing->numTotalConstraints)
	{
		pThing->numTotalConstraints *= 2;
		pThing->constraints = pSithHS->realloc(pThing->constraints, sizeof(sithConstraint) * pThing->numTotalConstraints);
	}

	sithConstraint* constraint = &pThing->constraints[nextIdx];
	memset(constraint, 0, sizeof(sithConstraint));
	return constraint;
}

void sithConstraint_InitConstraint(sithConstraint* pConstraint, int type, sithThing* pParentThing, sithThing* pConstrainedThing, sithThing* pTargetThing)
{
	pConstraint->type = type;
	pConstraint->flags = 0;
	pConstraint->parentThing = pParentThing;
	pConstraint->constrainedThing = pConstrainedThing;
	pConstraint->targetThing = pTargetThing;

	//pConstraint->next = pParentThing->constraints;
	//pParentThing->constraints = pConstraint;
	
	pConstrainedThing->constraintParent = pTargetThing;
	pConstrainedThing->constraintRoot = pParentThing;
}

void sithConstraint_AddBallSocketConstraint(sithThing* pThing, sithThing* pConstrainedThing, sithThing* pTargetThing, const rdVector3* pTargetAnchor, const rdVector3* pConstrainedAnchor, flex_t distance)
{
	//sithBallSocketConstraint* constraint = (sithBallSocketConstraint*)pSithHS->alloc(sizeof(sithBallSocketConstraint));
	sithConstraint* constraint = sithConstraint_AddConstraint(pThing);
	if (!constraint)
		return;
	//memset(constraint, 0, sizeof(sithBallSocketConstraint));
	sithConstraint_InitConstraint(constraint, SITH_CONSTRAINT_BALLSOCKET, pThing, pConstrainedThing, pTargetThing);

	constraint->ballSocket.targetAnchor = *pTargetAnchor;
	constraint->ballSocket.constraintAnchor = *pConstrainedAnchor;
}

void sithConstraint_AddConeConstraint(sithThing* pThing, sithThing* pConstrainedThing, sithThing* pTargetThing, const rdVector3* pAxis, flex_t angle, const rdVector3* pJointAxis, flex_t twistAngle)
{
	//sithConeLimitConstraint* constraint = (sithConeLimitConstraint*)pSithHS->alloc(sizeof(sithConeLimitConstraint));
	sithConstraint* constraint = sithConstraint_AddConstraint(pThing);
	if (!constraint)
		return;
	//memset(constraint, 0, sizeof(sithConeLimitConstraint));
	sithConstraint_InitConstraint(constraint, SITH_CONSTRAINT_CONE, pThing, pConstrainedThing, pTargetThing);

	constraint->coneLimit.coneAxis = *pAxis;
	constraint->coneLimit.coneAngle = angle;
	constraint->coneLimit.coneAngleCos = stdMath_Cos(angle);
	constraint->coneLimit.jointAxis = *pJointAxis;

	if (twistAngle < 360.0)
		sithConstraint_AddTwistConstraint(pThing, pConstrainedThing, pTargetThing, /*pAxis*/pJointAxis, pJointAxis, -twistAngle, twistAngle);
}

void sithConstraint_AddHingeConstraint(sithThing* pThing, sithThing* pConstrainedThing, sithThing* pTargetThing, const rdVector3* pTargetAxis, const rdVector3* pJointAxis, flex_t minAngle, flex_t maxAngle)
{
	//sithHingeLimitConstraint* constraint = (sithHingeLimitConstraint*)pSithHS->alloc(sizeof(sithHingeLimitConstraint));
	sithConstraint* constraint = sithConstraint_AddConstraint(pThing);
	if (!constraint)
		return;
	//memset(constraint, 0, sizeof(sithHingeLimitConstraint));
	sithConstraint_InitConstraint(constraint, SITH_CONSTRAINT_HINGE, pThing, pConstrainedThing, pTargetThing);

	constraint->hingeLimit.targetAxis = *pTargetAxis;
	constraint->hingeLimit.jointAxis = *pJointAxis;

	if (minAngle > -360.0 && maxAngle < 360.0)
		sithConstraint_AddTwistConstraint(pThing, pConstrainedThing, pTargetThing, pTargetAxis, pJointAxis, minAngle, maxAngle);
}

void sithConstraint_AddTwistConstraint(sithThing* pThing, sithThing* pConstrainedThing, sithThing* pTargetThing, const rdVector3* pTargetAxis, const rdVector3* pJointAxis, flex_t minAngle, flex_t maxAngle)
{
	//sithTwistLimitConstraint* constraint = (sithTwistLimitConstraint*)pSithHS->alloc(sizeof(sithTwistLimitConstraint));
	sithConstraint* constraint = sithConstraint_AddConstraint(pThing);
	if (!constraint)
		return;
//	memset(constraint, 0, sizeof(sithTwistLimitConstraint));
	sithConstraint_InitConstraint(constraint, SITH_CONSTRAINT_TWIST, pThing, pConstrainedThing, pTargetThing);

	constraint->twistLimit.targetAxis = *pTargetAxis;
	constraint->twistLimit.jointAxis = *pJointAxis;
	constraint->twistLimit.minAngle = minAngle;
	constraint->twistLimit.maxAngle = maxAngle;
}

//void sithConstraint_RemoveConstraint(sithConstraint* pConstraint)
//{
//	if(!pConstraint)
//		return;
//
//	sithConstraint* pConstraintList = pConstraint->parentThing->constraints;
//	if (pConstraintList == pConstraint)
//	{
//		pConstraint->parentThing->constraints = pConstraint->next;
//		pSithHS->free(pConstraint);
//		return;
//	}
//		
//	sithConstraint* current = pConstraintList;
//	while (current->next && current->next != pConstraint)
//		current = current->next;
//
//	if (current->next == pConstraint)
//	{
//		current->next = pConstraint->next;
//		pSithHS->free(pConstraint);
//	}
//}

// 1 / (J * M^-1 * J^T)
flex_t sithConstraint_ComputeEffectiveMass(sithConstraint* constraint)
{
	flex_t effectiveMass =
		  rdVector_Dot3(&constraint->result.JvA, &constraint->result.JvA) / constraint->targetThing->physicsParams.mass
		+ rdVector_Dot3(&constraint->result.JvB, &constraint->result.JvB) / constraint->constrainedThing->physicsParams.mass
		+ rdVector_Dot3(&constraint->result.JrA, &constraint->result.JrA) / constraint->targetThing->physicsParams.inertia
		+ rdVector_Dot3(&constraint->result.JrB, &constraint->result.JrB) / constraint->constrainedThing->physicsParams.inertia;
	return (effectiveMass > 0.0001f) ? (1.0f / effectiveMass) : 0.0001f;
}

static void sithConstraint_SolveBallSocketConstraint(/*sithBallSocketConstraint*/sithConstraint* pConstraint, flex_t deltaSeconds)
{
	sithConstraintResult* pResult = &pConstraint->result;

	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	// anchor A offset in world space
	rdVector3 offsetA;
	rdMatrix_TransformVector34(&offsetA, &pConstraint->ballSocket.targetAnchor, &bodyA->lookOrientation);

	// anchor B offset in world space
	rdVector3 offsetB;
	rdMatrix_TransformVector34(&offsetB, &pConstraint->ballSocket.constraintAnchor, &bodyB->lookOrientation);

	// anchor positions in world space
	rdVector3 anchorA, anchorB;
	rdVector_Add3(&anchorA, &offsetA, &bodyA->position);
	rdVector_Add3(&anchorB, &offsetB, &bodyB->position);

	// vector between the anchors and the distance
	rdVector3 constraint, unitConstraint;
	rdVector_Sub3(&constraint, &anchorA, &anchorB);

	// calculate constraint force
	// currently scalar, can be vector for more accuracy
	pResult->C = rdVector_Normalize3(&unitConstraint, &constraint);
	if(stdMath_Fabs(pResult->C) < 0.00001f)
	{
		pResult->C = 0.0f;
		return;
	}

	// add bias for error correction
	pResult->C = -(jkPlayer_puppetPosBias / deltaSeconds) * pResult->C;
	pResult->C = stdMath_Clamp(pResult->C, -CONSTRAINT_VEL_LIMIT, CONSTRAINT_VEL_LIMIT);

	rdVector3 unitConstraintN;
	rdVector_Neg3(&unitConstraintN, &unitConstraint);

	// Calculate Jacobians
	pResult->JvA = unitConstraint;
	pResult->JvB = unitConstraintN;	
	rdVector_Cross3(&pResult->JrA, &offsetA, &unitConstraintN);
	rdVector_Cross3(&pResult->JrB, &offsetB, &unitConstraint);
}

static void sithConstraint_SolveConeConstraint(/*sithConeLimitConstraint*/sithConstraint* pConstraint, flex_t deltaSeconds)
{
	sithConstraintResult* pResult = &pConstraint->result;

	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	// cone axis to world space
	rdVector3 coneAxis;
	rdMatrix_TransformVector34(&coneAxis, &pConstraint->coneLimit.coneAxis, &bodyA->lookOrientation);
	rdVector_Normalize3Acc(&coneAxis);

	// joint axis to world space
	rdVector3 thingAxis;
	rdMatrix_TransformVector34(&thingAxis, &pConstraint->coneLimit.jointAxis, &bodyB->lookOrientation);
	rdVector_Normalize3Acc(&thingAxis);
	
	rdVector3 relativeAxis;
	//rdVector_Cross3(&relativeAxis, &coneAxis, &thingAxis);
	rdVector_Cross3(&relativeAxis, &thingAxis, &coneAxis);
	rdVector_Normalize3Acc(&relativeAxis);

	flex_t dotProduct = rdVector_Dot3(&coneAxis, &thingAxis);
	if (dotProduct >= pConstraint->coneLimit.coneAngleCos)
	{
		pResult->C = 0.0f;
		return;
	}

	rdVector3 JwA = relativeAxis;
	rdVector3 JwB = relativeAxis;
	rdVector_Neg3Acc(&JwB);

	pResult->JvA = rdroid_zeroVector3;
	pResult->JvB = rdroid_zeroVector3;
	pResult->JrA = JwA;
	pResult->JrB = JwB;
	pResult->C = pConstraint->coneLimit.coneAngleCos - dotProduct;
	pResult->C = (jkPlayer_puppetAngBias / deltaSeconds) * pResult->C;
	pResult->C = stdMath_Clamp(pResult->C, -CONSTRAINT_VEL_LIMIT, CONSTRAINT_VEL_LIMIT);
}

static void sithConstraint_SolveHingeConstraint(/*sithHingeLimitConstraint*/sithConstraint* pConstraint, flex_t deltaSeconds)
{
	sithConstraintResult* pResult = &pConstraint->result;
	
	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	rdVector_Zero3(&pResult->JvA);
	rdVector_Zero3(&pResult->JvB);

	// hinge axii to world space
	rdVector3 hingeAxisA, hingeAxisB;	
	rdMatrix_TransformVector34(&hingeAxisA, &pConstraint->hingeLimit.targetAxis, &bodyA->lookOrientation);
	rdMatrix_TransformVector34(&hingeAxisB, &pConstraint->hingeLimit.jointAxis, &bodyB->lookOrientation);

	// angle between the 2 axis
	flex_t cosAngle = rdVector_Dot3(&hingeAxisA, &hingeAxisB);
	pResult->C = 1.0f - cosAngle;
	pResult->C = (jkPlayer_puppetAngBias / deltaSeconds) * pResult->C;
	pResult->C = stdMath_Clamp(pResult->C, -CONSTRAINT_VEL_LIMIT, CONSTRAINT_VEL_LIMIT);

	// perpendicular vector for angle-axis correction
	rdVector3 rotationAxis;
	rdVector_Cross3(&rotationAxis, &hingeAxisA, &hingeAxisB);
	rdVector_Normalize3Acc(&rotationAxis);

	pResult->JrA = rotationAxis;
	pResult->JrB = rotationAxis;
	rdVector_Neg3Acc(&pResult->JrA);
}

static void sithConstraint_SolveTwistConstraint(/*sithTwistLimitConstraint*/sithConstraint* pConstraint, flex_t deltaSeconds)
{
	sithConstraintResult* pResult = &pConstraint->result;

	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	rdVector_Zero3(&pResult->JvA);
	rdVector_Zero3(&pResult->JvB);

	// perpendicular vectors to the axis
	rdVector3 refVectorA, refVectorB;
	rdVector_Perpendicular3(&refVectorA, &pConstraint->twistLimit.targetAxis);
	rdVector_Perpendicular3(&refVectorB, &pConstraint->twistLimit.jointAxis);
	rdMatrix_TransformVector34Acc(&refVectorA, &bodyA->lookOrientation);
	rdMatrix_TransformVector34Acc(&refVectorB, &bodyB->lookOrientation);

	// compute a twist axis between by projecting the joint axis onto the target axis plane
	rdVector3 targetAxis;
	rdMatrix_TransformVector34(&targetAxis, &pConstraint->twistLimit.targetAxis, &bodyA->lookOrientation);

	rdVector3 jointAxis;
	rdMatrix_TransformVector34(&jointAxis, &pConstraint->twistLimit.jointAxis, &bodyB->lookOrientation);
	
	rdVector3 twistAxis;
	rdVector_ProjectDir3(&twistAxis, &jointAxis, &targetAxis);
	rdVector_Normalize3Acc(&twistAxis);

	// project the reference dirs onto the twist plane
	rdVector3 projA, projB;
	//rdVector_ProjectDir3(&projA, &refVectorA, &twistAxis);
	//rdVector_ProjectDir3(&projB, &refVectorB, &twistAxis);
	rdVector_Scale3(&projA, &twistAxis, rdVector_Dot3(&refVectorA, &twistAxis));
	rdVector_Sub3(&projA, &refVectorA, &projA);

	rdVector_Scale3(&projB, &twistAxis, rdVector_Dot3(&refVectorB, &twistAxis));
	rdVector_Sub3(&projB, &refVectorB, &projB);

	rdVector_Normalize3Acc(&projA);
	rdVector_Normalize3Acc(&projB);

	// compute cos(theta) and sin(theta)
	flex_t cosTheta = rdVector_Dot3(&projA, &projB);
	
	rdVector3 crossProduct;
	rdVector_Cross3(&crossProduct, &projB, &projA);
	flex_t sinTheta = rdVector_Dot3(&twistAxis, &crossProduct);

	// compute the twist angle
	flex_t twistAngle = atan2f(sinTheta, cosTheta) * 180.0f / M_PI;// stdMath_ArcTan4(sinTheta, cosTheta);
	if (twistAngle < pConstraint->twistLimit.minAngle)
	{
		pResult->C = pConstraint->twistLimit.minAngle - twistAngle;
	}
	else if (twistAngle > pConstraint->twistLimit.maxAngle)
	{
		pResult->C = pConstraint->twistLimit.maxAngle - twistAngle;
	}
	else
	{
		pResult->C = 0.0f;
		return;
	}
	pResult->C *= M_PI / 180.0f;
	pResult->C = (jkPlayer_puppetAngBias / deltaSeconds) * pResult->C;
	pResult->C = stdMath_Clamp(pResult->C, -CONSTRAINT_VEL_LIMIT, CONSTRAINT_VEL_LIMIT);

	pResult->JrA = twistAxis;
	pResult->JrB = twistAxis;
	rdVector_Neg3Acc(&pResult->JrA);
}

void sithConstraint_ApplyImpulses(sithThing* pThing)
{
	//sithConstraint* constraint = pThing->constraints;
	//for (; constraint; constraint = constraint->next)
	for (int i = 0; i < pThing->numConstraints; ++i)
	{
		sithConstraint* constraint = &pThing->constraints[i];
		if (constraint->flags & SITH_CONSTRAINT_DISABLED)
			continue;
		
		if (constraint->result.C == 0.0f)
			continue;

		rdVector_Add3Acc(&constraint->targetThing->physicsParams.vel, &constraint->result.linearImpulseA);
		rdVector_Add3Acc(&constraint->targetThing->physicsParams.rotVel, &constraint->result.angularImpulseA);

		rdVector_Add3Acc(&constraint->constrainedThing->physicsParams.vel, &constraint->result.linearImpulseB);
		rdVector_Add3Acc(&constraint->constrainedThing->physicsParams.rotVel, &constraint->result.angularImpulseB);
	}
}

int sithConstraint_SatisfyConstraint(sithConstraint* c, flex_t deltaSeconds)
{
	if (stdMath_Fabs(c->result.C) < 0.0001f)
		return 0;

	sithThing* bodyA = c->targetThing;
	sithThing* bodyB = c->constrainedThing;

	// Compute J * v
	flex_t Jv = rdVector_Dot3(&c->result.JvA, &bodyA->physicsParams.vel) +
		rdVector_Dot3(&c->result.JrA, &bodyA->physicsParams.rotVel) +
		rdVector_Dot3(&c->result.JvB, &bodyB->physicsParams.vel) +
		rdVector_Dot3(&c->result.JrB, &bodyB->physicsParams.rotVel);
	Jv = stdMath_ClipPrecision(Jv);
	if (stdMath_Fabs(Jv) < 0.0001f)
		return 0;

	// Compute corrective impulse
	flex_t deltaLambda = (c->result.C - Jv) * c->effectiveMass;
	deltaLambda *= 0.8; // todo: what's a good damping value?
	deltaLambda = stdMath_ClipPrecision(deltaLambda);

	if (stdMath_Fabs(deltaLambda) < 0.0001f)
		return 0;

	// Add deltaLambda to the current lambda
	flex_t newLambda = c->lambda + deltaLambda;
	//newLambda = stdMath_Clamp(newLambda, -100.0f, 100.0f);//c->lambdaMin, c->lambdaMax);
	deltaLambda = newLambda - c->lambda;
	c->lambda = newLambda;

	// Apply impulse to body A
	rdVector_Scale3(&c->result.linearImpulseA, &c->result.JvA, deltaLambda / c->targetThing->physicsParams.mass);
	rdVector_Scale3(&c->result.angularImpulseA, &c->result.JrA, deltaLambda / c->targetThing->physicsParams.inertia);

	// Apply impulse to body B
	rdVector_Scale3(&c->result.linearImpulseB, &c->result.JvB, deltaLambda / c->constrainedThing->physicsParams.mass);
	rdVector_Scale3(&c->result.angularImpulseB, &c->result.JrB, deltaLambda / c->constrainedThing->physicsParams.inertia);

	return 1;
}

#if USE_JOBS
void sithConstraint_SatisfyConstraintJob(void* arg)
{
	sithConstraint* c = (sithConstraint*)arg;
	sithConstraint_SatisfyConstraint(c, sithTime_deltaSeconds);
}
#endif

// Projected Gauss-Seidel
void sithConstraint_SatisfyConstraints(sithThing* thing, int iterations, flex_t deltaSeconds)
{
	// each iteration is dependent on the previous result, so we can't thread that part
	for (int iter = 0; iter < iterations; iter++)
	{
		//sithConstraint* c = thing->constraints;
		//for (; c; c = c->next)
		for (int i = 0; i < thing->numConstraints; ++i)
		{
			sithConstraint* c = &thing->constraints[i];
#if USE_JOBS
			stdJob_Execute(sithConstraint_SatisfyConstraintJob, c);
#else
			sithConstraint_SatisfyConstraint(c, deltaSeconds);
#endif
		}
#if USE_JOBS
		stdJob_Wait();
#endif
		sithConstraint_ApplyImpulses(thing);
	}
}

void sithConstraint_ApplyFriction(sithThing* pThing, flex_t deltaSeconds)
{
	// apply friction as impulses
	//sithConstraint* constraint = pThing->constraints;
	//for (; constraint; constraint = constraint->next)
	for (int i = 0; i < pThing->numConstraints; ++i)
	{
		sithConstraint* constraint = &pThing->constraints[i];
		if (constraint->flags & SITH_CONSTRAINT_DISABLED)
			continue;

		flex_t invMassA = 1.0f / constraint->targetThing->physicsParams.mass;
		flex_t invMassB = 1.0f / constraint->constrainedThing->physicsParams.mass;
		flex_t totalInvMass = invMassA + invMassB;

		rdVector3 omegaRel;
		rdVector_Sub3(&omegaRel, &constraint->targetThing->physicsParams.rotVel, &constraint->constrainedThing->physicsParams.rotVel);
		rdVector_Scale3Acc(&omegaRel, jkPlayer_puppetFriction / totalInvMass);

		rdVector3 impulseA, impulseB;
		rdVector_Scale3(&impulseA, &omegaRel, -invMassA);
		rdVector_Scale3(&impulseB, &omegaRel, invMassB);

		rdVector_Add3Acc(&constraint->targetThing->physicsParams.rotVel, &impulseA);
		rdVector_Add3Acc(&constraint->constrainedThing->physicsParams.rotVel, &impulseB);
	}
}

void sithConstraint_SolveConstraint(sithConstraint* pConstraint, flex_t deltaSeconds)
{
	memset(&pConstraint->result, 0, sizeof(sithConstraintResult));
	if (pConstraint->flags & SITH_CONSTRAINT_DISABLED)
		return;

	pConstraint->lambda *= 0.9f;
	switch (pConstraint->type)
	{
	case SITH_CONSTRAINT_BALLSOCKET:
		sithConstraint_SolveBallSocketConstraint(/*(sithBallSocketConstraint*)*/ pConstraint, deltaSeconds);
		break;
	case SITH_CONSTRAINT_CONE:
		sithConstraint_SolveConeConstraint(/*(sithConeLimitConstraint*)/**/pConstraint, deltaSeconds);
		break;
	case SITH_CONSTRAINT_HINGE:
		sithConstraint_SolveHingeConstraint(/*(sithHingeLimitConstraint*)*/ pConstraint, deltaSeconds);
		break;
	case SITH_CONSTRAINT_TWIST:
		sithConstraint_SolveTwistConstraint(/*(sithTwistLimitConstraint*)*/pConstraint, deltaSeconds);
		break;
	default:
		break;
	}
	pConstraint->result.C = stdMath_ClipPrecision(pConstraint->result.C);
	pConstraint->effectiveMass = sithConstraint_ComputeEffectiveMass(pConstraint);
}

#if USE_JOBS
void sithConstraint_SolveConstraintJob(void* arg)
{
	sithConstraint* pConstraint = (sithConstraint*)arg;
	sithConstraint_SolveConstraint(pConstraint, sithTime_deltaSeconds);
}
#endif

void sithConstraint_TickConstraints(sithThing* pThing, flex_t deltaSeconds)
{
	if (pThing->physicsParams.physflags & SITH_PF_RESTING)
		return;

	if (!pThing->constraints || !pThing->sector)
		return;

//	sithConstraint* pConstraint = pThing->constraints;
//	for (; pConstraint; pConstraint = pConstraint->next)
	for (int i = 0; i < pThing->numConstraints; ++i)
	{
		sithConstraint* pConstraint = &pThing->constraints[i];
#if USE_JOBS
		stdJob_Execute(sithConstraint_SolveConstraintJob, pConstraint);
#else
		sithConstraint_SolveConstraint(pConstraint, deltaSeconds);
#endif
	}

#if USE_JOBS
	stdJob_Wait();
#endif

	int iterations = 5; // 5 iterations by default
	if (pThing->type == SITH_THING_PLAYER)
	{
		iterations = 10; // fixed iterations for players
	}
	else if ((pThing->lastRenderedTickIdx + 1) == jkPlayer_currentTickIdx) // thing is visible, use more iterations
	{
		// reduce iteration count by distance
		flex_t dist = rdVector_Dist3(&pThing->position, &sithCamera_currentCamera->vec3_1);
		iterations = 15 - stdMath_Clamp(dist, 0.0f, 10.0f);
	}

	sithConstraint_SatisfyConstraints(pThing, iterations, deltaSeconds);
	sithConstraint_ApplyFriction(pThing, deltaSeconds);
}

static void sithConstraint_DrawBallSocket(/*sithBallSocketConstraint*/sithConstraint* pConstraint)
{
	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	rdVector3 targetAnchor;
	bodyA->lookOrientation.scale = bodyA->position;
	rdMatrix_TransformPoint34(&targetAnchor, &pConstraint->ballSocket.targetAnchor, &bodyA->lookOrientation);
	rdVector_Zero3(&bodyA->lookOrientation.scale);

	rdVector3 constrainedAnchor;
	bodyB->lookOrientation.scale = bodyB->position;
	rdMatrix_TransformPoint34(&constrainedAnchor, &pConstraint->ballSocket.constraintAnchor, &bodyB->lookOrientation);
	rdVector_Zero3(&bodyB->lookOrientation.scale);

	rdVector3 offset = { 0, -bodyB->moveSize, 0 };

	for (int i = 0; i < 2; ++i)
	{
		rdSprite debugSprite;
		rdSprite_NewEntry(&debugSprite, "dbgragoll", 0, i == 0 ? "saberred0.mat" : "saberblue0.mat", 0.005f, 0.005f, RD_GEOMODE_TEXTURED, RD_LIGHTMODE_FULLYLIT, RD_TEXTUREMODE_AFFINE, 1.0f, &offset);

		rdThing debug;
		rdThing_NewEntry(&debug, pConstraint->parentThing);
		rdThing_SetSprite3(&debug, &debugSprite);
		rdMatrix34 mat;
		rdMatrix_BuildTranslate34(&mat, i == 0 ? &targetAnchor : &constrainedAnchor);

		rdSprite_Draw(&debug, &mat);

		rdSprite_FreeEntry(&debugSprite);
		rdThing_FreeEntry(&debug);
	}
}

static void sithConstraint_DrawCone(/*sithConeLimitConstraint*/sithConstraint* pConstraint)
{
	sithThing* bodyA = pConstraint->targetThing;
	sithThing* bodyB = pConstraint->constrainedThing;

	rdVector3 coneAxis;
	rdMatrix_TransformVector34(&coneAxis, &pConstraint->coneLimit.coneAxis, &bodyA->lookOrientation);
	rdVector_Normalize3Acc(&coneAxis);

	rdVector3 thingAxis;
	rdMatrix_TransformVector34(&thingAxis, &pConstraint->coneLimit.jointAxis, &bodyB->lookOrientation);

	for (int i = 0; i < 2; ++i)
	{
		const char* mat0;
		const char* mat1;
		if (i == 0)
		{
			mat0 = "saberyellow0.mat";
			mat1 = "saberyellow1.mat";
		}
		else if (i == 1)
		{
			mat0 = "saberred0.mat";
			mat1 = "saberred1.mat";
		}

		flex_t len = 0.01f;
		flex_t sizex = 0.002f;
		flex_t sizey = 0.002f;
		rdVector3 lookPos;
		rdVector_Add3(&lookPos, &bodyA->position, i == 0 ? &coneAxis : &thingAxis);

		if (i == 0)
		{
			sizey = 0.05f * (pConstraint->coneLimit.coneAngle / 180.0f);
			len = 0.05f;
		}

		rdPolyLine debugLine;
		_memset(&debugLine, 0, sizeof(rdPolyLine));
		if (rdPolyLine_NewEntry(&debugLine, "dbgragoll", mat1, mat0, len, sizex, sizey, RD_GEOMODE_TEXTURED, RD_LIGHTMODE_FULLYLIT, RD_TEXTUREMODE_PERSPECTIVE, 1.0f))
		{
			rdThing debug;
			rdThing_NewEntry(&debug, pConstraint->parentThing);
			rdThing_SetPolyline(&debug, &debugLine);

			rdMatrix34 look;
			rdMatrix_LookAt(&look, &bodyA->position, &lookPos, 0.0f);

			rdPolyLine_Draw(&debug, &look);

			rdPolyLine_FreeEntry(&debugLine);
			rdThing_FreeEntry(&debug);
		}
	}
}

void sithConstraint_Draw(sithConstraint* pConstraint)
{
	switch (pConstraint->type)
	{
		case SITH_CONSTRAINT_BALLSOCKET:
			sithConstraint_DrawBallSocket(pConstraint);
			break;
		case SITH_CONSTRAINT_CONE:
			sithConstraint_DrawCone(pConstraint);
			break;
		case SITH_CONSTRAINT_HINGE:
			// todo
			break;
		default:
			break;
	}
}

void sithConstraint_DebugDrawConstraints(sithThing* pThing)
{
	//sithConstraint* pConstraint = pThing->constraints;
	//for (; pConstraint; pConstraint = pConstraint->next)
	for (int i = 0; i < pThing->numConstraints; ++i)
	{
		sithConstraint* pConstraint = &pThing->constraints[i];
		sithConstraint_Draw(pConstraint);
	}
}

#endif
