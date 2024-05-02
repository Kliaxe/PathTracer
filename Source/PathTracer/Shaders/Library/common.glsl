
// -------------------------------------------------------------------------
//    Constants
// -------------------------------------------------------------------------

const float PI = 3.141592653589f;
const float TWO_PI = 6.283185307178f;
const float ONE_OVER_PI = 0.318309886183f;
const float ONE_OVER_TWO_PI = 0.159154943091f;

const float FLT_MAX = 3.402823466e+38f;

const float MIN_DIELECTRICS_F0 = 0.04f;

#define BVH_STACKSIZE 16

// -------------------------------------------------------------------------
//    Configuration
// -------------------------------------------------------------------------

//#define DEBUG_HDRI_CACHE
//#define DEBUG_BVH

#if defined(DEBUG_HDRI_CACHE) || defined(DEBUG_BVH)
#define DEBUG_ENABLED
#endif

// -------------------------------------------------------------------------
//    Buffer structs
// -------------------------------------------------------------------------

struct Environment
{
	uint64_t hdriHandle;
	uint64_t hdriCacheHandle;
	vec2 hdriDimensions;
};

struct Material
{
	uint64_t emissionTextureHandle;
	uint64_t albedoTextureHandle;
	uint64_t normalTextureHandle;
	uint64_t specularTextureHandle;
	uint64_t specularColorTextureHandle;
	uint64_t metallicRoughnessTextureHandle;
	uint64_t sheenRoughnessTextureHandle;
	uint64_t sheenColorTextureHandle;
	uint64_t clearcoatTextureHandle;
	uint64_t clearcoatRoughnessTextureHandle;
	uint64_t transmissionTextureHandle;

	vec3 emission;
	vec3 albedo;
	float specular;
	float specularTint;
	float metallic;
	float roughness;
	float subsurface;
	float anisotropy;
	float sheenRoughness;
	float sheenTint;
	float clearcoat;
	float clearcoatRoughness;
	float refraction;
	float transmission;
};

struct BvhNode
{
	int left;   // Left subtree
	int right;  // Right subtree
	int n;      // Number of primitives
	int index;	// Primitive index
	vec3 AA;	// Bounding box AA
	vec3 BB;	// Bounding box BB
};

struct BvhPrimitive
{
	vec4 posAuvX;
	vec4 norAuvY;

	vec4 posBuvX;
	vec4 norBuvY;

	vec4 posCuvX;
	vec4 norCuvY;

	uint meshIndex;
};

// -------------------------------------------------------------------------
//    Common structs
// -------------------------------------------------------------------------

struct Ray
{
	vec3 origin;
	vec3 direction;
};

struct HitInfo
{
	bool didHit;
	vec3 hitPosition;
	vec3 hitDirection;
	float dst;
	vec2 uv;
	vec3 shadingNormal;
	vec3 geometryNormal;
	vec3 tangent;
	vec3 bitangent;
	uint primitiveIndex;
};

struct BrdfData
{
	// Roughnesses
	float roughness;				// perceptively linear roughness (artist's input)
	float alpha;					// linear roughness - often 'alpha' in specular BRDF equations
	float alphaSquared;				// alpha squared - pre-calculated value commonly used in BRDF equations
	float roughnessClearcoat;
	float alphaClearcoat;
	float alphaSquaredClearcoat;
	float alphaAnisotropicX;		// Anisotropic specific roughness value based on tangent
	float alphaAnisotropicY;		// Anisotropic specific roughness value based on bitangent

	// Vectors
	vec3 V; // Direction to viewer (or opposite direction of indident ray)
	vec3 N; // Shading normal
	vec3 L; // Direction to light (or direction of reflecting ray)
	vec3 H; // Half vector (microfacet normal)
	vec3 X;	// Tangent
	vec3 Y; // Bitangent

	// Angles
	float NdotV;
	float NdotL;
	float VdotH;
	float NdotH;
	float LdotH;
	float VdotX;
	float LdotX;
	float HdotX;
	float VdotY;
	float LdotY;
	float HdotY;

	// Common used terms for BRDF evaluation
	float eta;
	float f0;
	float f90;
};
