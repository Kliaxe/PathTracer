#include "BVH.h"
#include <algorithm>

#define INF 114514.0f

bool CompareX(const BVH::BvhPrimitive& t1, const BVH::BvhPrimitive& t2)
{
	glm::vec3 center1 = (t1.posA + t1.posB + t1.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	glm::vec3 center2 = (t2.posA + t2.posB + t2.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	return center1.x < center2.x;
}

bool CompareY(const BVH::BvhPrimitive& t1, const BVH::BvhPrimitive& t2) {
	glm::vec3 center1 = (t1.posA + t1.posB + t1.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	glm::vec3 center2 = (t2.posA + t2.posB + t2.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	return center1.y < center2.y;
}

bool CompareZ(const BVH::BvhPrimitive& t1, const BVH::BvhPrimitive& t2)
{
	glm::vec3 center1 = (t1.posA + t1.posB + t1.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	glm::vec3 center2 = (t2.posA + t2.posB + t2.posC) / glm::vec3(3.0f, 3.0f, 3.0f);
	return center1.z < center2.z;
}

int BVH::BuildBvh(std::vector<BvhPrimitive>& primitives, std::vector<BvhNode>& nodes, int l, int r, int n)
{
    if (l > r) return 0;

    // Note:
    // This cannot be operated by pointers, references, etc., and nodes[id] must be used to operate.
    // Because std::vector<> will be copied to a larger memory when it is expanded, then the address will change.
    // Pointers and references all point to the original memory, so an error will occur.
    nodes.push_back(BvhNode());
    size_t id = nodes.size() - 1;

    // Initialize values
    nodes[id].left = nodes[id].right = nodes[id].n = nodes[id].index = 0;
    nodes[id].AA = glm::vec3(1145141919.0f, 1145141919.0f, 1145141919.0f);
    nodes[id].BB = glm::vec3(-1145141919.0f, -1145141919.0f, -1145141919.0f);

    // Calculate AABB
    for (int i = l; i <= r; i++) 
    {
        // Minimum AA
        float minx = glm::min(primitives[i].posA.x, glm::min(primitives[i].posB.x, primitives[i].posC.x));
        float miny = glm::min(primitives[i].posA.y, glm::min(primitives[i].posB.y, primitives[i].posC.y));
        float minz = glm::min(primitives[i].posA.z, glm::min(primitives[i].posB.z, primitives[i].posC.z));
        nodes[id].AA.x = glm::min(nodes[id].AA.x, minx);
        nodes[id].AA.y = glm::min(nodes[id].AA.y, miny);
        nodes[id].AA.z = glm::min(nodes[id].AA.z, minz);

        // Maximum BB
        float maxx = glm::max(primitives[i].posA.x, glm::max(primitives[i].posB.x, primitives[i].posC.x));
        float maxy = glm::max(primitives[i].posA.y, glm::max(primitives[i].posB.y, primitives[i].posC.y));
        float maxz = glm::max(primitives[i].posA.z, glm::max(primitives[i].posB.z, primitives[i].posC.z));
        nodes[id].BB.x = glm::max(nodes[id].BB.x, maxx);
        nodes[id].BB.y = glm::max(nodes[id].BB.y, maxy);
        nodes[id].BB.z = glm::max(nodes[id].BB.z, maxz);
    }

    // No more than 'n' primitives return leaf nodes
    if ((r - l + 1) <= n) 
    {
        nodes[id].n = r - l + 1;
        nodes[id].index = l;
        return int(id);
    }

    // Else recursively build the tree
    float lenx = nodes[id].BB.x - nodes[id].AA.x;
    float leny = nodes[id].BB.y - nodes[id].AA.y;
    float lenz = nodes[id].BB.z - nodes[id].AA.z;

    // Split by best found axis
    if (lenx >= leny && lenx >= lenz) std::sort(primitives.begin() + l, primitives.begin() + r + 1, CompareX);
    if (leny >= lenx && leny >= lenz) std::sort(primitives.begin() + l, primitives.begin() + r + 1, CompareY);
    if (lenz >= lenx && lenz >= leny) std::sort(primitives.begin() + l, primitives.begin() + r + 1, CompareZ);

    // Recursion
    int mid = (l + r) / 2;
    int left = BuildBvh(primitives, nodes, l, mid, n);
    int right = BuildBvh(primitives, nodes, mid + 1, r, n);

    nodes[id].left = left;
    nodes[id].right = right;

    return int(id);
}

int BVH::BuildBvhWithSah(std::vector<BvhPrimitive>& primitives, std::vector<BvhNode>& nodes, int l, int r, int n)
{
    if (l > r) return 0;

    nodes.push_back(BvhNode());
    size_t id = nodes.size() - 1;

    // Initialize values
    nodes[id].left = nodes[id].right = nodes[id].n = nodes[id].index = 0;
    nodes[id].AA = glm::vec3(1145141919.0f, 1145141919.0f, 1145141919.0f);
    nodes[id].BB = glm::vec3(-1145141919.0f, -1145141919.0f, -1145141919.0f);

    // Calculate AABB
    for (int i = l; i <= r; i++) 
    {
        // Minimum AA
        float minx = glm::min(primitives[i].posA.x, glm::min(primitives[i].posB.x, primitives[i].posC.x));
        float miny = glm::min(primitives[i].posA.y, glm::min(primitives[i].posB.y, primitives[i].posC.y));
        float minz = glm::min(primitives[i].posA.z, glm::min(primitives[i].posB.z, primitives[i].posC.z));
        nodes[id].AA.x = glm::min(nodes[id].AA.x, minx);
        nodes[id].AA.y = glm::min(nodes[id].AA.y, miny);
        nodes[id].AA.z = glm::min(nodes[id].AA.z, minz);

        // Maximum BB
        float maxx = glm::max(primitives[i].posA.x, glm::max(primitives[i].posB.x, primitives[i].posC.x));
        float maxy = glm::max(primitives[i].posA.y, glm::max(primitives[i].posB.y, primitives[i].posC.y));
        float maxz = glm::max(primitives[i].posA.z, glm::max(primitives[i].posB.z, primitives[i].posC.z));
        nodes[id].BB.x = glm::max(nodes[id].BB.x, maxx);
        nodes[id].BB.y = glm::max(nodes[id].BB.y, maxy);
        nodes[id].BB.z = glm::max(nodes[id].BB.z, maxz);
    }

    // No more than 'n' primitives return leaf nodes
    if ((r - l + 1) <= n) 
    {
        nodes[id].n = r - l + 1;
        nodes[id].index = l;
        return int(id);
    }

    // Else recursively build the tree
    float Cost = INF;
    int Axis = 0;
    int Split = (l + r) / 2;
    for (int axis = 0; axis < 3; axis++) 
    {
        // Sort by x, y, z axis respectively
        if (axis == 0) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareX);
        if (axis == 1) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareY);
        if (axis == 2) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareZ);

        // leftMax[i]: The largest xyz value in [l, i]
        // leftMin[i]: The smallest xyz value in [l, i]
        std::vector<glm::vec3> leftMax(r - l + 1, glm::vec3(-INF, -INF, -INF));
        std::vector<glm::vec3> leftMin(r - l + 1, glm::vec3(INF, INF, INF));

        // Compute prefixes node i-l to align to index 0
        for (int i = l; i <= r; i++) 
        {
            BvhPrimitive& t = primitives[i];
            int bias = (i == l) ? 0 : 1;  // Bias for the first element

            leftMax[i - l].x = glm::max(leftMax[i - l - bias].x, glm::max(t.posA.x, glm::max(t.posB.x, t.posC.x)));
            leftMax[i - l].y = glm::max(leftMax[i - l - bias].y, glm::max(t.posA.y, glm::max(t.posB.y, t.posC.y)));
            leftMax[i - l].z = glm::max(leftMax[i - l - bias].z, glm::max(t.posA.z, glm::max(t.posB.z, t.posC.z)));

            leftMin[i - l].x = glm::min(leftMin[i - l - bias].x, glm::min(t.posA.x, glm::min(t.posB.x, t.posC.x)));
            leftMin[i - l].y = glm::min(leftMin[i - l - bias].y, glm::min(t.posA.y, glm::min(t.posB.y, t.posC.y)));
            leftMin[i - l].z = glm::min(leftMin[i - l - bias].z, glm::min(t.posA.z, glm::min(t.posB.z, t.posC.z)));
        }

        // rightMax[i]: The largest xyz value in [i, r]
        // rightMin[i]: The smallest xyz value in [i, r]
        std::vector<glm::vec3> rightMax(r - l + 1, glm::vec3(-INF, -INF, -INF));
        std::vector<glm::vec3> rightMin(r - l + 1, glm::vec3(INF, INF, INF));

        // Compute suffixes node i-l to align to index 0
        for (int i = r; i >= l; i--) 
        {
            BvhPrimitive& t = primitives[i];
            int bias = (i == r) ? 0 : 1;  // Bias for the first element

            rightMax[i - l].x = glm::max(rightMax[i - l + bias].x, glm::max(t.posA.x, glm::max(t.posB.x, t.posC.x)));
            rightMax[i - l].y = glm::max(rightMax[i - l + bias].y, glm::max(t.posA.y, glm::max(t.posB.y, t.posC.y)));
            rightMax[i - l].z = glm::max(rightMax[i - l + bias].z, glm::max(t.posA.z, glm::max(t.posB.z, t.posC.z)));

            rightMin[i - l].x = glm::min(rightMin[i - l + bias].x, glm::min(t.posA.x, glm::min(t.posB.x, t.posC.x)));
            rightMin[i - l].y = glm::min(rightMin[i - l + bias].y, glm::min(t.posA.y, glm::min(t.posB.y, t.posC.y)));
            rightMin[i - l].z = glm::min(rightMin[i - l + bias].z, glm::min(t.posA.z, glm::min(t.posB.z, t.posC.z)));
        }

        // Traverse to find splits
        float cost = INF;
        int split = l;
        for (int i = l; i <= r - 1; i++) 
        {
            float lenx, leny, lenz;

            // Left side [l, i]
            glm::vec3 leftAA = leftMin[i - l];
            glm::vec3 leftBB = leftMax[i - l];
            lenx = leftBB.x - leftAA.x;
            leny = leftBB.y - leftAA.y;
            lenz = leftBB.z - leftAA.z;
            float leftS = 2.0f * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float leftCost = leftS * (i - l + 1);

            // Right side [i+1, r]
            glm::vec3 rightAA = rightMin[i + 1 - l];
            glm::vec3 rightBB = rightMax[i + 1 - l];
            lenx = rightBB.x - rightAA.x;
            leny = rightBB.y - rightAA.y;
            lenz = rightBB.z - rightAA.z;
            float rightS = 2.0f * ((lenx * leny) + (lenx * lenz) + (leny * lenz));
            float rightCost = rightS * (r - i);

            // Save the minimum result for each split
            float totalCost = leftCost + rightCost;
            if (totalCost < cost) 
            {
                cost = totalCost;
                split = i;
            }
        }

        // Save best result for each axis
        if (cost < Cost) 
        {
            Cost = cost;
            Axis = axis;
            Split = split;
        }
    }

    // Split by best found axis
    if (Axis == 0) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareX);
    if (Axis == 1) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareY);
    if (Axis == 2) std::sort(&primitives[0] + l, &primitives[0] + r + 1, CompareZ);

    // Recursion
    int left = BuildBvhWithSah(primitives, nodes, l, Split, n);
    int right = BuildBvhWithSah(primitives, nodes, Split + 1, r, n);

    nodes[id].left = left;
    nodes[id].right = right;

    return int(id);
}
