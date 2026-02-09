#include "Collision.h"
#include <algorithm>
#include <cmath>

namespace Engine {
namespace Collision {

bool IntersectSegmentAABB(const Vector3& p0, const Vector3& p1, const AABB& box, float& outT, Vector3& outNormal) {
	float tmin = 0.0f;
	float tmax = 1.0f; // 線分なので範囲は 0〜1
	Vector3 dir{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};

	outNormal = {0, 0, 0};

	// 各軸ごとにスラブ判定
	for (int axis = 0; axis < 3; axis++) {
		float p = (axis == 0 ? p0.x : (axis == 1 ? p0.y : p0.z));
		float d = (axis == 0 ? dir.x : (axis == 1 ? dir.y : dir.z));
		float minB = (axis == 0 ? box.min.x : (axis == 1 ? box.min.y : box.min.z));
		float maxB = (axis == 0 ? box.max.x : (axis == 1 ? box.max.y : box.max.z));

		if (fabs(d) < 1e-6f) {
			if (p < minB || p > maxB)
				return false; // 平行＆外
		} else {
			float t1 = (minB - p) / d;
			float t2 = (maxB - p) / d;
			if (t1 > t2)
				std::swap(t1, t2);

			if (t1 > tmin) {
				tmin = t1;
				outNormal = {0, 0, 0};
				if (axis == 0)
					outNormal.x = (d > 0) ? -1.0f : 1.0f;
				if (axis == 1)
					outNormal.y = (d > 0) ? -1.0f : 1.0f;
				if (axis == 2)
					outNormal.z = (d > 0) ? -1.0f : 1.0f;
			}
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return false;
		}
	}
	outT = tmin;
	return true;
}

// Collision.cpp
bool Collision::IntersectRayAABB(const Vector3& origin, const Vector3& dir, const AABB& box, float& outT, Vector3& outNormal) {
	float tmin = -1e9f;
	float tmax = 1e9f;
	int hitAxis = -1;
	float sign = 0.0f;
	outNormal = {0, 0, 0};

	// --- 各軸スラブ法 ---
	for (int axis = 0; axis < 3; ++axis) {
		float o = (axis == 0 ? origin.x : (axis == 1 ? origin.y : origin.z));
		float d = (axis == 0 ? dir.x : (axis == 1 ? dir.y : dir.z));
		float minB = (axis == 0 ? box.min.x : (axis == 1 ? box.min.y : box.min.z));
		float maxB = (axis == 0 ? box.max.x : (axis == 1 ? box.max.y : box.max.z));

		if (fabs(d) < 1e-6f) {
			if (o < minB || o > maxB)
				return false; // 平行＆外
		} else {
			float t1 = (minB - o) / d;
			float t2 = (maxB - o) / d;
			float s1 = -1.0f; // min面
			float s2 = 1.0f;  // max面
			if (t1 > t2) {
				std::swap(t1, t2);
				std::swap(s1, s2);
			}

			if (t1 > tmin) {
				tmin = t1;
				hitAxis = axis;
				sign = s1;
			}
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return false;
		}
	}

	// --- ここを変更 ---
	// 内部からスタートしている場合でも、「出る面」を返すようにする
	if (tmin < 0.0f) {
		if (tmax < 0.0f)
			return false; // 完全に後方
		tmin = tmax;      // 出る側の面を採用
	}

	// --- 法線決定 ---
	outT = tmin;
	outNormal = {0, 0, 0};
	if (hitAxis == 0)
		outNormal.x = sign;
	if (hitAxis == 1)
		outNormal.y = sign;
	if (hitAxis == 2)
		outNormal.z = sign;
	outNormal = Normalize(outNormal);

	return true;
}

bool Collision::IntersectRayAABBExpanded(
    const Vector3& origin, const Vector3& dir, const AABB& box, float expand, // ← 拡張幅
    float& outT, Vector3& outNormal) {
	// ボックスを expand 分だけ拡大
	AABB expanded = box;
	expanded.min.x -= expand;
	expanded.min.y -= expand;
	expanded.min.z -= expand;
	expanded.max.x += expand;
	expanded.max.y += expand;
	expanded.max.z += expand;

	// 既存の関数で判定（拡張後AABBを使う）
	return IntersectRayAABB(origin, dir, expanded, outT, outNormal);
}




bool Collision::IntersectSegmentSphere(const Vector3& p0, const Vector3& p1, const Vector3& center, float radius, float& outT, Vector3& outNormal) {

  Vector3 d = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}; // 線分方向
	Vector3 m = {p0.x - center.x, p0.y - center.y, p0.z - center.z};

	float a = d.x * d.x + d.y * d.y + d.z * d.z;
	float b = 2.0f * (m.x * d.x + m.y * d.y + m.z * d.z);
	float c = (m.x * m.x + m.y * m.y + m.z * m.z) - radius * radius;

	 float discriminant = b * b - 4 * a * c;
	if (discriminant < 0.0f)
		return false; // 衝突なし

	float sqrtD = std::sqrtf(discriminant);
	float t1 = (-b - sqrtD) / (2.0f * a);
	float t2 = (-b + sqrtD) / (2.0f * a);

	// 線分範囲内か確認（0〜1）
	if (t1 > 1.0f || t2 < 0.0f)
		return false;

	// 衝突点の比率を選択
	outT = std::max(0.0f, t1);

	// 衝突点
	Vector3 hit = {p0.x + d.x * outT, p0.y + d.y * outT, p0.z + d.z * outT};

	// 法線
	outNormal = {hit.x - center.x, hit.y - center.y, hit.z - center.z};

	// 正規化
	float len = std::sqrtf(outNormal.x * outNormal.x + outNormal.y * outNormal.y + outNormal.z * outNormal.z);
	if (len > 1e-6f) {
		outNormal.x /= len;
		outNormal.y /= len;
		outNormal.z /= len;
	} else {
		outNormal = {0, 1, 0};
	}

	return true;


}

} // namespace Collision
} // namespace Engine
