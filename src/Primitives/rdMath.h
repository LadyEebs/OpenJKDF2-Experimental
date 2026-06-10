#ifndef _RDMATH_H
#define _RDMATH_H

#include "Primitives/rdVector.h"

#define rdMath_CalcSurfaceNormal_ADDR (0x0046D250)
#define rdMath_DistancePointToPlane_ADDR (0x0046D3C0)
#define rdMath_DeltaAngleNormalizedAbs_ADDR (0x0046D400)
#define rdMath_DeltaAngleNormalized_ADDR (0x0046D450)
#define rdMath_ClampVector_ADDR (0x0046D570)
#define rdMath_PointsCollinear_ADDR (0x0046D600)

MATH_FUNC FAST_FUNC flex_t rdMath_DistancePointToPlane(const rdVector3 *light, const rdVector3 *normal, const rdVector3 *vertex);
MATH_FUNC void rdMath_CalcSurfaceNormal(rdVector3 *out, rdVector3 *edge1, rdVector3 *edge2, rdVector3 *edge3);
MATH_FUNC flex_t rdMath_DeltaAngleNormalizedAbs(rdVector3 *a1, rdVector3 *a2);
MATH_FUNC flex_t rdMath_DeltaAngleNormalized(rdVector3 *a1, rdVector3 *a2, rdVector3 *a3);
MATH_FUNC void rdMath_ClampVector(rdVector3* out, flex_t minVal);
MATH_FUNC int rdMath_PointsCollinear(rdVector3 *a1, rdVector3 *a2, rdVector3 *a3);

MATH_FUNC void rdMath_ClampVectorRange(rdVector3* out, flex_t minVal, flex_t maxVal);
MATH_FUNC flex_t rdMath_clampf(flex_t d, flex_t min, flex_t max);

int rdMath_IntersectPointLine(const rdVector3* pPoint, const rdVector3* pStart, const rdVector3* pEnd);
int rdMath_IntersectLineSegments(const rdVector3* pStartA, const rdVector3* pEndA, const rdVector3* pStartB, const rdVector3* pEndB, rdVector3* pOut);

int rdMath_IntersectAABB_Sphere(rdVector3* minb, rdVector3* maxb, rdVector3* center, float radius);
int rdMath_IntersectAABB_OBB(rdVector3* minb, rdVector3* maxb, const rdMatrix44* mat);
int rdMath_IntersectAABB_Cone(rdVector3* minb, rdVector3* maxb, rdVector3* position, rdVector3* direction, float angle, float radius);

#endif // _RDMATH_H
