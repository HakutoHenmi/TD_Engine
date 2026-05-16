#pragma once
// 視錐台カリング (クリップ空間判定 / mul(v, WorldViewProj) と同等)

#include "Camera.h"
#include "Matrix4x4.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

namespace Engine {

inline Vector4 TransformToClip(const Vector3& v, const Matrix4x4& m) {
	float x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
	float y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
	float z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
	float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
	return {x, y, z, w};
}

struct FrustumCullSettings {
	// 描画用プロジェクションより広いFOVでカリング (1.0 = 同等, 1.2 = 約20%広い)
	float fovScale = 1.20f;
	// クリップ空間での余白 (0.35 = 画面端から35%手前まで描画対象に含める)
	float padHorizontal = 0.40f;
	float padTop = 0.30f;
	float padBottom = 0.65f; // 画面下側のポップイン軽減
	float padNear = 0.20f;
	float padFar = 0.10f;
};

struct Frustum {
	Matrix4x4 viewProj_{};
	FrustumCullSettings settings_{};

	static Frustum FromCamera(const Camera& camera, const FrustumCullSettings& settings = {}) {
		Frustum f{};
		f.settings_ = settings;

		DirectX::XMMATRIX proj = camera.Proj();
		if (settings.fovScale > 1.0001f) {
			// 透視成分を縮小 → FOV拡大 (カリング専用、描画には影響しない)
			DirectX::XMFLOAT4X4 projMat;
			DirectX::XMStoreFloat4x4(&projMat, proj);
			const float inv = 1.0f / settings.fovScale;
			projMat._11 *= inv;
			projMat._22 *= inv;
			proj = DirectX::XMLoadFloat4x4(&projMat);
		}

		DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(camera.View(), proj);
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&f.viewProj_.m[0][0]), vp);
		return f;
	}

	// ローカルAABBの8頂点を WorldViewProj でクリップ空間へ → GPUと同じ基準で可視判定
	bool IntersectsLocalAABB(const Matrix4x4& world, const Vector3& localMin, const Vector3& localMax) const {
		Matrix4x4 worldViewProj = Matrix4x4::Multiply(world, viewProj_);

		const Vector3 corners[8] = {
			{localMin.x, localMin.y, localMin.z},
			{localMax.x, localMin.y, localMin.z},
			{localMin.x, localMax.y, localMin.z},
			{localMax.x, localMax.y, localMin.z},
			{localMin.x, localMin.y, localMax.z},
			{localMax.x, localMin.y, localMax.z},
			{localMin.x, localMax.y, localMax.z},
			{localMax.x, localMax.y, localMax.z},
		};

		const float& padH = settings_.padHorizontal;
		const float& padT = settings_.padTop;
		const float& padB = settings_.padBottom;
		const float& padN = settings_.padNear;
		const float& padF = settings_.padFar;

		bool anyInFront = false;
		bool anyInside = false;

		for (const auto& c : corners) {
			Vector4 clip = TransformToClip(c, worldViewProj);
			if (clip.w <= 0.0001f)
				continue;

			anyInFront = true;

			float limitX = clip.w * (1.0f + padH);
			float limitTop = clip.w * (1.0f + padT);
			float limitBottom = clip.w * (1.0f + padB);
			float limitFar = clip.w * (1.0f + padF);
			float limitNear = -clip.w * padN;

			if (std::fabs(clip.x) <= limitX &&
				clip.y <= limitTop &&
				clip.y >= -limitBottom &&
				clip.z >= limitNear &&
				clip.z <= limitFar) {
				anyInside = true;
			}
		}

		if (!anyInFront)
			return false;
		if (anyInside)
			return true;

		// 大きいメッシュが視錐台をまたぐ場合: ローカル中心が前方なら描画
		Vector3 center = {
			(localMin.x + localMax.x) * 0.5f,
			(localMin.y + localMax.y) * 0.5f,
			(localMin.z + localMax.z) * 0.5f
		};
		Vector4 clipCenter = TransformToClip(center, worldViewProj);
		return clipCenter.w > 0.0001f;
	}
};

} // namespace Engine
