#version 460 core

in vec3 WorldPosition;
in vec3 WorldNormal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D DiffuseTexture;
uniform sampler2D NormalTexture;

uniform vec3 CameraPosition;

const vec3 AmbientColor = vec3(0.05f, 0.06f, 0.1f);
const vec3 LightColor = vec3(1.0f, 0.9f, 0.8f) * 4.0f;
const vec3 LightDirection = vec3(1.0f, 1.0f, 1.0f);
const float AmbientReflection = 1.0f;
const float DiffuseReflection = 1.0f;
const float SpecularReflection = 1.0f;
const float SpecularExponent = 100.0f;

vec3 GetAmbientReflection(vec3 objectColor)
{
	return AmbientColor * AmbientReflection * objectColor;
}

vec3 GetDiffuseReflection(vec3 objectColor, vec3 lightVector, vec3 normalVector)
{
	return LightColor * DiffuseReflection * objectColor * max(dot(lightVector, normalVector), 0.0f);
}

vec3 GetSpecularReflection(vec3 lightVector, vec3 viewVector, vec3 normalVector)
{
	vec3 halfVector = normalize(lightVector + viewVector);
	return LightColor * SpecularReflection * pow(max(dot(halfVector, normalVector), 0.0f), SpecularExponent);
}

vec3 GetBlinnPhongReflection(vec3 objectColor, vec3 lightVector, vec3 viewVector, vec3 normalVector)
{
	return GetAmbientReflection(objectColor)
		 + GetDiffuseReflection(objectColor, lightVector, normalVector)
		 + GetSpecularReflection(lightVector, viewVector, normalVector);
}

void main()
{
	vec4 objectColor = texture(DiffuseTexture, TexCoord);
	vec3 lightVector = normalize(LightDirection);
	vec3 viewVector = normalize(CameraPosition - WorldPosition);

	vec4 normalMap = texture(NormalTexture, TexCoord); // Hack to make OpenGL recognize active uniform
	vec3 normalVector = normalize(WorldNormal + normalMap.rgb * 0.00001f);

	FragColor = vec4(GetBlinnPhongReflection(objectColor.rgb, lightVector, viewVector, normalVector), 1.0f);
}
