
// -------------------------------------------------------------------------
//    Debugging utilities
// -------------------------------------------------------------------------

// Debug HDRI Cache: Draw red dots on important sources of light
vec4 DebugHdriCache(vec2 uv)
{
	vec3 hdriColor = SampleBindlessTexture(environment.hdriHandle, vec2(uv.x, -uv.y)).rgb;

	// Sample points
	for (uint i = 0u; i < 500u; i++)
	{
		uint XiState = i;
		vec2 Xi = RandomValueVec2(XiState);

		vec2 texcoord = SampleBindlessTexture(environment.hdriCacheHandle, Xi).rg;
		texcoord.y = 1.0f - texcoord.y;

		// Mark areas red that follow HDRI cache
		if (distance(uv, texcoord) < 0.005f)
		{
			hdriColor = vec3(1.0f, 0.0f, 0.0f);
		}
	}

	return vec4(hdriColor, 1.0f);
}

// Debug BVH: Count BVH node intersections on a given ray
vec4 DebugBvh(Ray ray)
{
	uint hitCounter = 0;

	// Stack
	int stack[BVH_STACKSIZE];
	int stackPointer = 0;

	stack[stackPointer++] = 1;
	while (stackPointer > 0)
	{
		int top = stack[--stackPointer];
		BvhNode node = bvhNodes[top];

		// If node is leaf, traverse primitives and find intersection
		if (node.n > 0)
		{
			hitCounter++;
		}
		else
		{
			if (stackPointer < BVH_STACKSIZE)
			{
				// Find intersection with left and right boxes AABB
				float dstLeft = FLT_MAX;
				float dstRight = FLT_MAX;

				if (node.left > 0.0f)
				{
					BvhNode leftNode = bvhNodes[node.left];
					dstLeft = HitAABB(ray, leftNode.AA, leftNode.BB);
				}

				if (node.right > 0.0f)
				{
					BvhNode rightNode = bvhNodes[node.right];
					dstRight = HitAABB(ray, rightNode.AA, rightNode.BB);
				}

				// Search in recent boxes
				if (dstLeft > 0.0f && dstRight > 0.0f)
				{
					if (dstLeft < dstRight)
					{
						// Left first
						stack[stackPointer++] = node.right;
						stack[stackPointer++] = node.left;
					}
					else
					{
						// Right first
						stack[stackPointer++] = node.left;
						stack[stackPointer++] = node.right;
					}
				}
				else if (dstLeft > 0.0f)
				{
					stack[stackPointer++] = node.left;
				}
				else if (dstRight > 0.0f)
				{
					stack[stackPointer++] = node.right;
				}
			}
			else
			{
				hitCounter = 0xFFFFFFFF;
				break;
			}
		}
	}

	if (hitCounter == 0xFFFFFFFF)
	{
		return vec4(1.0f, 0.0f, 1.0f, 1.0f); // error: stack overflow (purple)
	}

	// Heat map
	const vec3 mapTex[] = {
		vec3(0.0f, 0.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f),
		vec3(0.0f, 1.0f, 1.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(1.0f, 1.0f, 0.0f),
		vec3(1.0f, 0.0f, 0.0f),
	};

	const float mapTexLen = 5.0f;
	const float maxHeat = 100.0f;
	float l = clamp(float(hitCounter) / maxHeat, 0.0f, 1.0f) * mapTexLen;
	vec3 a = mapTex[uint(floor(l))];
	vec3 b = mapTex[uint(ceil(l))];
	vec3 heatmap = mix(a, b, l - floor(l));

	return vec4(heatmap, 1.0f);
}

// Choose debug output function
vec4 DebugOutput(vec2 uv, Ray debugRay)
{
#if defined(DEBUG_HDRI_CACHE)
	return DebugHdriCache(uv);
#elif defined(DEBUG_BVH)
	return DebugBvh(debugRay);
#endif
	return vec4(0.0f);
}
