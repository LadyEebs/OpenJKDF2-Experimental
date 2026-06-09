#include "sithLight.h"

#include "Engine/rdLight.h"
#include "Primitives/rdMath.h"
#include "World/sithWorld.h"
#include "General/stdConffile.h"
#include "General/stdString.h"
#include "General/stdMath.h"
#include "stdPlatform.h"
#include "jk.h"

#ifdef RENDER_DROID2

int sithLight_Load(sithWorld *world, int a2)
{
	return 1;

    if (a2)
        return 0;

    stdConffile_ReadArgs();

	if (_memcmp(stdConffile_entry.args[0].value, "world", 6u) && _memcmp(stdConffile_entry.args[0].value, "editor", 6u))
		return 0;

    if (_memcmp(stdConffile_entry.args[1].value, "lights", 7u) )
        return 0;

    int numLights = _atoi(stdConffile_entry.args[2].value);
    if ( !numLights)
        return 1;

    if ( !sithLight_New(world, numLights) )
    {
        stdPrintf(pSithHS->errorPrint, ".\\World\\sithLight.c", 163, "Memory error while reading lights, line %d.\n", stdConffile_linenum, 0, 0, 0);
        return 0;
    }
    
    sithWorld_UpdateLoadPercent(95.0);
    
    float loadPercent = 95.0;
    if ( stdConffile_ReadArgs() )
    {
        while ( _memcmp(stdConffile_entry.args[0].value, "end", 4u) )
        {
			sithLight* light = &world->lights[world->numLightsLoaded];
			light->id = world->numLightsLoaded++;
			rdLight_NewEntry(&light->rdlight);
			light->rdlight.type = atoi(stdConffile_entry.args[1].value);
			light->rdlight.active = 1;
			light->pos.x = atof(stdConffile_entry.args[2].value);
			light->pos.y = atof(stdConffile_entry.args[3].value);
			light->pos.z = atof(stdConffile_entry.args[4].value);
			light->rdlight.intensity = atof(stdConffile_entry.args[5].value);
			float range = atof(stdConffile_entry.args[6].value);
		//	light->rdlight.intensity *= range;
			light->rdlight.falloffMin = light->rdlight.falloffMax = range;// / 0.4;

			if (range < 0 && world->backdropSector)
			{
				light->rdlight.active = 0;
				rdVector_Sub3Acc(&light->pos, &world->backdropSector->center);
				//rdVector_Neg3Acc(&light->pos);
				rdVector_Normalize3Acc(&light->pos);
				//rdVector_Scale3Acc(&light->pos, 10000.0f);
			}

            float percentDelta = 5.0 / (double)numLights;
            loadPercent += percentDelta;
            sithWorld_UpdateLoadPercent(loadPercent);
            if ( !stdConffile_ReadArgs() )
                break;
        }
    }
    sithWorld_UpdateLoadPercent(100.0);
    return 1;
}

void sithLight_Free(sithWorld *world)
{
    if (!world->numLights)
        return;

	if(world->lights)
	    pSithHS->free(world->lights);
	if(world->lightBuckets)
		pSithHS->free(world->lightBuckets);
    world->lights = 0;
	world->lightBuckets = 0;
    world->numLightsLoaded = 0;
    world->numLights = 0;
	world->numLightBuckets = 0;
}

int sithLight_New(sithWorld *world, int num)
{
	sithLight*lights = (sithLight*)pSithHS->alloc(sizeof(sithLight) * num);
    world->lights = lights;
    if ( !lights)
        return 0;
    world->numLights = num;
    world->numLightsLoaded = 0;
    _memset(lights, 0, sizeof(sithLight) * num);

	world->numLightBuckets = (num + 63) / 64;
	uint64_t* buckets = (uint64_t*)pSithHS->alloc(sizeof(uint64_t) * world->numLightBuckets);
	_memset(buckets, 0, sizeof(uint64_t) * world->numLightBuckets);
	world->lightBuckets = buckets;

    return 1;
}

#endif
