
// -------------------------------------------------------------------------
//    Common utilities
// -------------------------------------------------------------------------

// Clever offset_ray function from Ray Tracing Gems chapter 6
// Offsets the ray origin from current position p, along normal n (which must be geometric normal)
// so that no self-intersection can occur.
vec3 OffsetRay(vec3 P, vec3 N)
{
	const float origin = 1.0f / 32.0f;
	const float floatScale = 1.0f / 65536.0f;
	const float intScale = 256.0f;

	ivec3 of_i = ivec3(intScale * N.x, intScale * N.y, intScale * N.z);

	vec3 p_i = vec3(
		intBitsToFloat(floatBitsToInt(P.x) + ((P.x < 0.0f) ? -of_i.x : of_i.x)),
		intBitsToFloat(floatBitsToInt(P.y) + ((P.y < 0.0f) ? -of_i.y : of_i.y)),
		intBitsToFloat(floatBitsToInt(P.z) + ((P.z < 0.0f) ? -of_i.z : of_i.z)));

	return vec3(
		abs(P.x) < origin ? P.x + floatScale * N.x : p_i.x,
		abs(P.y) < origin ? P.y + floatScale * N.y : p_i.y,
		abs(P.z) < origin ? P.z + floatScale * N.z : p_i.z);
}

void GetTangentBitangent(vec3 N, inout vec3 tangent, inout vec3 bitangent)
{
	// Choose a helper vector for the cross product
	vec3 helper = abs(N.x) > 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);

	// Generate vectors
	tangent = normalize(cross(N, helper));
	bitangent = normalize(cross(N, tangent));
}

mat3 GetTangentSpace(vec3 N)
{
	// Generate vectors
	vec3 T;
	vec3 B;
	GetTangentBitangent(N, T, B);

	// TBN matrix
	return mat3(T, B, N);
}

vec3 ToLocal(vec3 N, vec3 V)
{
	// Since the T, B, N are orthonormal, transpose operation is equivalent to the inverse
	return transpose(GetTangentSpace(N)) * V;
}

vec3 ToWorld(vec3 N, vec3 V)
{
	return GetTangentSpace(N) * V;
}

float Luminance(vec3 rgb)
{
	return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

// -------------------------------------------------------------------------
//    Material evaluation
// -------------------------------------------------------------------------

vec3 SampleNormalMap(sampler2D normalTexture, HitInfo hitInfo)
{
	// By sampling normal maps, uvs should are available to calculate tangent
	vec3 N = hitInfo.shadingNormal;
	vec3 T = hitInfo.tangent;

	// Gram-Schmidt process: Re-orthogonalize T with respect to N
	T = normalize(T - dot(T, N) * N);

	// Recalculate bitangent due to Gram-Schmidt
	vec3 B = cross(N, T);

	// Create tangent space matrix
	mat3 TBN = mat3(T, B, N);

	// Sample normal texture
	vec3 normalMap = texture(normalTexture, hitInfo.uv).rgb;

	// Transform normal vector to range [-1,1]
	normalMap = normalize(normalMap * 2.0 - 1.0);

	// Apply tangent space matrix
	return normalize(TBN * normalMap);
}

Material EvaluateMaterial(Material material, inout HitInfo hitInfo)
{
	Material evalutedMaterial = material;

	if (material.emissionTextureHandle != 0)
	{
		vec3 emissionSample = texture(sampler2D(material.emissionTextureHandle), hitInfo.uv).rgb;
		evalutedMaterial.emission *= emissionSample;
	}
	if (material.albedoTextureHandle != 0)
	{
		vec3 albedoSample = texture(sampler2D(material.albedoTextureHandle), hitInfo.uv).rgb;
		evalutedMaterial.albedo *= albedoSample;
	}
	if (material.normalTextureHandle != 0)
	{
		hitInfo.shadingNormal = SampleNormalMap(sampler2D(material.normalTextureHandle), hitInfo);
	}
	if (material.specularTextureHandle != 0)
	{
		float specularSample = texture(sampler2D(material.specularTextureHandle), hitInfo.uv).a;
		evalutedMaterial.specular *= specularSample;
	}
	if (material.specularColorTextureHandle != 0)
	{
		vec3 specularColorSample = texture(sampler2D(material.specularColorTextureHandle), hitInfo.uv).rgb;
		evalutedMaterial.specularTint *= length(specularColorSample); // We only care about magnitude, color will be sampled according to Disney
	}
	if (material.metallicRoughnessTextureHandle != 0)
	{
		vec2 metallicRoughnessSample = texture(sampler2D(material.metallicRoughnessTextureHandle), hitInfo.uv).rg;
		evalutedMaterial.metallic *= metallicRoughnessSample.x;
		evalutedMaterial.roughness *= metallicRoughnessSample.y;
	}
	if (material.sheenRoughnessTextureHandle != 0)
	{
		float sheenRoughnessSample = texture(sampler2D(material.sheenRoughnessTextureHandle), hitInfo.uv).a;
		evalutedMaterial.sheenRoughness *= sheenRoughnessSample;
	}
	if (material.sheenColorTextureHandle != 0)
	{
		vec3 sheenColorSample = texture(sampler2D(material.sheenColorTextureHandle), hitInfo.uv).rgb;
		evalutedMaterial.sheenTint *= length(sheenColorSample); // We only care about magnitude, color will be sampled according to Disney
	}
	if (material.clearcoatTextureHandle != 0)
	{
		float clearcoatSample = texture(sampler2D(material.clearcoatTextureHandle), hitInfo.uv).r;
		evalutedMaterial.clearcoat *= clearcoatSample;
	}
	if (material.clearcoatRoughnessTextureHandle != 0)
	{
		float clearcoatRoughnessSample = texture(sampler2D(material.clearcoatRoughnessTextureHandle), hitInfo.uv).g;
		evalutedMaterial.clearcoatRoughness *= clearcoatRoughnessSample;
	}
	if (material.transmissionTextureHandle != 0)
	{
		float transmissionSample = texture(sampler2D(material.transmissionTextureHandle), hitInfo.uv).r;
		evalutedMaterial.transmission *= transmissionSample;
	}

	return evalutedMaterial;
}

Material GetEvaluatedMaterial(inout HitInfo hitInfo)
{
	// Material index are associated with primitives
	uint index = hitInfo.primitiveIndex;

	// Get material and evaluate
	Material currentMaterial = materials[index];
	Material evaluatedMaterial = EvaluateMaterial(currentMaterial, hitInfo);

	return evaluatedMaterial;
}

// -------------------------------------------------------------------------
//    BRDF data preparation
// -------------------------------------------------------------------------

float IorToF0(float transmittedIor, float incidentIor)
{
	float r = (transmittedIor - incidentIor) / (transmittedIor + incidentIor);
	return r * r;
}

float F0ToIor(float f0)
{
	float r = sqrt(f0);
	return (1.0 + r) / (1.0 - r);
}

BrdfData PrepareEvaluationBrdfData(Material material, vec3 V, vec3 N, vec3 L)
{
	BrdfData data;

	// Get tangent and bitangent
	vec3 X;
	vec3 Y;
	GetTangentBitangent(N, X, Y);

	// Unpack 'perceptively linear' -> 'linear' -> 'squared' roughness
	data.roughness = clamp(material.roughness, 0.045f, 1.0f);
	data.alpha = data.roughness * data.roughness;
	data.alphaSquared = data.alpha * data.alpha;

	data.roughnessClearcoat = clamp(material.clearcoatRoughness, 0.045f, 1.0f);
	data.alphaClearcoat = data.roughnessClearcoat * data.roughnessClearcoat;
	data.alphaSquaredClearcoat = data.alphaClearcoat * data.alphaClearcoat;

	// Kulla 2017, "Revisiting Physically Based Shading at Imageworks"
	data.alphaAnisotropicX = max(0.0f, data.alpha * (1.0f + material.anisotropy));
	data.alphaAnisotropicY = max(0.0f, data.alpha * (1.0f - material.anisotropy));

	// Evaluate VNLHXY vectors
	data.V = V;
	data.N = N;
	data.L = L;

	// Pre-calculate for eta and H
	float NdotV = dot(data.N, data.V);
	float NdotL = dot(data.N, data.L);

	// eta to estimate in and out refractive direction, based on V backfacing
	data.eta = NdotV <= 0.0f ? material.refraction : (1.0f / material.refraction);

	// Calculate half vector based on eta, based on L backfacing
	data.H = NdotL <= 0.0f ? normalize(L + V * data.eta) : normalize(L + V);

	// Avoid half vector going into ground
	float NdotH = dot(N, data.H);
	if (NdotH < 0.0f)
		data.H = -data.H;

	data.X = X;
	data.Y = Y;

	data.NdotV = NdotV;
	data.NdotL = NdotL;
	data.VdotH = dot(data.V, data.H);
	data.NdotH = dot(data.N, data.H);
	data.LdotH = dot(data.L, data.H);
	data.VdotX = dot(data.V, data.X);
	data.LdotX = dot(data.L, data.X);
	data.HdotX = dot(data.H, data.X);
	data.VdotY = dot(data.V, data.Y);
	data.LdotY = dot(data.L, data.Y);
	data.HdotY = dot(data.H, data.Y);

	// Pre-calculate some more BRDF terms
	data.f0 = IorToF0(1.0f, data.eta);
	data.f90 = clamp(dot(vec3(data.f0), vec3(50.0f * 0.33f)), 0.0f, 1.0f);

	return data;
}

