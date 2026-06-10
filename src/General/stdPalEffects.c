#include "stdPalEffects.h"
#include "stdMath.h"

#include "jk.h"

#ifdef TILE_SW_RASTER
rdColor24 stdPalEffects_paletteCube[STDPAL_CUBE_SIZE] = { 0 };
stdPalEffectSetPaletteFunc_t stdPalEffects_setPaletteCube = { 0 };
#endif

#ifdef TILE_SW_RASTER
int stdPalEffects_Open(stdPalEffectSetPaletteFunc_t a1, stdPalEffectSetPaletteFunc_t a2)
#else
int stdPalEffects_Open(stdPalEffectSetPaletteFunc_t a1)
#endif
{
    stdPalEffects_SetPaletteFunc(a1);
#ifdef TILE_SW_RASTER
	stdPalEffects_setPaletteCube = a2;
#endif

    _memset(stdPalEffects_aEffects, 0, sizeof(stdPalEffects_aEffects));
    _memset(&stdPalEffects_state, 0, sizeof(stdPalEffects_state));
    _memset(&stdPalEffects_state.effect, 0, sizeof(stdPalEffects_state.effect));
    stdPalEffects_numEffectRequests = 0;
    stdPalEffects_state.effect.fade = 1.0;
    stdPalEffects_SetStateBools(1, 1, 1, 1);
    return 1;
}

void stdPalEffects_Close()
{
    ;
}

int stdPalEffects_NewRequest(int idx)
{
    int v2; // edx
    stdPalEffectRequest *v3; // eax

    if (stdPalEffects_numEffectRequests >= 0x20)
        return -1;

    for (v2 = 0; v2 < 32; v2++)
    {
        if ( !stdPalEffects_aEffects[v2].isValid )
            break;
    }

    _memset(&stdPalEffects_aEffects[v2].effect, 0, sizeof(stdPalEffects_aEffects[v2].effect));
    stdPalEffects_aEffects[v2].effect.fade = 1.0;
    stdPalEffects_aEffects[v2].isValid = 1;
    stdPalEffects_aEffects[v2].idx = idx;
    ++stdPalEffects_numEffectRequests;
    return v2;
}

void stdPalEffects_FreeRequest(uint32_t idx)
{
    if (idx >= 32)
        return;

    if ( stdPalEffects_aEffects[idx].isValid )
    {
        stdPalEffects_aEffects[idx].isValid = 0;
        --stdPalEffects_numEffectRequests;
    }
}

void stdPalEffects_FlushAllEffects()
{
    _memset(stdPalEffects_aEffects, 0, sizeof(stdPalEffectRequest) * 32); // sizeof(stdPalEffects_aEffects)
    stdPalEffects_numEffectRequests = 0;
}

// Added
void stdPalEffects_FlushAllAdds()
{
    for (int i = 0; i < 32; i++)
    {
        _memset(&stdPalEffects_aEffects[i].effect.add, 0, sizeof(stdPalEffects_aEffects[i].effect.add));
    }
}

stdPalEffect* stdPalEffects_GetEffectPointer(int idx)
{
    return &stdPalEffects_aEffects[idx].effect;
}

int stdPalEffects_RefreshPalette()
{
    stdPalEffects_state.bUseFilter = 1;
    stdPalEffects_state.bUseTint = 1;
    stdPalEffects_state.bUseFade = 1;
    stdPalEffects_state.bUseAdd = 1;
    stdPalEffects_state.bEnabled = 1;
    return 1;
}

void stdPalEffects_ResetEffectsState(stdPalEffectsState *effectsState)
{
    _memset(effectsState, 0, sizeof(stdPalEffectsState));
    _memset(&effectsState->effect, 0, sizeof(effectsState->effect));
    effectsState->effect.fade = 1.0;
    effectsState->effect.fade = 1.0;
}

void stdPalEffects_ResetEffect(stdPalEffect *effect)
{
    _memset(effect, 0, sizeof(stdPalEffect));
    effect->fade = 1.0;
}
void stdPalEffects_UpdatePalette(const void* palette)
{
	int8_t bPaletteModified = 0;
	int8_t bFallbackToOriginal = 0;
	int8_t bHasCopiedPalette = 0;

	stdPalEffects_GatherEffects();

	// Skip if no effects are active
	if (!stdPalEffects_state.bUseFilter &&
		!stdPalEffects_state.bUseTint &&
		!stdPalEffects_state.bUseFade &&
		!stdPalEffects_state.bUseAdd)
	{
		return;
	}

	// Filter
	if (stdPalEffects_state.field_4)
	{
		int fx = stdPalEffects_state.effect.filter.x;
		int fy = stdPalEffects_state.effect.filter.y;
		int fz = stdPalEffects_state.effect.filter.z;

		if (fx || fy || fz)
		{
			_memcpy(stdPalEffects_palette, palette, sizeof(stdPalEffects_palette));
			bHasCopiedPalette = 1;

			for (int i = 0; i < 256; i++)
			{
				if (!fx) stdPalEffects_palette[i].r >>= 2;
				if (!fy) stdPalEffects_palette[i].g >>= 2;
				if (!fz) stdPalEffects_palette[i].b >>= 2;
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseFilter = 0;
	}

	// Tint
	if (stdPalEffects_state.field_8)
	{
		float tx = stdPalEffects_state.effect.tint.x;
		float ty = stdPalEffects_state.effect.tint.y;
		float tz = stdPalEffects_state.effect.tint.z;

		if (tx != 0.0f || ty != 0.0f || tz != 0.0f)
		{
			if (!bHasCopiedPalette)
			{
				_memcpy(stdPalEffects_palette, palette, sizeof(stdPalEffects_palette));
				bHasCopiedPalette = 1;
			}

			stdPalEffects_ApplyTint(stdPalEffects_palette, tx, ty, tz);

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseTint = 0;
	}

	// Add
	if (stdPalEffects_state.field_C)
	{
		int addR = stdPalEffects_state.effect.add.x;
		int addG = stdPalEffects_state.effect.add.y;
		int addB = stdPalEffects_state.effect.add.z;

		if (addR || addG || addB)
		{
			if (!bHasCopiedPalette)
			{
				_memcpy(stdPalEffects_palette, palette, sizeof(stdPalEffects_palette));
				bHasCopiedPalette = 0;
			}

			for (int i = 0; i < 256; i++)
			{
				stdPalEffects_palette[i].r = (uint8_t)stdMath_ClampInt((int)stdPalEffects_palette[i].r + addR, 0, 255);
				stdPalEffects_palette[i].g = (uint8_t)stdMath_ClampInt((int)stdPalEffects_palette[i].g + addG, 0, 255);
				stdPalEffects_palette[i].b = (uint8_t)stdMath_ClampInt((int)stdPalEffects_palette[i].b + addB, 0, 255);
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseAdd = 0;
	}

	// Fade
	if (stdPalEffects_state.field_10)
	{
		float fade = stdPalEffects_state.effect.fade;

		if (fade < 1.0f)
		{
			if (!bHasCopiedPalette)
				_memcpy(stdPalEffects_palette, palette, sizeof(stdPalEffects_palette));

			for (int i = 0; i < 256; i++)
			{
				stdPalEffects_palette[i].r = (uint8_t)(stdPalEffects_palette[i].r * fade + 0.5f);
				stdPalEffects_palette[i].g = (uint8_t)(stdPalEffects_palette[i].g * fade + 0.5f);
				stdPalEffects_palette[i].b = (uint8_t)(stdPalEffects_palette[i].b * fade + 0.5f);
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseFade = 0;
	}

	// Apply updated palette
	if (bPaletteModified)
	{
		stdPalEffects_setPalette((uint8_t*)stdPalEffects_palette);
	}
	else if (bFallbackToOriginal)
	{
		stdPalEffects_setPalette((uint8_t*)palette);
	}

	// Clear 'enabled' state if all effects are off
	if (!stdPalEffects_state.effect.filter.x &&
		!stdPalEffects_state.effect.filter.y &&
		!stdPalEffects_state.effect.filter.z &&
		stdPalEffects_state.effect.tint.x == 0.0f &&
		stdPalEffects_state.effect.tint.y == 0.0f &&
		stdPalEffects_state.effect.tint.z == 0.0f &&
		stdPalEffects_state.effect.fade == 1.0f &&
		stdPalEffects_state.effect.add.x == 0 &&
		stdPalEffects_state.effect.add.y == 0 &&
		stdPalEffects_state.effect.add.z == 0)
	{
		stdPalEffects_state.bEnabled = 0;
	}
}


void stdPalEffects_GatherEffects()
{
    uint32_t effectRequestCounter; // ebx
    flex_d_t tintB; // st7
    flex_d_t tintG; // st6
    int addB; // edi
    int addG; // esi
    int addR; // edx
    stdPalEffectRequest* pEffectReq; // ecx
    flex_d_t tintR; // st5
    stdPalEffect palEffect; // [esp+10h] [ebp-28h] BYREF

    effectRequestCounter = 0;
    _memset(&palEffect, 0, sizeof(palEffect));
    palEffect.fade = 1.0;

    if ( stdPalEffects_numEffectRequests )
    {
        tintB = palEffect.tint.z;
        tintG = palEffect.tint.y;
        tintR = palEffect.tint.x;
        addB = palEffect.add.z;
        addG = palEffect.add.y;
        addR = palEffect.add.x;
        pEffectReq = &stdPalEffects_aEffects[0];
        do
        {
            if ( pEffectReq->isValid )
            {
                if ( pEffectReq->effect.filter.x )
                    palEffect.filter.x = 1;

                if ( pEffectReq->effect.filter.y )
                    palEffect.filter.y = 1;

                if ( pEffectReq->effect.filter.z )
                    palEffect.filter.z = 1;

                tintR += pEffectReq->effect.tint.x;
                tintG += pEffectReq->effect.tint.y;
                tintB += pEffectReq->effect.tint.z;
                
                addR += pEffectReq->effect.add.x;
                addG += pEffectReq->effect.add.y;
                addB += pEffectReq->effect.add.z;
                
                if ( pEffectReq->effect.fade < palEffect.fade )
                    palEffect.fade = pEffectReq->effect.fade;
                
                ++effectRequestCounter;
            }
            ++pEffectReq;
        }
        while ( effectRequestCounter < stdPalEffects_numEffectRequests );
        palEffect.tint.z = tintB;
        palEffect.tint.y = tintG;
        palEffect.tint.x = tintR;
        palEffect.add.z = addB;
        palEffect.add.y = addG;
        palEffect.add.x = addR;
    }
    else
    {
        tintB = palEffect.tint.z;
        tintG = palEffect.tint.y;
        addB = palEffect.add.z;
        addG = palEffect.add.y;
        addR = palEffect.add.x;
        tintR = palEffect.tint.x;
    }

    if ( palEffect.filter.x != stdPalEffects_state.effect.filter.x || palEffect.filter.y != stdPalEffects_state.effect.filter.y || palEffect.filter.z != stdPalEffects_state.effect.filter.z )
        stdPalEffects_state.bUseFilter = 1;

    if ( tintR != stdPalEffects_state.effect.tint.x || tintG != stdPalEffects_state.effect.tint.y || tintB != stdPalEffects_state.effect.tint.z )
        stdPalEffects_state.bUseTint = 1;

    if ( addR != stdPalEffects_state.effect.add.x || addG != stdPalEffects_state.effect.add.y || addB != stdPalEffects_state.effect.add.z )
        stdPalEffects_state.bUseAdd = 1;

    if ( palEffect.fade != stdPalEffects_state.effect.fade )
        stdPalEffects_state.bUseFade = 1;

    _memcpy(&stdPalEffects_state.effect, &palEffect, sizeof(stdPalEffects_state.effect));
}

// setunk

void stdPalEffects_SetFilter(int idx, int r, int g, int b)
{
    stdPalEffects_aEffects[idx].effect.filter.x = r;
    stdPalEffects_aEffects[idx].effect.filter.y = g;
    stdPalEffects_aEffects[idx].effect.filter.z = b;
}

void stdPalEffects_SetTint(int idx, flex_t r, flex_t g, flex_t b)
{
    stdPalEffects_aEffects[idx].effect.tint.x = r;
    stdPalEffects_aEffects[idx].effect.tint.y = g;
    stdPalEffects_aEffects[idx].effect.tint.z = b;
}

void stdPalEffects_SetAdd(int idx, int r, int g, int b)
{
    stdPalEffects_aEffects[idx].effect.add.x = r;
    stdPalEffects_aEffects[idx].effect.add.y = g;
    stdPalEffects_aEffects[idx].effect.add.z = b;
}

void stdPalEffects_SetFade(int idx, flex_t fade)
{
    stdPalEffects_aEffects[idx].effect.fade = fade;
}

// ApplyFilter

void stdPalEffects_ApplyTint(rdColor24* palette, flex_t tintR, flex_t tintG, flex_t tintB)
{
	double halfR = tintR * 0.5;
	double halfG = tintG * 0.5;
	double halfB = tintB * 0.5;

	double mulR = tintR - (halfB + halfG);
	double mulG = tintG - (halfR + halfB);
	double mulB = tintB - (halfR + halfG);

	for (int i = 0; i < 256; i++)
	{
		rdColor24* color = &palette[i];

		int r = color->r + (int)((double)color->r * mulR + 0.5);
		int g = color->g + (int)((double)color->g * mulG + 0.5);
		int b = color->b + (int)((double)color->b * mulB + 0.5);

		color->r = (uint8_t)stdMath_ClampInt(r, 0, 255);
		color->g = (uint8_t)stdMath_ClampInt(g, 0, 255);
		color->b = (uint8_t)stdMath_ClampInt(b, 0, 255);
	}
}

// ApplyAdd
// ApplyFade

#ifdef TILE_SW_RASTER

void stdPalEffects_UpdatePaletteCube(const void* palette)
{
	int8_t bPaletteModified = 0;
	int8_t bFallbackToOriginal = 0;
	int8_t bHasCopiedPalette = 0;

	stdPalEffects_GatherEffects();

	// Skip if no effects are active
	if (!stdPalEffects_state.bUseFilter &&
		!stdPalEffects_state.bUseTint &&
		!stdPalEffects_state.bUseFade &&
		!stdPalEffects_state.bUseAdd)
	{
		return;
	}

	// Filter
	if (stdPalEffects_state.field_4)
	{
		int fx = stdPalEffects_state.effect.filter.x;
		int fy = stdPalEffects_state.effect.filter.y;
		int fz = stdPalEffects_state.effect.filter.z;

		if (fx || fy || fz)
		{
			_memcpy(stdPalEffects_paletteCube, palette, sizeof(stdPalEffects_paletteCube));
			bHasCopiedPalette = 1;

			for (int i = 0; i < STDPAL_CUBE_SIZE; i++)
			{
				if (!fx) stdPalEffects_paletteCube[i].r >>= 2;
				if (!fy) stdPalEffects_paletteCube[i].g >>= 2;
				if (!fz) stdPalEffects_paletteCube[i].b >>= 2;
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseFilter = 0;
	}

	// Tint
	if (stdPalEffects_state.field_8)
	{
		float tx = stdPalEffects_state.effect.tint.x;
		float ty = stdPalEffects_state.effect.tint.y;
		float tz = stdPalEffects_state.effect.tint.z;

		if (tx != 0.0f || ty != 0.0f || tz != 0.0f)
		{
			if (!bHasCopiedPalette)
			{
				_memcpy(stdPalEffects_paletteCube, palette, sizeof(stdPalEffects_paletteCube));
				bHasCopiedPalette = 1;
			}

			double halfR = tx * 0.5;
			double halfG = ty * 0.5;
			double halfB = tz * 0.5;

			double mulR = tx - (halfB + halfG);
			double mulG = ty - (halfR + halfB);
			double mulB = tz - (halfR + halfG);

			for (int i = 0; i < STDPAL_CUBE_SIZE; i++)
			{
				rdColor24* color = &stdPalEffects_paletteCube[i];

				int r = color->r + (int)((double)color->r * mulR + 0.5);
				int g = color->g + (int)((double)color->g * mulG + 0.5);
				int b = color->b + (int)((double)color->b * mulB + 0.5);

				color->r = (uint8_t)stdMath_ClampInt(r, 0, 255);
				color->g = (uint8_t)stdMath_ClampInt(g, 0, 255);
				color->b = (uint8_t)stdMath_ClampInt(b, 0, 255);
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseTint = 0;
	}

	// Add
	if (stdPalEffects_state.field_C)
	{
		int addR = stdPalEffects_state.effect.add.x;
		int addG = stdPalEffects_state.effect.add.y;
		int addB = stdPalEffects_state.effect.add.z;

		if (addR || addG || addB)
		{
			if (!bHasCopiedPalette)
			{
				_memcpy(stdPalEffects_paletteCube, palette, sizeof(stdPalEffects_paletteCube));
				bHasCopiedPalette = 0;
			}

			for (int i = 0; i < STDPAL_CUBE_SIZE; i++)
			{
				stdPalEffects_paletteCube[i].r = (uint8_t)stdMath_ClampInt((int)stdPalEffects_paletteCube[i].r + addR, 0, 255);
				stdPalEffects_paletteCube[i].g = (uint8_t)stdMath_ClampInt((int)stdPalEffects_paletteCube[i].g + addG, 0, 255);
				stdPalEffects_paletteCube[i].b = (uint8_t)stdMath_ClampInt((int)stdPalEffects_paletteCube[i].b + addB, 0, 255);
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseAdd = 0;
	}

	// Fade
	if (stdPalEffects_state.field_10)
	{
		float fade = stdPalEffects_state.effect.fade;

		if (fade < 1.0f)
		{
			if (!bHasCopiedPalette)
				_memcpy(stdPalEffects_paletteCube, palette, sizeof(stdPalEffects_paletteCube));

			for (int i = 0; i < STDPAL_CUBE_SIZE; i++)
			{
				stdPalEffects_paletteCube[i].r = (uint8_t)(stdPalEffects_paletteCube[i].r * fade + 0.5f);
				stdPalEffects_paletteCube[i].g = (uint8_t)(stdPalEffects_paletteCube[i].g * fade + 0.5f);
				stdPalEffects_paletteCube[i].b = (uint8_t)(stdPalEffects_paletteCube[i].b * fade + 0.5f);
			}

			bPaletteModified = 1;
			stdPalEffects_state.bEnabled = 1;
		}
		else if (stdPalEffects_state.bEnabled)
		{
			bFallbackToOriginal = 1;
		}

		stdPalEffects_state.bUseFade = 0;
	}

	// Apply updated palette
	if (bPaletteModified)
	{
		stdPalEffects_setPaletteCube((uint8_t*)stdPalEffects_paletteCube);
	}
	else if (bFallbackToOriginal)
	{
		stdPalEffects_setPaletteCube((uint8_t*)palette);
	}

	// Clear 'enabled' state if all effects are off
	if (!stdPalEffects_state.effect.filter.x &&
		!stdPalEffects_state.effect.filter.y &&
		!stdPalEffects_state.effect.filter.z &&
		stdPalEffects_state.effect.tint.x == 0.0f &&
		stdPalEffects_state.effect.tint.y == 0.0f &&
		stdPalEffects_state.effect.tint.z == 0.0f &&
		stdPalEffects_state.effect.fade == 1.0f &&
		stdPalEffects_state.effect.add.x == 0 &&
		stdPalEffects_state.effect.add.y == 0 &&
		stdPalEffects_state.effect.add.z == 0)
	{
		stdPalEffects_state.bEnabled = 0;
	}
}

#endif

void stdPalEffects_SetPaletteFunc(stdPalEffectSetPaletteFunc_t func)
{
    stdPalEffects_setPalette = func;
}

void stdPalEffects_SetStateBools(int a1, int a2, int a3, int a4)
{
    stdPalEffects_state.field_4 = a1;
    stdPalEffects_state.field_8 = a2;
    stdPalEffects_state.field_C = a3;
    stdPalEffects_state.field_10 = a4;
}

void stdPalEffects_ApplyFilter(rdColor24 *aPalette, int filterR, int filterG, int filterB)
{
    uint8_t *p = (uint8_t *)aPalette;
    for (int i = 0; i < 0x300; i += 3)
    {
        if ( filterR == 0 )
            p[i] = p[i] >> 2;
        if ( filterG == 0 )
            p[i + 1] = p[i + 1] >> 2;
        if ( filterB == 0 )
            p[i + 2] = p[i + 2] >> 2;
    }
}

void stdPalEffects_ApplyAdd(rdColor24 *aPalette, int addR, int addG, int addB)
{
    uint8_t *p = (uint8_t *)aPalette;
    for (int i = 0; i < 0x300; i += 3)
    {
        int r = addR + p[i];
        if ( r < 0 ) r = 0;
        else if ( r > 255 ) r = 255;
        p[i] = (uint8_t)r;

        int g = addG + p[i + 1];
        if ( g < 0 ) g = 0;
        else if ( g > 255 ) g = 255;
        p[i + 1] = (uint8_t)g;

        int b = addB + p[i + 2];
        if ( b < 0 ) b = 0;
        else if ( b > 255 ) b = 255;
        p[i + 2] = (uint8_t)b;
    }
}

void stdPalEffects_ApplyFade(rdColor24 *aPalette, flex_t fade)
{
    uint8_t *p = (uint8_t *)aPalette;
    for (int i = 0; i < 0x300; i += 3)
    {
        p[i]     = (__int64)((flex_d_t)p[i] * fade);
        p[i + 1] = (__int64)((flex_d_t)p[i + 1] * fade);
        p[i + 2] = (__int64)((flex_d_t)p[i + 2] * fade);
    }
}
