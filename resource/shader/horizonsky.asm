ps.1.0

# r1 = xy * (0.5, -0.5) * c0.x
mul r1, sv:xy, (0.5/-0.5) fmt:half2
mul r1, r1[fmt:half2], c0.x fmt:half2

# r2.xy = r1.yx * (c0.z, -c0.z)
mul r2.x, r1.y, -c0.z fmt:half2
mul r2.y, r1.x,  c0.z fmt:half2

# r1.xy = r1.xy * c0.y + r2.xy
mad r1, r1[fmt:half2], c0.y, r2[fmt:half2] fmt:half2

# r1.xy = r1.xy / texinfo(t0, 0).xy
texinfo r2, t0, 0 fmt:half2
div r1, r1[fmt:half2], r2[fmt:half2] fmt:half2

# texadd(t0, r1, c1)
texadd r0, t0, r1[fmt:half2], c1.xy

# sky uses full bright for emissive
mul r1, r0, mat:glow
mul r1, r1, 0.25
