
// -------------------------------------------------------------------------
//    Random number generation and importance sampling functions
// -------------------------------------------------------------------------

// PCG (permuted congruential generator). Thanks to:
// www.pcg-random.org and www.shadertoy.com/view/XlGcRh
uint NextRandom(inout uint state)
{
	state = state * 747796405 + 2891336453;
	uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
	result = (result >> 22) ^ result;
	return result;
}

// Random float in range [0; 1]
float RandomValue(inout uint state)
{
	return NextRandom(state) / 4294967295.0f; // 2^32 - 1
}

// Random vec2 where components are in range [0; 1]
vec2 RandomValueVec2(inout uint state)
{
	float R1 = RandomValue(state);
	float R2 = RandomValue(state);
	return vec2(R1, R2);
}

// Random vec3 where components are in range [0; 1]
vec3 RandomValueVec3(inout uint state)
{
	float R1 = RandomValue(state);
	float R2 = RandomValue(state);
	float R3 = RandomValue(state);
	return vec3(R1, R2, R3);
}

// Point on hemisphere with uniform distribution
//	u, v : in range [0, 1]
// PDF = 1 / (2 * PI)
vec3 HemispherepointUniform(float u, float v)
{
	float phi = v * TWO_PI;
	float cosTheta = 1.0f - u;
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Point on hemisphere with cosine-weighted distribution
//	u, v : in range [0, 1]
// PDF = NoL / PI
vec3 HemispherepointCos(float u, float v)
{
	float phi = v * TWO_PI;
	float cosTheta = sqrt(1.0f - u);
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Sample half-angle directions from Generalized Trowbridge-Reitz (GGX) 2 lobe normal distribution
// PDF = D * NoH / (4 * VoH)
vec3 ImportanceSampleGGX(float alpha, vec2 Xi)
{
	float a2 = alpha * alpha;

	float cosThetaH = sqrt((1.0f - Xi.y) / (1.0f + (a2 - 1.0f) * Xi.y));
	float sinThetaH = sqrt(max(0.0f, 1.0f - cosThetaH * cosThetaH));

	float phiH = TWO_PI * Xi.x;
	float sinPhiH = sin(phiH);
	float cosPhiH = cos(phiH);

	// Sample the normal vector of the 'microfacet' as the half-angle vector h for specular reflection
	return vec3(sinThetaH * cosPhiH, sinThetaH * sinPhiH, cosThetaH);
}

// Samples a microfacet normal for the GGX distribution using VNDF method.
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// PDF = G_SmithV * VoH * D / NoV / (4 * VoH)
// PDF = G_SmithV * D / (4 * NoV)
vec3 ImportanceSampleVisibleGGX(vec3 Ve, float ax, float ay, vec2 Xi)
{
	// Section 3.2: transforming the view direction to the hemisphere configuration
	vec3 Vh = normalize(vec3(ax * Ve.x, ay * Ve.y, Ve.z));

	// Section 4.1: orthonormal basis (with special case if cross product is zero)
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0.0f ? vec3(-Vh.y, Vh.x, 0.0f) * inversesqrt(lensq) : vec3(1.0f, 0.0f, 0.0f);
	vec3 T2 = cross(Vh, T1);

	// Section 4.2: parameterization of the projected area
	float r = sqrt(Xi.x);
	float phi = TWO_PI * Xi.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.0f + Vh.z);
	t2 = mix(sqrt(1.0f - t1 * t1), t2, s);

	// Section 4.3: reprojection onto hemisphere
	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	// Section 3.4: transforming the normal back to the ellipsoid configuration
	return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0f, Nh.z)));
}

// Multiple importance sampling power heuristic of two functions with a power of two. 
// [Veach 1997, "Robust Monte Carlo Methods for Light Transport Simulation"]
float MisMixWeight(float a, float b)
{
	float t = a * a;
	return t / (b * b + t);
}

// When sampling from discrete CDFs, it can be convenient to re-use the random number by rescaling it
// This function assumes that randomValue is in the interval: [lowerBound, UpperBound) and returns a value in [0,1)
float RescaleRandomNumber(float randomValue, float lowerBound, float upperBound)
{
	const float oneMinusEpsilon = 0.99999994f; // 32-bit float just before 1.0
	return min((randomValue - lowerBound) / (upperBound - lowerBound), oneMinusEpsilon);
}
