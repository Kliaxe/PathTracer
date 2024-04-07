#pragma once
#include "glm/glm.hpp"
#include <vector>

class BVH
{
public:
	struct BVHNode
	{
		// Left and right subtree index
		int left;
		int right;

		// Leaf node information
		int n;
		int index;

		// Collision box
		glm::vec3 AA;
		glm::vec3 BB;
	};

	struct BVHPrimitive 
	{
		glm::vec3 posA;
		glm::vec3 posB;
		glm::vec3 posC;

		glm::vec3 norA;
		glm::vec3 norB;
		glm::vec3 norC;

		glm::vec2 uvA;
		glm::vec2 uvB;
		glm::vec2 uvC;

		//unsigned int verticesStartIndex;
		//unsigned int indicesStartIndex;
	};

	struct BVHNodeAlign
	{
		int left;
		int right;
		int n; 
		int index;
		glm::vec3 AA; float _padding1;
		glm::vec3 BB; float _padding2;
	};

	struct BVHPrimitiveAlign
	{
		glm::vec4 posAuvX;
		glm::vec4 norAuvY;

		glm::vec4 posBuvX;
		glm::vec4 norBuvY;

		glm::vec4 posCuvX;
		glm::vec4 norCuvY;

		//unsigned int verticesStartIndex;
		//unsigned int indicesStartIndex;
	};

	// Construct BVH
	static int BuildBVH(std::vector<BVHPrimitive>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n);
	static int BuildBVHWithSAH(std::vector<BVHPrimitive>& triangles, std::vector<BVHNode>& nodes, int l, int r, int n);
};