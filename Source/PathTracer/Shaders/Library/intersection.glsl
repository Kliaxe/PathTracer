
// -------------------------------------------------------------------------
//    Triangle and AABB intersection
// -------------------------------------------------------------------------

// Note: Triangle order matters! Must go counter-clock wise
// Calculate the intersection of a ray with a triangle using Möller–Trumbore algorithm
// Based on: https://stackoverflow.com/a/42752998
HitInfo RayTriangle(Ray ray, BvhPrimitive primitive)
{
	vec3 posA = primitive.posAuvX.xyz;
	vec3 posB = primitive.posBuvX.xyz;
	vec3 posC = primitive.posCuvX.xyz;

	vec3 norA = primitive.norAuvY.xyz;
	vec3 norB = primitive.norBuvY.xyz;
	vec3 norC = primitive.norCuvY.xyz;

	vec2 uvA = vec2(primitive.posAuvX.w, primitive.norAuvY.w);
	vec2 uvB = vec2(primitive.posBuvX.w, primitive.norBuvY.w);
	vec2 uvC = vec2(primitive.posCuvX.w, primitive.norCuvY.w);

	vec3 edgeAB = posB - posA;
	vec3 edgeAC = posC - posA;
	vec3 normalVector = cross(edgeAB, edgeAC);
	vec3 ao = ray.origin - posA;
	vec3 dao = cross(ao, ray.direction);

	float determinant = -dot(ray.direction, normalVector);
	float invDet = 1.0f / determinant;

	// Calculate dst to triangle & barycentric coordinates of intersection point
	float dst = dot(ao, normalVector) * invDet;
	float u = dot(edgeAC, dao) * invDet;
	float v = -dot(edgeAB, dao) * invDet;
	float w = 1.0f - u - v;

	// Initialize hit info
	HitInfo hitInfo;
	hitInfo.didHit = determinant >= 1e-10 && dst >= 0.0f && u >= 0.0f && v >= 0.0f && w >= 0.0f;
	hitInfo.hitPosition = ray.origin + ray.direction * dst;
	hitInfo.hitDirection = ray.direction;
	hitInfo.dst = dst;

	// Calculate shading normal, geometry normal, and uv
	hitInfo.uv = uvA * w + uvB * u + uvC * v;
	hitInfo.shadingNormal = normalize(norA * w + norB * u + norC * v);
	hitInfo.geometryNormal = normalize(cross(edgeAC, edgeAB));

	// Calculate tangent and bitangent
	vec2 deltaUVB = uvB - uvA;
	vec2 deltaUVC = uvC - uvA;

	float invTangentDeterminant = 1.0f / (deltaUVB.x * deltaUVC.y - deltaUVB.y * deltaUVC.x);

	hitInfo.tangent = (edgeAB * deltaUVC.y - edgeAC * deltaUVB.y) * invTangentDeterminant;
	hitInfo.bitangent = (edgeAC * deltaUVB.x - edgeAB * deltaUVC.x) * invTangentDeterminant;

	return hitInfo;
}

// Return distance between ray and AABB box
float HitAABB(Ray r, vec3 AA, vec3 BB)
{
	vec3 invdir = 1.0 / r.direction;

	vec3 f = (BB - r.origin) * invdir;
	vec3 n = (AA - r.origin) * invdir;

	vec3 tmax = max(f, n);
	vec3 tmin = min(f, n);

	float t1 = min(tmax.x, min(tmax.y, tmax.z));
	float t0 = max(tmin.x, max(tmin.y, tmin.z));

	return (t1 >= t0) ? ((t0 > 0.0) ? (t0) : (t1)) : (-1);
}

// -------------------------------------------------------------------------
//    BVH hit closest
// -------------------------------------------------------------------------

HitInfo HitArrayClosest(Ray ray, int L, int R)
{
	HitInfo closestHit;
	closestHit.didHit = false;
	closestHit.dst = FLT_MAX;

	uint closestPrimitiveIndex = 0;

	for (int i = L; i <= R; i++)
	{
		BvhPrimitive primitive = bvhPrimitives[i];
		HitInfo hitInfo = RayTriangle(ray, primitive);

		if (hitInfo.didHit && hitInfo.dst < closestHit.dst)
		{
			closestHit = hitInfo;

			// Store the index of the closest primitive
			closestPrimitiveIndex = primitive.meshIndex;
		}
	}

	// After the loop, get and evaluate the material
	if (closestHit.didHit)
	{
		closestHit.primitiveIndex = closestPrimitiveIndex;
	}

	return closestHit;
}

// Get closest HitInfo by intersecting with Bvh nodes and Bvh primitives
HitInfo HitBvhClosest(Ray ray)
{
	HitInfo closestHit;
	closestHit.didHit = false;
	closestHit.dst = FLT_MAX;

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
			int L = node.index;
			int R = node.index + node.n - 1;

			// Go through all primitives inside of BVH node range and estimate closest hit
			HitInfo hitInfo = HitArrayClosest(ray, L, R);

			// Out of the other nodes, did this one perform better?
			if (hitInfo.didHit && hitInfo.dst < closestHit.dst)
			{
				closestHit = hitInfo;
			}
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
				break;
			}
		}
	}

	return closestHit;
}

// -------------------------------------------------------------------------
//    BVH hit any
// -------------------------------------------------------------------------

HitInfo HitArrayAny(Ray ray, int L, int R)
{
	HitInfo anyHit;
	anyHit.didHit = false;
	anyHit.dst = FLT_MAX;

	for (int i = L; i <= R; i++)
	{
		BvhPrimitive primitive = bvhPrimitives[i];
		HitInfo hitInfo = RayTriangle(ray, primitive);

		if (hitInfo.didHit)
		{
			anyHit = hitInfo;

			// Exit the loop as soon as a hit is found
			break;
		}
	}

	return anyHit;
}

// Get any HitInfo by intersecting with Bvh nodes and Bvh primitives
// No materials are retrieved and evaluated
HitInfo HitBvhAny(Ray ray)
{
	HitInfo anyHit;
	anyHit.didHit = false;
	anyHit.dst = FLT_MAX;

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
			int L = node.index;
			int R = node.index + node.n - 1;

			// Go through all primitives inside of BVH node range and get first found primitive
			HitInfo hitInfo = HitArrayAny(ray, L, R);

			// Out of the other nodes, did this one perform better?
			if (hitInfo.didHit)
			{
				anyHit = hitInfo;
				break;
			}
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
				break;
			}
		}
	}

	return anyHit;
}