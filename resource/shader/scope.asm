ps.1.0

alias color, r0
alias emissive, r1
alias diffuse, v0
alias specular, v1
alias mask, r1

# sample color and apply lighting
tex color, tex0, t0
mul color, color, diffuse

# UV radial mask
frc mask, t0												fmt:half2		# r1 = frac(uv)
dp2 mask, mask[fmt:half2 expand], mask[fmt:half2 expand]	fmt:float clamp	# r1 = saturate(dot(r1, r1))
pow mask, 1 - mask[fmt:float], 4.0 fmt:float								# r1 = pow(1 - r1, 4)

# read framebuffer and overlay with mask
tex r2, fbo, t1, 0.0 #r2[fmt:half2]									# read framebuffer
mad r0.rgb, mask[fmt:float], r2[div:2], r0							# r0 = r1 * r2 + r0

# add specular
add r0.rgb, r0, specular

# clear emissive
mov r1, 0
