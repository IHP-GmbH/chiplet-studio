/**
 * BVH.cpp - Bounding Volume Hierarchy implementation
 */

#include "BVH.h"
#include <algorithm>
#include <limits>

namespace chiplet {

void BVH::build(const std::vector<AA_BOUNDING_BOX>& boxes,
                const std::vector<int>& indices)
{
    clear();

    if (boxes.empty() || boxes.size() != indices.size()) {
        return;
    }

    // Create primitive list with bounding boxes and indices
    std::vector<std::pair<AA_BOUNDING_BOX, int>> primitives;
    primitives.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        primitives.emplace_back(boxes[i], indices[i]);
    }

    // Build tree recursively
    m_root = buildRecursive(primitives, 0, static_cast<int>(primitives.size()), 0);
}

void BVH::clear()
{
    m_root.reset();
    m_nodeCount = 0;
    m_maxDepth = 0;
    m_leafCount = 0;
}

std::unique_ptr<BVHNode> BVH::buildRecursive(
    std::vector<std::pair<AA_BOUNDING_BOX, int>>& primitives,
    int start, int end, int depth)
{
    if (start >= end) {
        return nullptr;
    }

    auto node = std::make_unique<BVHNode>();
    m_nodeCount++;
    m_maxDepth = std::max(m_maxDepth, depth + 1);

    // Compute bounds for all primitives in range
    node->bounds = primitives[start].first;
    for (int i = start + 1; i < end; ++i) {
        node->bounds.AddBounds(primitives[i].first);
    }

    int count = end - start;

    // Leaf node: single primitive
    if (count == 1) {
        node->primitiveIndex = primitives[start].second;
        m_leafCount++;
        return node;
    }

    // Find split axis (longest axis of bounding box)
    int axis = node->bounds.longestAxis();

    // Sort primitives along split axis by centroid
    std::sort(primitives.begin() + start, primitives.begin() + end,
        [axis](const std::pair<AA_BOUNDING_BOX, int>& a,
               const std::pair<AA_BOUNDING_BOX, int>& b) {
            VECTOR3D centerA = a.first.center();
            VECTOR3D centerB = b.first.center();
            switch (axis) {
                case 0: return centerA.x < centerB.x;
                case 1: return centerA.y < centerB.y;
                default: return centerA.z < centerB.z;
            }
        });

    // Split at midpoint
    int mid = start + count / 2;

    // Recursively build children
    node->left = buildRecursive(primitives, start, mid, depth + 1);
    node->right = buildRecursive(primitives, mid, end, depth + 1);

    return node;
}

std::vector<int> BVH::rayQuery(const VECTOR3D& origin,
                                const VECTOR3D& direction) const
{
    std::vector<std::pair<float, int>> hits;

    if (m_root) {
        rayQueryRecursive(m_root.get(), origin, direction, hits);
    }

    // Sort by distance and extract indices
    std::sort(hits.begin(), hits.end(),
        [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
            return a.first < b.first;
        });

    std::vector<int> result;
    result.reserve(hits.size());
    for (const auto& hit : hits) {
        result.push_back(hit.second);
    }
    return result;
}

void BVH::rayQueryRecursive(const BVHNode* node,
                             const VECTOR3D& origin,
                             const VECTOR3D& direction,
                             std::vector<std::pair<float, int>>& hits) const
{
    if (!node) {
        return;
    }

    // Test ray against node bounds
    float tMin, tMax;
    if (!node->bounds.rayIntersect(origin, direction, tMin, tMax)) {
        return;  // Ray misses this node entirely
    }

    // Leaf node: add to hits
    if (node->isLeaf()) {
        hits.emplace_back(tMin, node->primitiveIndex);
        return;
    }

    // Internal node: recurse into children
    rayQueryRecursive(node->left.get(), origin, direction, hits);
    rayQueryRecursive(node->right.get(), origin, direction, hits);
}

int BVH::rayQueryClosest(const VECTOR3D& origin,
                          const VECTOR3D& direction,
                          const std::function<float(int)>& getDistance) const
{
    // Get all hits
    std::vector<int> candidates = rayQuery(origin, direction);

    if (candidates.empty()) {
        return -1;
    }

    // Find closest using the provided distance function
    int closestIdx = -1;
    float closestDist = std::numeric_limits<float>::max();

    for (int idx : candidates) {
        float dist = getDistance(idx);
        if (dist >= 0.0f && dist < closestDist) {
            closestDist = dist;
            closestIdx = idx;
        }
    }

    return closestIdx;
}

std::vector<int> BVH::frustumQuery(const FRUSTUM& frustum) const
{
    std::vector<int> visible;

    if (m_root) {
        frustumQueryRecursive(m_root.get(), frustum, visible);
    }

    return visible;
}

void BVH::frustumQueryRecursive(const BVHNode* node,
                                 const FRUSTUM& frustum,
                                 std::vector<int>& visible) const
{
    if (!node) {
        return;
    }

    // Test node bounds against frustum
    // FRUSTUM::IsAABoundingBoxInside returns true if box is at least partially inside
    if (!frustum.IsAABoundingBoxInside(node->bounds)) {
        return;  // Entire subtree is outside frustum
    }

    // Leaf node: add to visible list
    if (node->isLeaf()) {
        visible.push_back(node->primitiveIndex);
        return;
    }

    // Internal node: recurse into children
    frustumQueryRecursive(node->left.get(), frustum, visible);
    frustumQueryRecursive(node->right.get(), frustum, visible);
}

} // namespace chiplet
