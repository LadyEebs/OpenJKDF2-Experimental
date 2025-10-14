import "uniforms.gli"
import "clustering.gli"
import "math.gli"
import "lighting.gli"
import "attr.gli"
import "reg.gli"

vec3 CalculateAmbientDiffuse(vec3 normal)
{
	vec3 ambientDiffuse = vec3(0.0);
	for(int sg = 0; sg < AMBIENT_LOBES; ++sg)
	{
		SG lightSG;
		lightSG.Amplitude = ambientSG[sg].xyz;//unpack_argb2101010(ambientSG[sg]).xyz / 255.0;
		lightSG.Axis = ambientSGBasis[sg].xyz;
		lightSG.Sharpness = ambientSGBasis[sg].w;
	
		vec3 diffuse = SGIrradiancePunctual(lightSG, normal);
		ambientDiffuse.xyz += diffuse;
	}
	return ambientDiffuse;
}

vec3 CalculateAmbientSpecular(float roughness, vec3 normal, vec3 view, vec3 reflected)
{
	vec3 ambientSpec = vec3(0.0);

	// project the reflection vector onto a sphere around the sector for this ambient SG
	//reflected = project_to_sphere(f_coord.xyz, reflected, ambientCenter.xyz, ambientCenter.w);

	float m = roughness * roughness;
	float m2 = max(m * m, 1e-4);
	float amplitude = 1.0 * fastRcpNR0(M_PI * m2);
	float sharpness = (2.0 * fastRcpNR0(m2)) * fastRcpNR0(4.0 * max(dot(normal, view), 0.1));
	
	for(int sg = 0; sg < AMBIENT_LOBES; ++sg)
	{
		vec4 sgCol = ambientSG[sg];//unpack_argb2101010(ambientSG[sg]);
		vec3 ambientColor = sgCol.xyz;//mix(vec3(v_color[0].bgr), sgCol.xyz, vec3(sgCol.w)); // use vertex color if no ambientSG data
	
		float umLength = length(sharpness * reflected + (ambientSGBasis[sg].w * ambientSGBasis[sg].xyz));
		float attenuation = 1.0 - exp(-2.0 * umLength);
		float nDotL = clamp(dot(normal.xyz, reflected), 0.0, 1.0);
	
		// todo: can we roll this into something like the direct light approx?
		float D = (2.0 * M_PI) * nDotL * attenuation * fastRcpNR1(umLength);
		//float D = (2.0 * nDotL) * attenuation * fastRcpNR1(umLength);
		
		float expo = (exp(umLength - sharpness - ambientSGBasis[sg].w) * amplitude);

		// fresnel approx as 1 / ldoth at center of the warped lobe
		//vec3 h = normalize(reflected + view);
		//D /= clamp(dot(reflected, h), 0.1, 1.0);

		ambientSpec = (D * expo) * ambientColor + ambientSpec;
	}
	return (ambientSpec);
}


flex compute_mip_bias(float z_min)
{
	flex mipmap_level = flex(0.0);
	mipmap_level        = z_min < mipDistances.x ? mipmap_level : flex(1.0);
	mipmap_level        = z_min < mipDistances.y ? mipmap_level : flex(2.0);
	mipmap_level        = z_min < mipDistances.z ? mipmap_level : flex(3.0);
	return mipmap_level + flex(mipDistances.w);
}

void main(void)
{
	vec4 viewPos = modelMatrix * vec4(coord3d, 1.0);
    vec4 pos = projMatrix * viewPos;

	mat3 normalMatrix = transpose(inverse(mat3(modelMatrix))); // if we ever need scaling
	vec3 normal = normalMatrix * (v_normal.xyz * 2.0 - 1.0);
	normal = normalize(normal);

	f_lodbias = min(compute_mip_bias(viewPos.y), flex(numMips - 1));

    gl_Position = pos;
    vec4 color0 = clamp(vec4(v_color[0].bgra), vec4(0.0), vec4(1.0));
	vec4 color1 = clamp(vec4(v_color[1].bgra), vec4(0.0), vec4(1.0));

	for (int i = 0; i < UV_SETS; ++i)
	{
		f_uv[i] = v_uv[i].xyz;
		f_uv[i].xy += uv_offset[i].xy;
	}

 	f_coord = vec4(viewPos.xyz, pos.w / 128.0);
	
	vec3 view = normalize(-viewPos.xyz);

	color1.a = 0.0;
	if (fogEnabled > 0)
		color1.a = clamp((pos.w - fogStart) / (fogEnd - fogStart), 0.0, 1.0);

	color1.rgb = vec3(0.0);

	// do ambient diffuse in vertex shader
	if (lightMode >= 2)
		color0.xyz = max(color0.xyz, ambientColor.xyz);
	
	// extra light
	color0.xyz += ambientColor.a;

	if (lightMode >= 2)
		color0.xyz += CalculateAmbientDiffuse(normal);

	color1.rgb = vec3(0.0);
	if (lightMode > 2)
		color1.xyz = CalculateAmbientSpecular(roughnessFactor, normal, view, reflect(-view, normal));

	// light mode "diffuse" (a.k.a new gouraud)
	if (lightMode == 2)
	{
		light_result result;
		result.diffuse = packF2x11_1x10(color0.rgb);
		result.specular = packF2x11_1x10(color1.rgb);

		if(numLights > 0u)
		{
			float a = roughnessFactor;// * roughnessFactor;

			uint cluster = compute_cluster_index(clamp(pos.xy / pos.w * 0.5 + 0.5, vec2(0.0), vec2(1.0)) * iResolution.xy, viewPos.y);
			light_input params;
			params.pos       = packVPOS(vec4(viewPos.xyz, pos.w));
			params.normal    = packSnorm4x8(vec4(normal.xyz, 0));
			params.view      = encode_octahedron_uint(view);
			params.reflected = encode_octahedron_uint(reflect(-view, normal));
			params.spec_c    = calc_spec_c(a);
			params.tint      = packUnorm4x8(color0);

			// loop light buckets
			uint first_item = firstLight;
			uint last_item = first_item + numLights - 1u;
			uint first_bucket = first_item >> 5u;
			uint last_bucket = min(last_item >> 5u, max(0u, CLUSTER_BUCKETS_PER_CLUSTER - 1u));
			for (uint bucket = first_bucket; bucket <= last_bucket; ++bucket)
			{
				uint bucket_bits = uint(texelFetch(clusterBuffer, int(cluster * CLUSTER_BUCKETS_PER_CLUSTER + bucket)).x);
				while(bucket_bits != 0u)
				{
					uint bucket_bit_index = findLSB(bucket_bits);
					uint light_index = (bucket << 5u) + bucket_bit_index;
					bucket_bits ^= (1u << bucket_bit_index);
	
					if (light_index >= first_item && light_index <= last_item)
					{
						calc_point_light(result, light_index, params);
					}
					else if (light_index > last_item)
					{
						bucket = CLUSTER_BUCKETS_PER_CLUSTER;
						break;
					}
				}
			}
	
			color0.rgb = unpackF2x11_1x10(result.diffuse);
			//color1.rgb = unpackF2x11_1x10(result.specular);
		}
	}

	f_color[0] = flex4(color0);
	f_color[1] = flex4(color1);
	f_normal.xyz = flex3(normal);

	
#ifdef MOTION_BLUR
	f_curTC = pos;
	f_curTC.xy = (pos.xy * vec2(1, -1) + pos.ww  ) * 0.5;

	mat4 prevModelView = viewMatrixPrev * modelMatrixPrev;
	vec4 prevPos = projMatrix * prevModelView * vec4(coord3d, 1.0);

	f_prevTC = prevPos;
	f_prevTC.xy = (prevPos.xy * vec2(1, -1) + prevPos.ww  ) * 0.5;
#endif
}
