import "defines.gli"
import "uniforms.gli"
import "clustering.gli"
import "math.gli"
import "attr.gli"
import "lighting.gli"
import "decals.gli"
import "occluders.gli"
import "textures.gli"
import "texgen.gli"
import "framebuffer.gli"
import "reg.gli"
import "vm.gli"
import "debug.gli"

uint get_cluster()
{
	float viewDist = readVPOS().y;
	return compute_cluster_index(gl_FragCoord.xy, viewDist) * CLUSTER_BUCKETS_PER_CLUSTER;
}

// packs light results into color0 and color1
float calc_light()
{
	v[0] = 0;
	v[1] = 0;

	vec3 normal    = normalize(unpackSnorm4x8(vnorm).xyz);
	vec3 viewPos   = readVPOS().xyz;
	vec3 view      = normalize(-viewPos.xyz);
	vec3 reflected = reflect(-view, normal);

	flex fog = flex(0.0);// fetch_vtx_color(1).w;
	if(fogEnabled > 0)
	{
		float distToCam = max(0.0, viewPos.y) * fastRcpNR1(-view.y);
		fog = flex(saturate((distToCam - fogStart) * fastRcpNR1(fogEnd - fogStart)));
	}

	float dither = dither_value_float(uvec2(gl_FragCoord.xy));

	if (lightMode < 2)
	{	
		v[0] = pack_vertex_reg( vec4(fetch_vtx_color(0)) );
		v[1] = pack_vertex_reg( vec4(fetch_vtx_color(1).rgb, fog) );
	}
	else
	{
		v[0] = pack_vertex_reg(vec4(0.0, 0.0, 0.0, fetch_vtx_color(0).w));
		v[1] = pack_vertex_reg(vec4(0.0, 0.0, 0.0, fog));

		light_result result;
		result.diffuse  = packF2x11_1x10(vec3(fetch_vtx_color(0).rgb));
		result.specular = packF2x11_1x10(vec3(fetch_vtx_color(1).rgb));

		// light mode "gouraud" (a.k.a new per pixel)
		if (lightMode > 2)
		{
			light_input params;
			params.pos       = vpos;
			params.normal    = vnorm;//packSnorm4x8(vec4(normal.xyz, 0));
			params.view      = packSnorm4x8(vec4(view,0));
			params.reflected = packSnorm4x8(vec4(reflected,0));
			params.spec_c    = calc_spec_c(roughnessFactor);
			params.tint      = packUnorm4x8(vec4(fetch_vtx_color(0)));

			uint cluster = get_cluster();
			calculate_lighting(result, params, cluster);

			// loop shadow occluders
			if ((aoFlags & 0x1) == 0x1)// && numOccluders > 0u)
			{		
				float shadow = calculate_shadows(params, cluster);
	
				result.diffuse = packF2x11_1x10(unpackF2x11_1x10(result.diffuse) * shadow);
				result.specular = packF2x11_1x10(unpackF2x11_1x10(result.specular) * shadow);
			} // aoFlags
		} // lightMode > 2

		// gamma hack
		// Unity "Optimizing PBR"
		// https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_renaldas_2D00_slides.pdf
		//result.specular.rgb = sqrt(result.specular.rgb) * specularFactor.rgb;
	
		const vec3 specularScale = specularFactor.rgb;
		const vec3 diffuseScale  = albedoFactor.rgb;

		vec3 diffuse  = unpackF2x11_1x10(result.diffuse);
		vec3 specular = unpackF2x11_1x10(result.specular);

		uint v0 = pack_vertex_reg(vec4(diffuse.rgb * diffuseScale.rgb+(dither * ditherScale), 0));
		uint v1 = pack_vertex_reg(vec4(specular.rgb * specularScale.rgb+(dither * ditherScale), 0));

		v[0] = (v[0] & 0xFF000000) | (v0 & 0x00FFFFFF);
		v[1] = (v[1] & 0xFF000000) | (v1 & 0x00FFFFFF);
		
		return result.cost;
	} // lightMode < 2

	return 0.0;
}

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragGlow;

#ifdef MOTION_BLUR
layout(location = 2) out vec4 fragVel;
#endif

void main(void)
{
#if defined(GL_KHR_shader_subgroup_ballot) && defined(GL_KHR_shader_subgroup_arithmetic)
	s_lodbias = subgroupBroadcastFirst(subgroupMax(uint( fetch_vtx_lodbias() )));
#else
	s_lodbias = uint( fetch_vtx_lodbias() );
#endif

	vec2    uv      = fetch_vtx_uv(0);
	vec3    viewPos = fetch_vtx_pos();
	flex3   viewDir = flex3(normalize(-viewPos.xyz));
	flex3   normal  = normalize(fetch_vtx_normal());
	flex3x3 tbn     = construct_tbn(uv, viewPos, normal);
	vdir            = encode_octahedron_uint(vec3(viewDir * tbn));

	// pack interpolated vertex data to reduce register pressure
	vpos  = packVPOS(fetch_vtx_coord());
	vnorm = packSnorm4x8(vec4(normal.xyz, 0.0));

	for (int i = 0; i < UV_SETS; ++i)
		tr[i] = packTexcoordRegister(fetch_vtx_uv(i));

	// calculate lighting
	float cost = calc_light();

	// run the combiner stage
	run_vm();

	// blend in the decals
	apply_decals(get_cluster());

	// unpack color and do fog and whatever else
	vec4 outColor = unpackUnorm4x8(r[0]); // unpack r0
	outColor.rgb *= invlightMult;

	// alpha testing
#ifdef ALPHA_DISCARD
    if (outColor.a < 1e-3) // todo: alpha test value
		discard;
#endif

	float dither = dither_value_float(uvec2(gl_FragCoord.xy));

	// apply fog to outputs
	if(fogEnabled > 0)
	{
		vec3 fog_color = fogColor.rgb * invlightMult;
		if (fogLightDir.w > 0.0)
		{
			vec3  viewDir = normalize(-fetch_vtx_pos());
			float viewDot = dot(fogLightDir.xyz, viewDir.xyz) * 0.5 + 0.5;
			fog_color *= textureLod(phaseTexture, vec2(fogAnisotropy, viewDot), 0).r;
		}

		float fog = unpackUnorm4x8(v[1]).w * fogColor.a;
		outColor.rgb = mix(outColor.rgb, dither * ditherScaleAlways + fog_color.rgb, saturate(dither * ditherScaleAlways + fog));
		fragGlow.rgb = fragGlow.rgb * (1.0 - fog);
	}

	// todo: make ditherMode a per-rt thing
	// dither the output if needed
	outColor.rgb = (outColor.rgb + (dither * ditherScale));

	fragColor = subsample(outColor);

	
	//fragColor.rgb = temperature(cost / 8.0);

	// unpack glow and output
	fragGlow = vec4(unpackUnorm4x8(r[1])); // unpack r1

	// apply glow multiplier
	fragGlow.rgb *= emissiveFactor.w;

	// note we subtract instead of add to avoid boosting blacks
	fragGlow.rgb = saturate(-dither * ditherScaleAlways + fragGlow.rgb);

#ifdef MOTION_BLUR
	vec2 curTC  = f_curTC.xy / f_curTC.w;
	vec2 prevTC = f_prevTC.xy / f_prevTC.w;

	float tick = 1.0 / deltaTime;
	float shutter = tick / 150.0;

	vec2 vel = (curTC.xy - prevTC.xy) * shutter;
	fragVel = vec4(vel, fetch_vtx_depth(), 1);
#endif
}
