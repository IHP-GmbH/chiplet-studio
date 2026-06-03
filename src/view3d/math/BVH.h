// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * BVH.h - Bounding Volume Hierarchy for spatial acceleration
 *
 * Provides O(log N) ray casting and frustum culling queries for component picking
 * and visibility determination.
 */

#ifndef CHIPLET_VIEW3D_MATH_BVH_H
#define CHIPLET_VIEW3D_MATH_BVH_H

#include "Maths.h"  // Includes all math types: VECTOR3D, MATRIX4X4, AA_BOUNDING_BOX, FRUSTUM
#include <vector>
#include <memory>
#include <functional>

namespace chiplet {

/**
 * BVH Node - internal structure for the tree
 */
struct BVHNode {
    AA_BOUNDING_BOX bounds;
    std::unique_ptr<BVHNode> left;
    std::unique_ptr<BVHNode> right;
    int primitiveIndex = -1;  // -1 for internal nodes, >= 0 for leaves

    bool isLeaf() const { return primitiveIndex >= 0; }
};

/**
 * BVH - Bounding Volume Hierarchy
 *
 * A binary tree of axis-aligned bounding boxes for efficient spatial queries.
 * Construction uses midpoint split along the longest axis.
 */
class BVH {
public:
    BVH() = default;
    ~BVH() = default;

    // Non-copyable but movable
    BVH(const BVH&) = delete;
    BVH& operator=(const BVH&) = delete;
    BVH(BVH&&) = default;
    BVH& operator=(BVH&&) = default;

    /**
     * Build BVH from a list of bounding boxes.
     * @param boxes Vector of axis-aligned bounding boxes
     * @param indices Vector of primitive indices (must match boxes size)
     */
    void build(const std::vector<AA_BOUNDING_BOX>& boxes,
               const std::vector<int>& indices);

    /**
     * Clear the BVH tree.
     */
    void clear();

    /**
     * Test if BVH is empty.
     */
    bool empty() const { return m_root == nullptr; }

    /**
     * Ray query - find all intersecting primitives.
     * @param origin Ray origin point
     * @param direction Ray direction (does not need to be normalized)
     * @return Vector of primitive indices that the ray intersects
     */
    std::vector<int> rayQuery(const VECTOR3D& origin,
                              const VECTOR3D& direction) const;

    /**
     * Ray query for closest hit.
     * @param origin Ray origin point
     * @param direction Ray direction
     * @param getDistance Function to compute precise distance for a primitive index
     * @return Index of closest primitive, or -1 if no hit
     */
    int rayQueryClosest(const VECTOR3D& origin,
                        const VECTOR3D& direction,
                        const std::function<float(int)>& getDistance) const;

    /**
     * Frustum query - find all primitives inside or intersecting the frustum.
     * @param frustum View frustum for visibility testing
     * @return Vector of visible primitive indices
     */
    std::vector<int> frustumQuery(const FRUSTUM& frustum) const;

    /**
     * Get statistics for debugging.
     */
    int nodeCount() const { return m_nodeCount; }
    int maxDepth() const { return m_maxDepth; }
    int leafCount() const { return m_leafCount; }

private:
    /**
     * Recursive BVH construction using midpoint split.
     */
    std::unique_ptr<BVHNode> buildRecursive(
        std::vector<std::pair<AA_BOUNDING_BOX, int>>& primitives,
        int start, int end, int depth);

    /**
     * Recursive ray traversal.
     */
    void rayQueryRecursive(const BVHNode* node,
                           const VECTOR3D& origin,
                           const VECTOR3D& direction,
                           std::vector<std::pair<float, int>>& hits) const;

    /**
     * Recursive frustum traversal.
     */
    void frustumQueryRecursive(const BVHNode* node,
                               const FRUSTUM& frustum,
                               std::vector<int>& visible) const;

    std::unique_ptr<BVHNode> m_root;
    int m_nodeCount = 0;
    int m_maxDepth = 0;
    int m_leafCount = 0;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_MATH_BVH_H
