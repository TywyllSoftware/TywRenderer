#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 inUV;

struct Light 
{
	vec4 position;
	vec3 color;
	float radius;
};



layout (binding = 1) uniform   sampler2D  PositionTexture;
layout (binding = 2) uniform   sampler2D  SpecularTexture;
layout (binding = 3) uniform   usampler2D  DiffuseNormalAndDepthPacked;
layout (binding = 4) uniform   sampler2D   ssaoBlurImage; 
layout (binding = 5) uniform   sampler2D   ssaoImage; 

layout (binding = 6) uniform UBO 
{
	Light lights[17];
	vec4 viewPos;
	float fScreenGamma;
} ubo;


layout (binding = 7) uniform UBODebug
{
	uint  bSSAOIsOn;
	uint  bShowBlurSSAO;
	uint  bShowSSAO;
	uint  bShowDefuseOnly;
	uint  bShowDefuseAndSSAO;
}uboDebugOption;


layout (location = 0) out vec4 outFragColor;
#define lightCount 17
vec3 DefferedPass()
{
	ivec2 P0 = ivec2(inUV * textureSize(DiffuseNormalAndDepthPacked, 0));
	uvec4 uvec4_DiffuseNormalAndDepthPacked = texelFetch(DiffuseNormalAndDepthPacked, P0, 0);

	//Get Diffuse And Normal
	vec2 tempDiffuse0 = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.x);
	vec2 tempDiffAndNormal = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.y);
	vec2 tempNormal = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.z);

	vec3 diffuseTexture = vec3(tempDiffuse0, tempDiffAndNormal.x);
	vec3 normalTexture =  normalize(vec3(tempDiffAndNormal.y, tempNormal));

	//Get Position
	vec3 positionTexture = texture(PositionTexture, inUV).rgb;
	// GL to Vulkan coord space
	positionTexture.y = -positionTexture.y;

	//Get Specular
	vec3 specularTexture = texture(SpecularTexture, inUV).rgb;

	// Ambient part
	float AmbientOcclusion = texture(ssaoBlurImage, inUV).r;


	//Using ssao only in initialization stepped. Gennerated much brighter look;
	//What we want. Is to use SSAO Diffuse Texture Combination (diffuse with shadows) in diffuse calculation part if SSAO Is On
	//If it is off. We just use diffuse texture.
	vec3 ambient;
	if(uboDebugOption.bSSAOIsOn == 1)
	{
		ambient = vec3(diffuseTexture * AmbientOcclusion);
	}
	else
	{
		ambient = diffuseTexture;
	}

	vec3 lighting  = vec3(0.0);
	for(int i = 0; i < lightCount; ++i)
	{
		// Vector to light
		vec3 L = ubo.lights[i].position.xyz - positionTexture;

		// Distance from light to fragment position
		float dist = length(L);

		// Viewer to fragment
		vec3 V = ubo.viewPos.xyz - positionTexture;
		V = normalize(V);
		
		//if(dist < ubo.lights[i].radius)
		{
			// Light to fragment
			L = normalize(L);

			// Attenuation
			float atten = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

			// Diffuse part
			float NdotL = max(0.0, dot(normalTexture, L));
			vec3 diff = ubo.lights[i].color * ambient * NdotL * atten;


			// calculate specular reflection only if
			// the surface is oriented to the light source
			float specularTerm = 0;
			//if(dot(normalTexture, L) > 0)
			{
				//vec3 R = reflect(-L, N);
				vec3 H = normalize(L + V);
				specularTerm = pow(max(0.0, dot(normalTexture, H)), 16.0);
			}
			vec3 spec = ubo.lights[i].color * specularTexture * specularTerm * atten;

			//Final color
			lighting += diff + spec;	
		}	
	}  

	return lighting;
}


void main() 
 {
	if(uboDebugOption.bShowBlurSSAO == 1)
	{
		float ssaoTexture = texture(ssaoBlurImage, inUV).r;
		outFragColor = vec4(ssaoTexture);
	}
	else if(uboDebugOption.bShowSSAO == 1)
	{
		float ssaoTexture = texture(ssaoImage, inUV).r;
		outFragColor = vec4(ssaoTexture);
	}
	else if(uboDebugOption.bShowDefuseOnly == 1)
	{
		ivec2 P0 = ivec2(inUV * textureSize(DiffuseNormalAndDepthPacked, 0));
		uvec4 uvec4_DiffuseNormalAndDepthPacked = texelFetch(DiffuseNormalAndDepthPacked, P0, 0);

		//Get Diffuse
		vec2 tempDiffuse0 = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.x);
		vec2 tempDiffAndNormal = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.y);
		vec3 diffuseTexture = vec3(tempDiffuse0, tempDiffAndNormal.x);

		outFragColor = vec4(diffuseTexture, 1.0);
	}
	else if(uboDebugOption.bShowDefuseAndSSAO == 1)
	{
		ivec2 P0 = ivec2(inUV * textureSize(DiffuseNormalAndDepthPacked, 0));
		uvec4 uvec4_DiffuseNormalAndDepthPacked = texelFetch(DiffuseNormalAndDepthPacked, P0, 0);

		//Get Diffuse
		vec2 tempDiffuse0 = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.x);
		vec2 tempDiffAndNormal = unpackHalf2x16(uvec4_DiffuseNormalAndDepthPacked.y);
		vec3 diffuseTexture = vec3(tempDiffuse0, tempDiffAndNormal.x);

		// Ambient part
		float AmbientOcclusion = texture(ssaoBlurImage, inUV).r;

		vec3 ambient = vec3(diffuseTexture * AmbientOcclusion);
		outFragColor = vec4(ambient, 1.0);
	}
	else
	{
		vec3 color = DefferedPass();
		vec3 colorGammaCorrected = pow(color, vec3(1.0/ubo.fScreenGamma));
		outFragColor = vec4(colorGammaCorrected, 1.0);
	}
}