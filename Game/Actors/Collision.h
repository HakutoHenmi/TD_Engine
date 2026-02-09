#pragma once
#include "AABB.h"
#include "Matrix4x4.h"

namespace Engine {
namespace Collision {

// 線分 vs AABB
// 戻り値: 衝突したかどうか
// outT: 衝突までの比率(0〜1)
// outNormal: 衝突面の法線
bool IntersectSegmentAABB(const Vector3& p0, const Vector3& p1, const AABB& box, float& outT, Vector3& outNormal);

// レイ vs AABB
bool IntersectRayAABB(const Vector3& origin, const Vector3& dir, const AABB& box, float& outT, Vector3& outNormal);

bool IntersectRayAABBExpanded(
    const Vector3& origin, const Vector3& dir, const AABB& box, float expand, // ← 拡張幅
    float& outT, Vector3& outNormal);

//線分 vs 球（レーザー用）
bool IntersectSegmentSphere(const Vector3& p0, const Vector3& p1, const Vector3& center, float radius, float& outT, Vector3& outNormal);


} // namespace Collision
} // namespace Engine
