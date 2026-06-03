ps.1.0

# sample color and apply lighting
tex r0, tex0, t0
mul r0, r0, v0

# UV radial mask
frc r1, t0											fmt:half2	# r1 = frac(uv)
dp2 r1, r1[fmt:half2 expand], r1[fmt:half2 expand]	clamp		# r1 = saturate(dot(r1, r1))
pow r1, 1 - r1, 4.0												# r1 = pow(1 - r1, 4)

# read framebuffer and overlay with mask
tex r2, fbo, t1, 0.0 #r2[fmt:half2]									# read framebuffer
mad r0.rgb, r1, r2[div:2], r0							# r0 = r1 * r2 + r0

# add specular
add r0.rgb, r0, v1

# clear emissive
mov r1, 0
