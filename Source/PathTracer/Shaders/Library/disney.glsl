
// -------------------------------------------------------------------------
//    Disney evaluation functions
// -------------------------------------------------------------------------

vec3 CalculateTint(Material material)
{
	float luminance = Luminance(material.albedo);
	return luminance > 0.0f ? material.albedo / luminance : vec3(1.0f);
}

void TintColors(Material material, BrdfData brdfData, out vec3 Csheen, out vec3 Cspec0)
{
	vec3 Ctint = CalculateTint(material);

	Cspec0 = brdfData.f0 * mix(vec3(1.0f), Ctint, material.specularTint) * material.specular; // 0° Specular Color
	Csheen = mix(vec3(1.0f), Ctint, material.sheenTint); // Sheen Color
}

vec3 EvaluateDisneyDiffuse(Material material, BrdfData brdfData, out float pdf)
{
	pdf = 0.0f;
	if (brdfData.NdotL <= 0.0f)
	{
		return vec3(0.0f);
	}

	// Burley 2012, "Physically-Based Shading at Disney"
	float Fd = Fd_Burley(brdfData.roughness, brdfData.NdotV, brdfData.NdotL, brdfData.LdotH);

	// Hanrahan-Krueger BRDF approximation of isotropic bssrdf
	float Fdss = Fd_HanrahanKrueger(brdfData.roughness, brdfData.NdotV, brdfData.NdotL, brdfData.LdotH);

	pdf = brdfData.NdotL * ONE_OVER_PI;
	return mix(Fd, Fdss, material.subsurface) * material.albedo;
}

vec3 EvaluateMicrofacetReflection(Material material, BrdfData brdfData, vec3 F, out float pdf)
{
	pdf = 0.0f;
	if (brdfData.NdotL <= 0.0f)
	{
		return vec3(0.0f);
	}

	float D = D_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.HdotX, brdfData.HdotY, brdfData.NdotH);
	float V1 = V_Smith_G1_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.VdotX, brdfData.VdotY, abs(brdfData.NdotV));
	float V2 = V_Smith_G2_Correlated_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.VdotX, brdfData.VdotY, brdfData.LdotX, brdfData.LdotY, abs(brdfData.NdotV), abs(brdfData.NdotL));

	pdf = D * V1 / (4.0f * brdfData.NdotV);
	return F * D * V2;
}

vec3 EvaluateMicrofacetRefraction(Material material, BrdfData brdfData, vec3 F, out float pdf)
{
	pdf = 0.0f;
	if (brdfData.NdotL >= 0.0f)
	{
		return vec3(0.0f);
	}

	float D = D_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.HdotX, brdfData.HdotY, brdfData.NdotH);
	float V1 = V_Smith_G1_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.VdotX, brdfData.VdotY, abs(brdfData.NdotV));
	float V2 = V_Smith_G2_Correlated_GGX_Anisotropic(brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, brdfData.VdotX, brdfData.VdotY, brdfData.LdotX, brdfData.LdotY, abs(brdfData.NdotV), abs(brdfData.NdotL));

	float denom = brdfData.LdotH + brdfData.VdotH * brdfData.eta;
	denom *= denom;
	float eta2 = brdfData.eta * brdfData.eta;
	float jacobian = abs(brdfData.LdotH) / denom;

	pdf = D * V1 * max(0.0f, brdfData.VdotH) * jacobian / brdfData.NdotV;
	return pow(material.albedo, vec3(0.5f)) * (1.0f - F) * D * V2 * abs(brdfData.VdotH) * jacobian * eta2 * 4.0f;
}

vec3 EvaluateSheen(Material material, BrdfData brdfData, vec3 Csheen, out float pdf)
{
	pdf = 0.0f;
	if (brdfData.NdotL <= 0.0f)
	{
		return vec3(0.0f);
	}

	float D = D_Charlie(material.sheenRoughness, clamp(brdfData.NdotH, 0.0f, 1.0f));
	float V = V_Neubelt(brdfData.NdotV, brdfData.NdotL);

	pdf = brdfData.NdotL * ONE_OVER_PI;
	return D * V * Csheen;
}

vec3 EvaluateClearcoat(Material material, BrdfData brdfData, out float pdf)
{
	pdf = 0.0f;
	if (brdfData.NdotL <= 0.0f)
	{
		return vec3(0.0f);
	}

	float D = D_GGX(brdfData.alphaClearcoat, brdfData.NdotH);
	float V = V_Kelemen(brdfData.LdotH);
	float F = F_Schlick(MIN_DIELECTRICS_F0, 1.0f, brdfData.LdotH); // Fix IOR to 1.5

	pdf = D * brdfData.NdotH / (4.0f * brdfData.LdotH);
	return vec3(D) * V * F;
}

// -------------------------------------------------------------------------
//    Disney sampling BRDF
// -------------------------------------------------------------------------

vec3 SampleDisneyBrdf(Material material, BrdfData brdfData, inout uint rngState)
{
	// Tint colors
	vec3 Csheen, Cspec0;
	TintColors(material, brdfData, Csheen, Cspec0);

	// Energy loss
	float clearcoatEnergyLoss = 1.0f - (F_Schlick(MIN_DIELECTRICS_F0, 1.0f, brdfData.LdotH) * material.clearcoat);

	// Model weights
	float dielectricWeight = (1.0f - material.metallic) * (1.0f - material.transmission) * clearcoatEnergyLoss;
	float metalWeight = material.metallic * clearcoatEnergyLoss;
	float glassWeight = (1.0f - material.metallic) * material.transmission;
	float sheenWeight = material.sheenRoughness;
	float clearcoatWeight = material.clearcoat;

	// Lobe probabilities
	float schlickWeight = F_SchlickWeight(brdfData.NdotV);
	float diffuseProbability = dielectricWeight * Luminance(material.albedo);
	float dielectricProbability = dielectricWeight * Luminance(mix(Cspec0, vec3(1.0f), schlickWeight));
	float metalProbability = metalWeight * Luminance(mix(material.albedo, vec3(1.0f), schlickWeight));
	float glassProbability = glassWeight;
	float sheenProbability = sheenWeight;
	float clearcoatProbability = clearcoatWeight;

	// Normalize probabilities
	float invTotalWeight = 1.0f / (diffuseProbability + dielectricProbability + metalProbability + glassProbability + sheenProbability + clearcoatProbability);
	diffuseProbability *= invTotalWeight;
	dielectricProbability *= invTotalWeight;
	metalProbability *= invTotalWeight;
	glassProbability *= invTotalWeight;
	sheenProbability *= invTotalWeight;
	clearcoatProbability *= invTotalWeight;

	// CDF of the sampling probabilities
	float cdf[6];
	cdf[0] = diffuseProbability;
	cdf[1] = cdf[0] + dielectricProbability;
	cdf[2] = cdf[1] + metalProbability;
	cdf[3] = cdf[2] + glassProbability;
	cdf[4] = cdf[3] + sheenProbability;
	cdf[5] = cdf[4] + clearcoatProbability;

	// Sample Xi
	vec3 Xi = RandomValueVec3(rngState);

	// Sample a lobe based on its importance
	float rd = Xi.z;

	if (rd < cdf[0]) // Diffuse
	{
		// Sample direction
		vec3 H = HemispherepointCos(Xi.x, Xi.y);

		// To world
		H = ToWorld(brdfData.N, H);

		return H;
	}
	else if (rd < cdf[2]) // Dielectric + Metallic reflection
	{
		// Sample direction
		vec3 tangentV = ToLocal(brdfData.N, brdfData.V);
		vec3 H = ImportanceSampleVisibleGGX(tangentV, brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, Xi.xy);

		// Avoid scattered ray going into ground
		if (H.z < 0.0f)
			H = -H;

		// To world
		H = ToWorld(brdfData.N, H);

		return normalize(reflect(-brdfData.V, H));
	}
	else if (rd < cdf[3]) // Glass
	{
		// Sample direction
		vec3 tangentV = ToLocal(brdfData.N, brdfData.V);
		vec3 H = ImportanceSampleVisibleGGX(tangentV, brdfData.alphaAnisotropicX, brdfData.alphaAnisotropicY, Xi.xy);

		// Avoid scattered ray going into ground
		if (H.z < 0.0f)
			H = -H;

		// To world
		H = ToWorld(brdfData.N, H);

		float F = F_Dielectric(abs(dot(brdfData.V, H)), brdfData.eta);

		// Rescale random number for reuse
		rd = RescaleRandomNumber(rd, cdf[2], cdf[3]);

		// Reflection
		if (rd < F)
		{
			return normalize(reflect(-brdfData.V, H));
		}
		else // Transmission
		{
			return normalize(refract(-brdfData.V, H, brdfData.eta));
		}
	}
	else if (rd < cdf[4]) // Sheen
	{
		// Sample direction
		vec3 H = HemispherepointCos(Xi.x, Xi.y);

		// To world
		H = ToWorld(brdfData.N, H);

		return H;
	}
	else // Clearcoat
	{
		// Sample direction
		vec3 H = ImportanceSampleGGX(brdfData.alphaClearcoat, Xi.xy);

		// Avoid scattered ray going into ground
		if (H.z < 0.0f)
			H = -H;

		// To world
		H = ToWorld(brdfData.N, H);

		return normalize(reflect(-brdfData.V, H));
	}
}

// -------------------------------------------------------------------------
//    Disney evaluation BRDF
// -------------------------------------------------------------------------

// Based on: https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
vec3 EvaluateDisneyBrdf(Material material, BrdfData brdfData, out float pdf)
{
	vec3 f = vec3(0.0f);
	pdf = 0.0f;

	// Tint colors
	vec3 Csheen, Cspec0;
	TintColors(material, brdfData, Csheen, Cspec0);

	// Energy loss
	float clearcoatEnergyLoss = 1.0f - (F_Schlick(MIN_DIELECTRICS_F0, 1.0f, brdfData.LdotH) * material.clearcoat);

	// Model weights
	float dielectricWeight = (1.0f - material.metallic) * (1.0f - material.transmission) * clearcoatEnergyLoss;
	float metalWeight = material.metallic * clearcoatEnergyLoss;
	float glassWeight = (1.0f - material.metallic) * material.transmission;
	float sheenWeight = material.sheenRoughness;
	float clearcoatWeight = material.clearcoat;

	// Lobe probabilities
	float schlickWeight = F_SchlickWeight(brdfData.NdotV);
	float diffuseProbability = dielectricWeight * Luminance(material.albedo);
	float dielectricProbability = dielectricWeight * Luminance(mix(Cspec0, vec3(1.0f), schlickWeight));
	float metalProbability = metalWeight * Luminance(mix(material.albedo, vec3(1.0f), schlickWeight));
	float glassProbability = glassWeight;
	float sheenProbability = sheenWeight;
	float clearcoatProbability = clearcoatWeight;

	// Normalize probabilities
	float invTotalWeight = 1.0f / (diffuseProbability + dielectricProbability + metalProbability + glassProbability + sheenProbability + clearcoatProbability);
	diffuseProbability *= invTotalWeight;
	dielectricProbability *= invTotalWeight;
	metalProbability *= invTotalWeight;
	glassProbability *= invTotalWeight;
	sheenProbability *= invTotalWeight;
	clearcoatProbability *= invTotalWeight;

	bool reflection = brdfData.NdotL * brdfData.NdotV > 0.0f;

	float tmpPdf = 0.0f;
	float VdotH = abs(brdfData.VdotH);

	// Diffuse
	if (diffuseProbability > 0.0f && reflection)
	{
		f += EvaluateDisneyDiffuse(material, brdfData, tmpPdf) * dielectricWeight;
		pdf += tmpPdf * diffuseProbability;
	}

	// Dielectric Reflection
	if (dielectricProbability > 0.0f && reflection)
	{
		// Normalize for interpolating based on Cspec0
		float F = (F_Dielectric(VdotH, 1.0f / material.refraction) - brdfData.f0) / (1.0f - brdfData.f0);

		f += EvaluateMicrofacetReflection(material, brdfData, mix(Cspec0, vec3(1.0f), F), tmpPdf) * dielectricWeight;
		pdf += tmpPdf * dielectricProbability;
	}

	// Metallic reflection
	if (metalProbability > 0.0f && reflection)
	{
		// Tinted to albedo
		vec3 F = mix(material.albedo, vec3(1.0f), F_SchlickWeight(VdotH));

		f += EvaluateMicrofacetReflection(material, brdfData, F, tmpPdf) * metalWeight;
		pdf += tmpPdf * metalProbability;
	}

	// Glass/Specular BSDF
	if (glassProbability > 0.0f)
	{
		// Dielectric fresnel (achromatic)
		float F = F_Dielectric(VdotH, brdfData.eta);

		if (reflection)
		{
			f += EvaluateMicrofacetReflection(material, brdfData, vec3(F), tmpPdf) * glassWeight;
			pdf += tmpPdf * glassProbability * F;
		}
		else
		{
			f += EvaluateMicrofacetRefraction(material, brdfData, vec3(F), tmpPdf) * glassWeight;
			pdf += tmpPdf * glassProbability * (1.0f - F);
		}
	}

	// Sheen
	if (sheenProbability > 0.0f && reflection)
	{
		f += EvaluateSheen(material, brdfData, Csheen, tmpPdf) * sheenWeight;
		pdf += tmpPdf * sheenProbability;
	}

	// Clearcoat
	if (clearcoatProbability > 0.0f && reflection)
	{
		f += EvaluateClearcoat(material, brdfData, tmpPdf) * clearcoatWeight;
		pdf += tmpPdf * clearcoatProbability;
	}

	// Apply NdotL
	return f * abs(brdfData.NdotL);
}
