
// -------------------------------------------------------------------------
//    Uniforms
// -------------------------------------------------------------------------

uniform mat4 ViewMatrix;
uniform mat4 ProjMatrix;
uniform mat4 InvProjMatrix;
uniform uint FrameCount;
uniform vec2 FrameDimensions;

uniform uint AntiAliasingEnabled;
uniform float FocalLength;
uniform float ApertureSize;
uniform vec2 ApertureShape;

uniform float SpecularModifier;
uniform float SpecularTintModifier;
uniform float MetallicModifier;
uniform float RoughnessModifier;
uniform float SubsurfaceModifier;
uniform float AnisotropyModifier;
uniform float SheenRoughnessModifier;
uniform float SheenTintModifier;
uniform float ClearcoatModifier;
uniform float ClearcoatRoughnessModifier;
uniform float RefractionModifier;
uniform float TransmissionModifier;

uniform float DebugValueA;
uniform float DebugValueB;

// -------------------------------------------------------------------------
//    Buffers
// -------------------------------------------------------------------------

layout(std430, binding = 0) readonly buffer EnvironmentBuffer
{
    Environment environment;
};

layout(std430, binding = 1) readonly buffer MaterialBuffer
{
    Material materials[];
};

layout(std430, binding = 2) readonly buffer BvhNodeBuffer
{
    BvhNode bvhNodes[];
};

layout(std430, binding = 3) readonly buffer BvhPrimitiveBuffer
{
    BvhPrimitive bvhPrimitives[];
};
