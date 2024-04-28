#pragma once
#include "glm/glm.hpp"
#include <vector>

class BVH
{
public:
	struct BvhNode
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

	struct BvhPrimitive 
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

		unsigned int meshIndex;
	};

	struct alignas(16) BvhNodeAlign
	{
		int left;
		int right;
		int n; 
		int index;
		alignas(16) glm::vec3 AA;
		alignas(16) glm::vec3 BB;
	};

	struct alignas(16) BvhPrimitiveAlign
	{
		alignas(16) glm::vec4 posAuvX;
		alignas(16) glm::vec4 norAuvY;

		alignas(16) glm::vec4 posBuvX;
		alignas(16) glm::vec4 norBuvY;

		alignas(16) glm::vec4 posCuvX;
		alignas(16) glm::vec4 norCuvY;

		alignas(16) unsigned int meshIndex;
	};

	// Construct BVH
	static int BuildBvh(std::vector<BvhPrimitive>& triangles, std::vector<BvhNode>& nodes, int l, int r, int n);
	static int BuildBvhWithSah(std::vector<BvhPrimitive>& triangles, std::vector<BvhNode>& nodes, int l, int r, int n);
};