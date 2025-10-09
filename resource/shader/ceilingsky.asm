ps.1.0

# plane intersection dot(c - p, n) / dot(d, n)
# r1 = c0.w / dot(vidr.xyz, c0.xyz)
dp3 r1, sv:wvdir, c0 fmt:float
div r1, c0.w, r1[fmt:float] fmt:float precise

# r1 = r2 > 0 && r2 < 1000 ? r1 : 1000
#sub r2, r1[fmt:float], 1000.0 fmt:float # r2 = r1 - 1000
#cmp r1, r1[fmt:float], r1[fmt:float], 1000.0 fmt:float # r1 = r1 > 0 ? r1 : 1000
#cmp r1, r2[fmt:float], r1[fmt:float], 1000.0 fmt:float # r1 = r1 < 1000 ? r1 : 1000

# r1 = vdir * r1 * 16.0
mul r1, sv:wvdir, r1[fmt:float] fmt:half2
mul r1, r1[fmt:half2], 16.0 fmt:half2

# r1.xy = r1.xy / texinfo(t0, 0).xy
texinfo r2, t0, 0 fmt:half2
div r1, r1[fmt:half2], r2[fmt:half2] fmt:half2

# texadd(t0, r1, c1)
texadd r0, t0, r1[fmt:half2], c1.xy

# sky uses full bright for emissive
mul r1, r0, mat:glow
mul r1, r1, 0.25
