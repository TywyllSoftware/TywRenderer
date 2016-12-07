#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable


layout (location = 0) in vec2 inUV;


layout (binding = 1) uniform   usampler2D  PosSpecularPacked;
layout (binding = 2) uniform   usampler2D  DiffuseNormalDepthPacked; 
layout (binding = 3) uniform   sampler2D   texNoise;

//Output 
layout (location = 0) out float FragColor;


// parameters (you'd probably want to use them as uniforms to more easily tweak the effect)
const int kernelSize = 64;
const float radius = 1.0;
layout (std140 , binding = 4) uniform UBOSSAOKernel 
{
	vec4 samples[64];
} ubossaokernel;

layout (binding = 5) uniform UBO
{
	mat4 projection;
}ubo;



// c_precision of 128 fits within 7 base-10 digits
const float c_precision = 128.0;
const float c_precisionp1 = c_precision + 1.0;
/*
	param value 3-component encoded float
	returns normalized RGB value
*/
vec3 float2color(float value) 
{
    vec3 color;
    color.r = mod(value, c_precisionp1) / c_precision;
    color.b = mod(floor(value / c_precisionp1), c_precisionp1) / c_precision;
    color.g = floor(value / (c_precisionp1 * c_precisionp1)) / c_precision;
    return color;
}

//http://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

float SSAOAlgo0()
{
	ivec2 P0 = ivec2(inUV * textureSize(DiffuseNormalDepthPacked, 0));
	ivec2 P1 = ivec2(inUV * textureSize(PosSpecularPacked, 0));

	uvec4 uvec4_PosSpecularPacked = texelFetch(PosSpecularPacked, P1, 0);
	uvec4 uvec4_DiffuseNormalDepthPacked = texelFetch(DiffuseNormalDepthPacked, P0, 0);

    //Get position and depth texture
	vec2 tempPosition0 = unpackHalf2x16(uvec4_PosSpecularPacked.x);
	vec2 tempPosAndSpec = unpackHalf2x16(uvec4_PosSpecularPacked.y);

    vec3 fragPos = vec3(tempPosition0, tempPosAndSpec.x);

	//Get normal
	vec2 tempDiffAndNormal = unpackHalf2x16(uvec4_DiffuseNormalDepthPacked.y);
	vec2 tempNormal = unpackHalf2x16(uvec4_DiffuseNormalDepthPacked.z);

    vec3 normal = normalize(vec3(tempDiffAndNormal.y, tempNormal));

	//Random vec using noise lookup
	ivec2 texDim = textureSize(DiffuseNormalDepthPacked, 0); 
	ivec2 noiseDim = textureSize(texNoise, 0);
	const vec2 noiseUV = vec2(float(texDim.x)/float(noiseDim.x), float(texDim.y)/(noiseDim.y)) * inUV;  
	vec3 randomVec = texture(texNoise, noiseUV).xyz * 2.0 - 1.0;

    // Create TBN change-of-basis matrix: from tangent-space to view-space
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Iterate over the sample kernel and calculate occlusion factor
    float f_occlusion = 0.0;
    for(int i = 0; i < 64; ++i)
    {
        // get sample position
        vec3 Sample = vec3(TBN * ubossaokernel.samples[i].xyz); // From tangent to view-space
        Sample = fragPos + Sample * radius; 
        
		
        // project sample position (to sample texture) (to get position on screen/texture)
        vec4 offset = vec4(Sample, 1.0);
        offset = ubo.projection * offset; // from view to clip-space
        offset.xyz /= offset.w; // perspective divide
        offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0
        
		// get sample depth
		ivec2 texOffset = ivec2(offset.xy * textureSize(DiffuseNormalDepthPacked, 0));
        uint packed = texelFetch(DiffuseNormalDepthPacked, texOffset, 0).a; // Get depth value of kernel sample
		float sampleDepth = -uintBitsToFloat(packed);
			
        // range check & accumulate
       float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth ));
       f_occlusion += (sampleDepth >= Sample.z ? 1.0 : 0.0) * rangeCheck;          
    }
    f_occlusion = 1.0 - (f_occlusion / kernelSize);
	return f_occlusion;
}

void main()
{
    FragColor = SSAOAlgo0();
}