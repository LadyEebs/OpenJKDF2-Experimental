#ifndef _SITHRAGDOLL_H
#define _SITHRAGDOLL_H

#include "types.h"

int sithRagdoll_Load(sithWorld *world, int a2);
int sithRagdoll_LoadRagdollEntry(sithRagdoll* ragdoll, char *fpath);
sithRagdoll* sithRagdoll_LoadEntry(char *a1);
void sithRagdoll_Free(sithWorld *world);

extern int sithRagdoll_activeRagdolls;
extern int sithRagdoll_restingRagdolls;

void sithRagdoll_StartPhysics(sithThing* pThing, rdVector3* pInitialVel, flex_t deltaSeconds);
void sithRagdoll_StopPhysics(sithThing* pThing);
void sithRagdoll_ThingTick(sithThing* pThing, flex_t deltaSeconds);
void sithRagdoll_DebugDrawPhysicsBodies(sithThing* pThing);
void sithRagdoll_DebugDrawPhysicsJoints(sithThing* pThing);


#endif // _SITHRAGDOLL_H
