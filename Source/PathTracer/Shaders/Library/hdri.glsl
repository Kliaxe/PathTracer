
// -------------------------------------------------------------------------
//    HDRI sampling
// -------------------------------------------------------------------------

vec2 GetSphericalCoordinates(vec3 V)
{
	float u = atan(V.z, V.x) * ONE_OVER_TWO_PI + 0.5f;
	float v = 0.5f - asin(V.y) * ONE_OVER_PI;

	return vec2(u, v);
}

// Sample from the precomputed HDRI cache
vec3 SampleHdri(inout uint rngState)
{
	// Sample Xi
	vec2 Xi = RandomValueVec2(rngState);

	vec2 uv = SampleBindlessTexture(environment.hdriCacheHandle, Xi).rg; // x, y
	uv.y = 1.0f - uv.y; // Flip

	// Get angle
	float phi = TWO_PI * (uv.x - 0.5f);	// [-pi ~ pi]
	float theta = PI * (uv.y - 0.5f);	// [-pi/2 ~ pi/2]

	// Calculate the direction in spherical coordinates
	vec3 L = vec3(cos(theta) * cos(phi), sin(theta), cos(theta) * sin(phi));

	return L;
}

// Get HDRI environment color
vec3 EvaluateHdri(vec3 L, out float pdf)
{
	vec3 color = vec3(0.0f);
	pdf = 0.0;

	vec2 uv = GetSphericalCoordinates(normalize(L));

	color = SampleBindlessTexture(environment.hdriHandle, uv).rgb;
	pdf = SampleBindlessTexture(environment.hdriCacheHandle, uv).b; // Sample probability density

	float theta = PI * (1.0f - uv.y); // 1.0f for full range...
	float sinTheta = max(sin(theta), 1e-10f);

	// Conversion factor between spherical coordinates and image integration domain
	float pConvert = environment.hdriDimensions.x * environment.hdriDimensions.y / (TWO_PI * PI * sinTheta);

	// Apply pdf conversion
	pdf *= pConvert;

	return color;
}
