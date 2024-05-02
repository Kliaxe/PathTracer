
// -------------------------------------------------------------------------
//    Physically based shading utilities
// -------------------------------------------------------------------------

// D = Microfacet NDF
// G = Shadowing and masking
// F = Fresnel

// Vis = G / (4*NoL*NoV)
// f = Microfacet specular BRDF = D*G*F / (4*NoL*NoV) = D*Vis*F

// Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
float D_GGX(float alpha, float NdotH)
{
	float oneMinusNoHSquared = 1.0f - NdotH * NdotH;
	float a = NdotH * alpha;
	float k = alpha / (oneMinusNoHSquared + a * a);
	return k * k * ONE_OVER_PI;
}

// Burley 2012, "Physically-Based Shading at Disney"
float D_GGX_Anisotropic(float at, float ab, float HdotX, float HdotY, float NdotH)
{
	float a2 = at * ab;
	vec3 d = vec3(ab * HdotX, at * HdotY, a2 * NdotH);
	float d2 = dot(d, d);
	float b2 = a2 / d2;
	return a2 * b2 * b2 * ONE_OVER_PI;
}

// Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
float D_Charlie(float roughness, float NdotH)
{
	float invR = 1.0f / roughness;
	float cos2h = NdotH * NdotH;
	float sin2h = 1.0f - cos2h;
	return (2.0f + invR) * pow(sin2h, invR * 0.5f) / ONE_OVER_TWO_PI;
}

// Smith's visibility function for isotropic surfaces
float V_Smith_G1_GGX(float alpha, float NdotV)
{
	float a = alpha * alpha;
	float b = NdotV * NdotV;
	return (2.0f * NdotV) / (NdotV + sqrt(a + b - a * b));
}

// Smith's visibility function for anisotropic surfaces
float V_Smith_G1_GGX_Anisotropic(float ax, float ay, float VdotX, float VdotY, float NdotV)
{
	float a = VdotX * ax;
	float b = VdotY * ay;
	float c = NdotV;
	return (2.0f * NdotV) / (NdotV + sqrt(a * a + b * b + c * c));
}

// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// Note that returned value is G2 / (4 * NdotL * NdotV)
float V_Smith_G2_Correlated_GGX(float alpha, float NdotV, float NdotL)
{
	float a2 = alpha * alpha;
	float lambdaV = NdotL * sqrt((NdotV - a2 * NdotV) * NdotV + a2);
	float lambdaL = NdotV * sqrt((NdotL - a2 * NdotL) * NdotL + a2);
	return 0.5f / (lambdaV + lambdaL);
}

// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// Note that returned value is G2 / (4 * NdotL * NdotV)
float V_Smith_G2_Correlated_GGX_Anisotropic(float ax, float ay, float VdotX, float VdotY, float LdotX, float LdotY, float NdotV, float NdotL)
{
	float lambdaV = NdotL * length(vec3(ax * VdotX, ay * VdotY, NdotV));
	float lambdaL = NdotV * length(vec3(ax * LdotX, ay * LdotY, NdotL));
	return 0.5f / (lambdaV + lambdaL);
}

// Kelemen 2001, "A Microfacet Based Coupled Specular-Matte BRDF Model with Importance Sampling"
float V_Kelemen(float LdotH)
{
	// Constant to prevent NaN
	return 0.25f / (LdotH * LdotH + 1e-5f);
}

// Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
float V_Neubelt(float NdotV, float NdotL)
{
	return 1.0f / (4.0f * (NdotL + NdotV - NdotL * NdotV));
}

// Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
float F_SchlickWeight(float u)
{
	float m = clamp(1.0f - u, 0.0f, 1.0f);
	float m2 = m * m;
	return m * m2 * m2;
}

float F_Schlick(float f0, float f90, float u)
{
	float w = F_SchlickWeight(u);
	return f0 + (f90 - f0) * w;
}

// Calculates the full Fresnel term for a dielectric material
float F_Dielectric(float cosThetaI, float incidentIor)
{
	float sinThetaTSq = incidentIor * incidentIor * (1.0f - cosThetaI * cosThetaI);

	// Total internal reflection
	if (sinThetaTSq > 1.0f)
		return 1.0f;

	float cosThetaT = sqrt(max(1.0f - sinThetaTSq, 0.0f));

	float rs = (incidentIor * cosThetaT - cosThetaI) / (incidentIor * cosThetaT + cosThetaI);
	float rp = (incidentIor * cosThetaI - cosThetaT) / (incidentIor * cosThetaI + cosThetaT);

	return 0.5f * (rs * rs + rp * rp);
}

float Fd_Lambert()
{
	return ONE_OVER_PI;
}

// Burley 2012, "Physically-Based Shading at Disney"
float Fd_Burley(float roughness, float NdotV, float NdotL, float LdotH)
{
	float f90 = 0.5f + 2.0f * roughness * LdotH * LdotH;
	float lightScatter = F_Schlick(1.0f, f90, NdotL);
	float viewScatter = F_Schlick(1.0f, f90, NdotV);
	return lightScatter * viewScatter * ONE_OVER_PI;
}

// Burley 2012, "Physically-Based Shading at Disney"
// Hanrahan-Krueger BRDF approximation of isotropic bssrdf
float Fd_HanrahanKrueger(float roughness, float NdotV, float NdotL, float LdotH)
{
	float Fss90 = roughness * LdotH * LdotH;
	float FLss = F_SchlickWeight(NdotL);
	float FVss = F_SchlickWeight(NdotV);
	float Fss = mix(1.0f, Fss90, FLss) * mix(1.0f, Fss90, FVss);
	return 1.25f * (Fss * (1.0f / (NdotL + NdotV) - 0.5f) + 0.5f) * ONE_OVER_PI;
}
