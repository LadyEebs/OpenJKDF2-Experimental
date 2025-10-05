#include "rdVector.h"

#include <math.h>
#include "rdMatrix.h"
#include "General/stdMath.h"
#include "Primitives/rdMath.h"

#ifdef TARGET_TWL
#include <nds.h>
#endif

const rdVector2 rdroid_zeroVector2 = {0.0, 0.0};
const rdVector3 rdroid_zeroVector3 = {0.0,0.0,0.0};
const rdVector3 rdroid_xVector3 = {1.0,0.0,0.0};
const rdVector3 rdroid_yVector3 = {0.0,1.0,0.0};
const rdVector3 rdroid_zVector3 = {0.0,0.0,1.0};

rdVector2* rdVector_Set2(rdVector2* v, flex_t x, flex_t y)
{
    v->x = x;
    v->y = y;
    return v;
}

rdVector3* rdVector_Set3(rdVector3* v, flex_t x, flex_t y, flex_t z)
{
    v->x = x;
    v->y = y;
    v->z = z;
    return v;
}

rdVector4* rdVector_Set4(rdVector4* v, flex_t x, flex_t y, flex_t z, flex_t w)
{
    v->x = x;
    v->y = y;
    v->z = z;
    v->w = w;
    return v;
}

void rdVector_Copy2(rdVector2* v1, const rdVector2* v2)
{
    v1->x = v2->x;
    v1->y = v2->y;
}

void rdVector_Copy3(rdVector3* v1, const rdVector3* v2)
{
    v1->x = v2->x;
    v1->y = v2->y;
    v1->z = v2->z;
}

void rdVector_Copy4(rdVector4* v1, const rdVector4* v2)
{
    v1->x = v2->x;
    v1->y = v2->y;
    v1->z = v2->z;
    v1->w = v2->w;
}

rdVector2* rdVector_Neg2(rdVector2* v1, const rdVector2* v2)
{
    v1->x = -v2->x;
    v1->y = -v2->y;
    return v1;
}

rdVector3* rdVector_Neg3(rdVector3* v1, const rdVector3* v2)
{
    v1->x = -v2->x;
    v1->y = -v2->y;
    v1->z = -v2->z;
    return v1;
}

rdVector4* rdVector_Neg4(rdVector4* v1, const rdVector4* v2)
{
    v1->x = -v2->x;
    v1->y = -v2->y;
    v1->z = -v2->z;
    v1->w = -v2->w;
    return v1;
}

rdVector2* rdVector_Neg2Acc(rdVector2* v1)
{
    v1->x = -v1->x;
    v1->y = -v1->y;
    return v1;
}

rdVector3* rdVector_Neg3Acc(rdVector3* v1)
{
    v1->x = -v1->x;
    v1->y = -v1->y;
    v1->z = -v1->z;
    return v1;
}

rdVector4* rdVector_Neg4Acc(rdVector4* v1)
{
    v1->x = -v1->x;
    v1->y = -v1->y;
    v1->z = -v1->z;
    v1->w = -v1->w;
    return v1;
}

rdVector2* rdVector_Add2(rdVector2* v1, const rdVector2* v2, const rdVector2* v3)
{
    v1->x = v2->x + v3->x;
    v1->y = v2->y + v3->y;
    return v1;
}

rdVector3* rdVector_Add3(rdVector3* v1, const rdVector3* v2, const rdVector3* v3)
{
    v1->x = v2->x + v3->x;
    v1->y = v2->y + v3->y;
    v1->z = v2->z + v3->z;
    return v1;
}

rdVector4* rdVector_Add4(rdVector4* v1, const rdVector4* v2, const rdVector4* v3)
{
    v1->x = v2->x + v3->x;
    v1->y = v2->y + v3->y;
    v1->z = v2->z + v3->z;
    v1->w = v2->w + v3->w;
    return v1;
}

rdVector2* rdVector_Add2Acc(rdVector2* v1, const rdVector2* v2)
{
    v1->x = v2->x + v1->x;
    v1->y = v2->y + v1->y;
    return v1;
}

rdVector3* rdVector_Add3Acc(rdVector3* v1, const rdVector3* v2)
{
    v1->x = v2->x + v1->x;
    v1->y = v2->y + v1->y;
    v1->z = v2->z + v1->z;
    return v1;
}

rdVector4* rdVector_Add4Acc(rdVector4* v1, const rdVector4* v2)
{
    v1->x = v2->x + v1->x;
    v1->y = v2->y + v1->y;
    v1->z = v2->z + v1->z;
    v1->w = v2->w + v1->w;
    return v1;
}


rdVector2* rdVector_Sub2(rdVector2* v1, const rdVector2* v2, const rdVector2* v3)
{
    v1->x = v2->x - v3->x;
    v1->y = v2->y - v3->y;
    return v1;
}

rdVector3* rdVector_Sub3(rdVector3* v1, const rdVector3* v2, const rdVector3* v3)
{
    v1->x = v2->x - v3->x;
    v1->y = v2->y - v3->y;
    v1->z = v2->z - v3->z;
    return v1;
}

rdVector4* rdVector_Sub4(rdVector4* v1, const rdVector4* v2, const rdVector4* v3)
{
    v1->x = v2->x - v3->x;
    v1->y = v2->y - v3->y;
    v1->z = v2->z - v3->z;
    v1->w = v2->w - v3->w;
    return v1;
}

rdVector2* rdVector_Sub2Acc(rdVector2* v1, const rdVector2* v2)
{
    v1->x = -v2->x + v1->x;
    v1->y = -v2->y + v1->y;
    return v1;
}

rdVector3* rdVector_Sub3Acc(rdVector3* v1, const rdVector3* v2)
{
    v1->x = -v2->x + v1->x;
    v1->y = -v2->y + v1->y;
    v1->z = -v2->z + v1->z;
    return v1;
}

rdVector4* rdVector_Sub4Acc(rdVector4* v1, const rdVector4* v2)
{
    v1->x = -v2->x + v1->x;
    v1->y = -v2->y + v1->y;
    v1->z = -v2->z + v1->z;
    v1->w = -v2->w + v1->w;
    return v1;
}

flex_t rdVector_Dot2(const rdVector2* v1, const rdVector2* v2)
{
    return (v1->x * v2->x) + (v1->y * v2->y);
}

flex_t rdVector_Dot3(const rdVector3* v1, const rdVector3* v2)
{
    return (v1->x * v2->x) + (v1->y * v2->y) + (v1->z * v2->z);
}

flex_t rdVector_Dot4(const rdVector4* v1, const rdVector4* v2)
{
    return (v1->x * v2->x) + (v1->y * v2->y) + (v1->z * v2->z) + (v1->w * v2->w);
}

void rdVector_Cross3(rdVector3 *v1, const rdVector3 *v2, const rdVector3 *v3)
{
    v1->x = (v3->z * v2->y) - (v2->z * v3->y);
    v1->y = (v2->z * v3->x) - (v3->z * v2->x);
    v1->z = (v3->y * v2->x) - (v2->y * v3->x);
}

void rdVector_Cross3Acc(rdVector3 *v1, const rdVector3 *v2)
{
	rdVector3 tmp = *v1;
    v1->x = (v2->z * tmp.y) - (tmp.z * v2->y);
    v1->y = (tmp.z * v2->x) - (v2->z * tmp.x);
    v1->z = (v2->y * tmp.x) - (tmp.y * v2->x);
}

flex_t rdVector_Len2(const rdVector2* v)
{
    return stdMath_Sqrt(rdVector_Dot2(v,v));
}

flex_t rdVector_Len3(const rdVector3* v)
{
#ifdef TARGET_TWL
    int64_t val = ((int64_t)v->x.to_raw()*v->x.to_raw())+((int64_t)v->y.to_raw()*v->y.to_raw())+((int64_t)v->z.to_raw()*v->z.to_raw());
    return sqrt64fixed_mine_2(val);
#else
    return stdMath_Sqrt(rdVector_Dot3(v,v));
#endif
}

flex_t rdVector_Len4(const rdVector4* v)
{
    return stdMath_Sqrt(rdVector_Dot4(v,v));
}

flex_t rdVector_Normalize2(rdVector2 *v1, const rdVector2 *v2)
{
    flex_t len = rdVector_Len2(v2);
    if (len == 0.0)
    {
        v1->x = v2->x;
        v1->y = v2->y;
    }
    else
    {
        v1->x = v2->x / len;
        v1->y = v2->y / len;
    }
    return len;
}

flex_t rdVector_Normalize3(rdVector3 *v1, const rdVector3 *v2)
{
#ifdef TARGET_TWL
#if 0
    static int last_frame = 0;
    static int num_sqrts = 0;
    extern int std3D_frameCount;
    if (last_frame != std3D_frameCount) {
        printf("norms %d\n", num_sqrts);
        last_frame = std3D_frameCount;
        num_sqrts = 0;
    }
    num_sqrts += 1;
#endif

    flex_t len = sqrt64fixed_mine_2(((int64_t)v2->x.to_raw()*v2->x.to_raw())+((int64_t)v2->y.to_raw()*v2->y.to_raw())+((int64_t)v2->z.to_raw()*v2->z.to_raw()));
    //flex_t len = rdVector_Len3(v2);
    if (len == 0.0)
    {
        v1->x = v2->x;
        v1->y = v2->y;
        v1->z = v2->z;
    }
    else
    {
        v1->x = divflex_mine(v2->x, len);
        v1->y = divflex_mine(v2->y, len);
        v1->z = divflex_mine(v2->z, len);

        //v1->x = f32toflex(divf32_mine(flextof32(v2->x), flextof32(len)));
        //v1->y = f32toflex(divf32_mine(flextof32(v2->y), flextof32(len)));
        //v1->z = f32toflex(divf32_mine(flextof32(v2->z), flextof32(len)));
    }
    return len;
#else
    flex_t len = rdVector_Len3(v2);
    if (len == 0.0)
    {
        v1->x = v2->x;
        v1->y = v2->y;
        v1->z = v2->z;
    }
    else
    {
        v1->x = v2->x / len;
        v1->y = v2->y / len;
        v1->z = v2->z / len;
    }
    return len;
#endif
}

flex_t rdVector_Normalize3Quick(rdVector3 *v1, const rdVector3 *v2)
{
    flex_t series_1;
    flex_t series_2;
    flex_t series_3;

    flex_t x_pos = (v2->x >= 0.0) ? v2->x : -v2->x;
    flex_t y_pos = (v2->y >= 0.0) ? v2->y : -v2->y;
    flex_t z_pos = (v2->z >= 0.0) ? v2->z : -v2->z;

    series_1 = x_pos;
    series_2 = z_pos;
    series_3 = y_pos;

    if (z_pos <= y_pos)
    {
        if (x_pos < y_pos)
        {
            series_3 = x_pos;
            series_1 = y_pos;
            if (z_pos > x_pos)
            {
                series_2 = x_pos;
                series_3 = z_pos;
            }
        }
    }
    else if (z_pos <= x_pos)
    {
        series_2 = y_pos;
        series_3 = z_pos;
    }
    else
    {
        series_2 = x_pos;
        series_1 = z_pos;
        if (y_pos < x_pos)
        {
            series_2 = y_pos;
            series_3 = x_pos;
        }
    }

    flex_t len = ((0.34375 * series_3) + (0.25 * series_2) + series_1);
    flex_t len_recip = 1.0 / len;
    v1->x = v2->x * len_recip;
    v1->y = v2->y * len_recip;
    v1->z = v2->z * len_recip;
    return len;
}

flex_t rdVector_Normalize4(rdVector4 *v1, const rdVector4 *v2)
{
    flex_t len = rdVector_Len4(v2);
    if (len == 0.0)
    {
        v1->x = v2->x;
        v1->y = v2->y;
        v1->z = v2->z;
        v1->w = v2->w;
    }
    else
    {
        v1->x = v2->x / len;
        v1->y = v2->y / len;
        v1->z = v2->z / len;
        v1->w = v2->w / len;
    }
    return len;
}

flex_t rdVector_Normalize2Acc(rdVector2 *v1)
{
    flex_t len = rdVector_Len2(v1);
    if (len == 0.0)
    {
        v1->x = v1->x;
        v1->y = v1->y;
    }
    else
    {
        v1->x = v1->x / len;
        v1->y = v1->y / len;
    }
    return len;
}

flex_t rdVector_Normalize3Acc(rdVector3 *v1)
{
    flex_t len = rdVector_Len3(v1);
    if (len == 0.0)
    {
        v1->x = v1->x;
        v1->y = v1->y;
        v1->z = v1->z;
    }
    else
    {
        v1->x = v1->x / len;
        v1->y = v1->y / len;
        v1->z = v1->z / len;
    }
    return len;
}

flex_t rdVector_Normalize3QuickAcc(rdVector3 *v1)
{
    flex_t series_1;
    flex_t series_2;
    flex_t series_3;

    flex_t x_pos = (v1->x >= 0.0) ? v1->x : -v1->x;
    flex_t y_pos = (v1->y >= 0.0) ? v1->y : -v1->y;
    flex_t z_pos = (v1->z >= 0.0) ? v1->z : -v1->z;

    series_1 = x_pos;
    series_2 = z_pos;
    series_3 = y_pos;

    if (z_pos <= y_pos)
    {
        if (x_pos < y_pos)
        {
            series_3 = x_pos;
            series_1 = y_pos;
            if (z_pos > x_pos)
            {
                series_2 = x_pos;
                series_3 = z_pos;
            }
        }
    }
    else if (z_pos <= x_pos)
    {
        series_2 = y_pos;
        series_3 = z_pos;
    }
    else
    {
        series_2 = x_pos;
        series_1 = z_pos;
        if (y_pos < x_pos)
        {
            series_2 = y_pos;
            series_3 = x_pos;
        }
    }

    flex_t len = ((0.34375 * series_3) + (0.25 * series_2) + series_1);
    // Added: prevent div 0
    if (len == 0.0) {
        len = 0.00000001;
    }
    flex_t len_recip = 1.0 / len;
    v1->x = v1->x * len_recip;
    v1->y = v1->y * len_recip;
    v1->z = v1->z * len_recip;
    return len;
}

flex_t rdVector_Normalize4Acc(rdVector4 *v1)
{
    flex_t len = rdVector_Len4(v1);
    if (len == 0.0)
    {
        v1->x = v1->x;
        v1->y = v1->y;
        v1->z = v1->z;
        v1->w = v1->w;
    }
    else
    {
        v1->x = v1->x / len;
        v1->y = v1->y / len;
        v1->z = v1->z / len;
        v1->w = v1->w / len;
    }
    return len;
}

rdVector2* rdVector_Scale2(rdVector2 *v1, const rdVector2 *v2, flex_t scale)
{
    v1->x = v2->x * scale;
    v1->y = v2->y * scale;
    return v1;
}

rdVector3* rdVector_Scale3(rdVector3 *v1, const rdVector3 *v2, flex_t scale)
{
    v1->x = v2->x * scale;
    v1->y = v2->y * scale;
    v1->z = v2->z * scale;
    return v1;
}

rdVector4* rdVector_Scale4(rdVector4 *v1, const rdVector4 *v2, flex_t scale)
{
    v1->x = v2->x * scale;
    v1->y = v2->y * scale;
    v1->z = v2->z * scale;
    v1->w = v2->w * scale;
    return v1;
}

rdVector2* rdVector_Scale2Acc(rdVector2 *v1, flex_t scale)
{
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    return v1;
}

rdVector3* rdVector_Scale3Acc(rdVector3 *v1, flex_t scale)
{
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    v1->z = v1->z * scale;
    return v1;
}

rdVector4* rdVector_Scale4Acc(rdVector4 *v1, flex_t scale)
{
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    v1->z = v1->z * scale;
    v1->w = v1->w * scale;
    return v1;
}

rdVector2* rdVector_InvScale2(rdVector2 *v1, const rdVector2 *v2, flex_t scale)
{
    v1->x = v2->x / scale;
    v1->y = v2->y / scale;
    return v1;
}

rdVector3* rdVector_InvScale3(rdVector3 *v1, const rdVector3 *v2, flex_t scale)
{
    v1->x = v2->x / scale;
    v1->y = v2->y / scale;
    v1->z = v2->z / scale;
    return v1;
}

rdVector4* rdVector_InvScale4(rdVector4 *v1, const rdVector4 *v2, flex_t scale)
{
    v1->x = v2->x / scale;
    v1->y = v2->y / scale;
    v1->z = v2->z / scale;
    v1->w = v2->w / scale;
    return v1;
}

rdVector2* rdVector_InvScale2Acc(rdVector2 *v1, flex_t scale)
{
    v1->x = v1->x / scale;
    v1->y = v1->y / scale;
    return v1;
}

rdVector3* rdVector_InvScale3Acc(rdVector3 *v1, flex_t scale)
{
    v1->x = v1->x / scale;
    v1->y = v1->y / scale;
    v1->z = v1->z / scale;
    return v1;
}

rdVector4* rdVector_InvScale4Acc(rdVector4 *v1, flex_t scale)
{
    v1->x = v1->x / scale;
    v1->y = v1->y / scale;
    v1->z = v1->z / scale;
    v1->w = v1->w / scale;
    return v1;
}

void rdVector_Rotate3(rdVector3 *out, const rdVector3 *in, const rdVector3 *vAngs)
{
    rdMatrix34 tmp;

    rdMatrix_BuildRotate34(&tmp, vAngs);
    rdMatrix_TransformVector34(out, in, &tmp);
}

void rdVector_Rotate3Acc(rdVector3 *out, const rdVector3 *vAngs)
{
    rdMatrix34 tmp;

    rdMatrix_BuildRotate34(&tmp, vAngs);
    rdMatrix_TransformVector34Acc(out, &tmp);
}

void rdVector_ExtractAngle(const rdVector3 *v1, rdVector3 *out)
{
    out->x = stdMath_ArcSin3(v1->z);
    out->y = stdMath_ArcTan4(v1->y, v1->x);
    out->z = 0.0;
}

// Added
flex_t rdVector_Dist3(const rdVector3 *v1, const rdVector3 *v2)
{
    rdVector3 tmp;
    
    rdVector_Sub3(&tmp, v1, v2);
    return rdVector_Len3(&tmp);
}

// Added
flex_t rdVector_DistSquared3(const rdVector3 *v1, const rdVector3 *v2)
{
    rdVector3 tmp;
    
    rdVector_Sub3(&tmp, v1, v2);
    return rdVector_Dot3(&tmp,&tmp);
}

rdVector3* rdVector_MultAcc3(rdVector3 *v1, const rdVector3 *v2, flex_t scale)
{
    v1->x += v2->x * scale;
    v1->y += v2->y * scale;
    v1->z += v2->z * scale;
    return v1;
}

void rdVector_Zero3(rdVector3 *v)
{
    rdVector_Copy3(v, &rdroid_zeroVector3);
}

void rdVector_Zero2(rdVector2 *v)
{
    rdVector_Copy2(v, &rdroid_zeroVector2);
}

int rdVector_IsZero3(rdVector3* v)
{
    return (v->x == 0.0 && v->y == 0.0 && v->z == 0.0);
}

int rdVector_IsNotZero3(rdVector3* v)
{
	return (v->x != 0.0 || v->y != 0.0 || v->z != 0.0);
}

flex_t rdVector_NormalDot(const rdVector3* v1, const rdVector3* v2, const rdVector3* norm)
{
    return rdMath_DistancePointToPlane(v1, norm, v2);
}

void rdVector_AbsRound3(rdVector3* v)
{
    v->x = stdMath_ClipPrecision(stdMath_Fabs(v->x));
    v->y = stdMath_ClipPrecision(stdMath_Fabs(v->y));
    v->z = stdMath_ClipPrecision(stdMath_Fabs(v->z));
}

void rdVector_ClipPrecision3(rdVector3* v)
{
    v->x = stdMath_ClipPrecision(v->x);
    v->y = stdMath_ClipPrecision(v->y);
    v->z = stdMath_ClipPrecision(v->z);
}

void rdVector_NormalizeAngleAcute3(rdVector3* v)
{
    v->x = stdMath_NormalizeAngleAcute(v->x);
    v->y = stdMath_NormalizeAngleAcute(v->y);
    v->z = stdMath_NormalizeAngleAcute(v->z);
}

void rdVector_NormalizeDeltaAngle3(rdVector3* v, const rdVector3* a, const rdVector3* b)
{
	v->x = stdMath_NormalizeDeltaAngle(a->x, b->x);
	v->y = stdMath_NormalizeDeltaAngle(a->y, b->y);
	v->z = stdMath_NormalizeDeltaAngle(a->z, b->z);
}

void rdVector_ClampRange3(rdVector3* v, flex_t minVal, flex_t maxVal)
{
    if (v->x < minVal)
    {
        v->x = minVal;
    }
    
    if (v->x > maxVal)
    {
        v->x = maxVal;
    }
    
    if (v->y < minVal)
    {
        v->y = minVal;
    }
    
    if (v->y > maxVal)
    {
        v->y = maxVal;
    }
    
    if (v->z < minVal)
    {
        v->z = minVal;
    }
    
    if (v->z > maxVal)
    {
        v->z = maxVal;
    }
}

void rdVector_ClampValue3(rdVector3* v, flex_t val)
{
    flex_t valAbs = val;
    if (valAbs < 0.0)
    {
        valAbs = -valAbs;
    }

    rdVector_ClampRange3(v, -valAbs, valAbs);
}

void rdVector_Clamp3(rdVector3* v, const rdVector3* minVal, const rdVector3* maxVal)
{
	v->x = stdMath_Clamp(v->x, minVal->x, maxVal->x);
	v->y = stdMath_Clamp(v->y, minVal->y, maxVal->y);
	v->z = stdMath_Clamp(v->z, minVal->z, maxVal->z);
}

void rdVector_Reflect3(rdVector3* v, const rdVector3* incidentVec, const rdVector3* normal)
{
	float dp = 2.f * rdVector_Dot3(incidentVec, normal);
	v->x = incidentVec->x - dp * normal->x;
	v->y = incidentVec->y - dp * normal->y;
	v->z = incidentVec->z - dp * normal->z;
}

void rdVector_Average3(rdVector3* out, rdVector3* a, rdVector3* b, rdVector3* c)
{
	out->x = (a->x + b->x + c->x) / 3.0f;
	out->y = (a->y + b->y + c->y) / 3.0f;
	out->z = (a->z + b->z + c->z) / 3.0f;
}

void rdVector_Lerp3(rdVector3* out, rdVector3* a, rdVector3* b, float f)
{
	out->x = stdMath_Lerp(a->x, b->x, f);
	out->y = stdMath_Lerp(a->y, b->y, f);
	out->z = stdMath_Lerp(a->z, b->z, f);
}

void rdVector_Project3(rdVector3* out, rdVector3* p, rdVector3* o, rdVector3* n)
{
	rdVector3 v;
	rdVector_Sub3(&v, p, o);

	float dist = rdVector_Dot3(&v, n);

	rdVector_Copy3(out, p);
	rdVector_MultAcc3(out, n, -dist);
}

void rdVector_ProjectDir3(rdVector3* out, rdVector3* v, rdVector3* n)
{
	float dist = rdVector_Dot3(v, n);
	rdVector_Scale3(out, n, dist);
}

int rdVector_Compare3(const rdVector3* a, const rdVector3* b)
{
	return memcmp(a, b, sizeof(rdVector3));
}

int rdVector_Compare4(const rdVector4* a, const rdVector4* b)
{
	return memcmp(a, b, sizeof(rdVector4));
}

void rdVector_ToPYR(rdVector3* pyr, const rdVector3* v)
{
	static const rdVector3 pitchAxisA = { 1, 0, 0 };  // Local X-axis
	static const rdVector3 yawAxisA = { 0, 0, 1 };  // Local Z-axis
	static const rdVector3 rollAxisA = { 0, 1, 0 };  // Local Y-axis

	pyr->x = rdVector_Dot3(v, &pitchAxisA) * (180.0f / M_PI);  // Pitch
	pyr->y = rdVector_Dot3(v, &yawAxisA) * (180.0f / M_PI);   // Yaw
	pyr->z = rdVector_Dot3(v, &rollAxisA) * (180.0f / M_PI);   // Roll
}

void rdVector_FromPYR(rdVector3* v, const rdVector3* pyr)
{
	// Local angular velocity in axis terms
	rdVector3 pitchAxis = { 1, 0, 0 };
	rdVector3 yawAxis = { 0, 0, 1 };
	rdVector3 rollAxis = { 0, 1, 0 };

	// Scale by respective angular velocities
	rdVector_Scale3(&pitchAxis, &pitchAxis, pyr->x * (M_PI / 180.0f)); // Convert to radians
	rdVector_Scale3(&yawAxis, &yawAxis, pyr->y * (M_PI / 180.0f));     // Convert to radians
	rdVector_Scale3(&rollAxis, &rollAxis, pyr->z * (M_PI / 180.0f));   // Convert to radians

	rdVector_Add3(v, &pitchAxis, &rollAxis);
	rdVector_Add3Acc(v, &yawAxis);
}

void rdVector_Perpendicular3(rdVector3* perp, const rdVector3* v)
{
	rdVector3 arbitraryVec = { 1.0f, 0.0f, 0.0f };
	if (stdMath_Fabs(rdVector_Dot3(v, &arbitraryVec)) > 0.99f)
		arbitraryVec = (rdVector3){ 0.0f, 1.0f, 0.0f };
	rdVector_Cross3(perp, v, &arbitraryVec);
	rdVector_Normalize3Acc(perp);
}

void rdVector_Copy3To4(rdVector4* out, rdVector3* in)
{
	out->x = in->x;
	out->y = in->y;
	out->z = in->z;
}

void rdVector_Copy4To3(rdVector3* out, rdVector4* in)
{
	out->x = in->x;
	out->y = in->y;
	out->z = in->z;
}

rdVector3 rdVector_Min3(rdVector3* a, rdVector3* b)
{
	rdVector3 c;
	c.x = stdMath_Min(a->x, b->x);
	c.y = stdMath_Min(a->y, b->y);
	c.z = stdMath_Min(a->z, b->z);
	return c;
}

rdVector3 rdVector_Max3(rdVector3* a, rdVector3* b)
{
	rdVector3 c;
	c.x = stdMath_Max(a->x, b->x);
	c.y = stdMath_Max(a->y, b->y);
	c.z = stdMath_Max(a->z, b->z);
	return c;
}


rdVector3* rdVector_Min3Acc(rdVector3* out, rdVector3* in)
{
	out->x = stdMath_Min(out->x, in->x);
	out->y = stdMath_Min(out->y, in->y);
	out->z = stdMath_Min(out->z, in->z);
	return out;
}
 
rdVector3* rdVector_Max3Acc(rdVector3* out, rdVector3* in)
{
	out->x = stdMath_Max(out->x, in->x);
	out->y = stdMath_Max(out->y, in->y);
	out->z = stdMath_Max(out->z, in->z);
	return out;
}