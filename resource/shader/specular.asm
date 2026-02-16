ps.1.0

# aliases
alias diff, r0
alias glow, r1

# sample diffuse and multiply by diffuse lighting
tex diff, tex0, t0 # sample diffuse
mul diff.rgb, diff.rgb, v0.rgb # multiply diffuse color with diffuse light

# multiply with specular light and add to diffuse result
mad diff.rgb, diff.rgb[mul:4], v1.rgb, diff.rgb

# sample glow and take max with diffuse
tex glow, tex1, t0
max diff.rgb, diff.rgb, glow.rgb

# apply glow multiplier
mul glow, glow, mat:glow

# vertex alpha
mov diff.a, v0.a
