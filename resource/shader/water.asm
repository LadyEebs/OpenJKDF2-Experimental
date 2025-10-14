ps.1.0

# layer 1
tex r0, t0, t0 # sample offset 0
tex r1, t0, t1 # sample offset 1

# combine the samples
add r1, r0, r1 div:2 # (r0 + r1) / 2

# output color
mov r0, lum(r0)

# layer 2
tex r2, tex0, t2
tex r3, tex0, t3
add r3, r2, r3 div:2 # (r2 + r3) / 2
add r2, lum(r2), lum(r1) fmt:float # add r1 to r2 

# base
mov r0.a, 0.5 # set alpha to default
sub r1.a, lum(r1), 0.2 # subtract low threshold
cmp r0.a, r1.a, 0.0, r0.a # r1 > 0 ? 0.0 : r0 (0.5)

# highlights
sub r2, r2[fmt:float], 0.75	fmt:float # subtract high threshold
cmp r0.a, r2[fmt:float], 0.8, r0.a # r2 > 0 ? 0.9 : r0 (0.5 or 0.0)

# sample distorted fb

# take lum for offset later
mul r1, lum(r1), 0.025 fmt:float
add r2, sv:uv, r1[fmt:float] fmt:half2
tex r3, fbo, r2[fmt:half2], 0.0
mul r3, r3, sv:identity

# blend in some of the distorted diffuse
texadd r1, t0, t0, r1[fmt:float]
mul r1.rgb, r1.rgb, v0.rgb
mix r3.rgb, r3.rgb, r1.rgb, 0.65

# add the refraction to the highlights
mad r0.rgb, r0.rgb, r0.a, r3.rgb

# make sure the alpha is 1 and the emissive is clear
mov r0.a, 1
mov r1, (0/0/0/1)
