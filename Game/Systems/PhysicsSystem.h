#pragma once
#include "ISystem.h"
#include <cmath>
#include <cfloat>

namespace Game {

class PhysicsSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		for (auto& obj : objects) {
			for (auto& rb : obj.rigidbodies) {
				if (!rb.enabled || rb.isKinematic) continue;

				if (rb.useGravity) {
					rb.velocity.y -= 9.8f * ctx.dt;
				}

				obj.translate.x += rb.velocity.x * ctx.dt;
				obj.translate.y += rb.velocity.y * ctx.dt;
				obj.translate.z += rb.velocity.z * ctx.dt;

				if (obj.boxColliders.empty()) continue;
				auto& bc = obj.boxColliders[0];
				if (!bc.enabled) continue;

				Engine::Vector3 axes1[3], c1, e1;
				GetObbAxes(obj, bc, axes1, c1, e1);

				for (const auto& other : objects) {
					if (&obj == &other) continue;
					if (other.boxColliders.empty()) continue;
					const auto& obc = other.boxColliders[0];
					if (!obc.enabled || obc.isTrigger) continue;

					Engine::Vector3 axes2[3], c2, e2;
					GetObbAxes(other, obc, axes2, c2, e2);

					DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&c2)),
						DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&c1)));

					float minOverlap = FLT_MAX;
					Engine::Vector3 pushAxis = {0, 0, 0};
					bool intersected = true;

					std::vector<DirectX::XMVECTOR> testAxes;
					for (int i = 0; i < 3; ++i)
						testAxes.push_back(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[i])));
					for (int i = 0; i < 3; ++i)
						testAxes.push_back(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[i])));
					for (int i = 0; i < 3; ++i) {
						for (int j = 0; j < 3; ++j) {
							DirectX::XMVECTOR cross = DirectX::XMVector3Cross(
								DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[i])),
								DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[j])));
							if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(cross)) > 1e-6f) {
								testAxes.push_back(DirectX::XMVector3Normalize(cross));
							}
						}
					}

					for (const auto& axis : testAxes) {
						float r1 = e1.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[0]))))) +
						           e1.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[1]))))) +
						           e1.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[2])))));
						float r2 = e2.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[0]))))) +
						           e2.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[1]))))) +
						           e2.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[2])))));

						float distance = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(diff, axis)));
						float overlap = r1 + r2 - distance;

						if (overlap <= 0.0f) { intersected = false; break; }

						if (overlap < minOverlap) {
							minOverlap = overlap;
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&pushAxis), axis);
							if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(diff, axis)) > 0) {
								pushAxis.x *= -1; pushAxis.y *= -1; pushAxis.z *= -1;
							}
						}
					}

					if (intersected) {
						obj.translate.x += pushAxis.x * minOverlap;
						obj.translate.y += pushAxis.y * minOverlap;
						obj.translate.z += pushAxis.z * minOverlap;

						DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
						DirectX::XMVECTOR pA = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pushAxis));
						float dotV = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, pA));
						if (dotV < 0) {
							DirectX::XMVECTOR vN = DirectX::XMVectorScale(pA, dotV);
							vel = DirectX::XMVectorSubtract(vel, vN);
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
						}
						GetObbAxes(obj, bc, axes1, c1, e1);
					}
				}
			}
		}
	}

	void Reset(std::vector<SceneObject>& objects) override {
		for (auto& obj : objects) {
			for (auto& rb : obj.rigidbodies) {
				rb.velocity = {0, 0, 0};
			}
		}
	}

private:
	static void GetObbAxes(const SceneObject& o, const BoxColliderComponent& cb,
		Engine::Vector3 axes[3], Engine::Vector3& center, Engine::Vector3& extents) {
		Engine::Matrix4x4 mat = o.GetTransform().ToMatrix();
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
		DirectX::XMVECTOR c = DirectX::XMVector3TransformCoord(
			DirectX::XMVectorSet(cb.center.x, cb.center.y, cb.center.z, 1.0f), worldMat);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&center), c);

		DirectX::XMVECTOR axisX = DirectX::XMVector3Normalize(worldMat.r[0]);
		DirectX::XMVECTOR axisY = DirectX::XMVector3Normalize(worldMat.r[1]);
		DirectX::XMVECTOR axisZ = DirectX::XMVector3Normalize(worldMat.r[2]);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[0]), axisX);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[1]), axisY);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[2]), axisZ);

		extents.x = cb.size.x * 0.5f * std::abs(o.scale.x);
		extents.y = cb.size.y * 0.5f * std::abs(o.scale.y);
		extents.z = cb.size.z * 0.5f * std::abs(o.scale.z);
	}
};

} // namespace Game
