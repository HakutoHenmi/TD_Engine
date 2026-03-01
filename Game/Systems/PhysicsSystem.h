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

				// 事前にCharacterMovementを取得しておく（接地判定用）
				CharacterMovementComponent* cm = obj.characterMovements.empty() ? nullptr : &obj.characterMovements[0];
				if (cm) cm->isGrounded = false;

				for (auto& other : objects) {
					if (&obj == &other) continue;

					// --- Box vs Box ---
					if (!other.boxColliders.empty()) {
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

							if (cm && pushAxis.y > 0.5f) {
								cm->isGrounded = true;
								if (cm->velocityY < 0) cm->velocityY = 0;
							}

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

					// --- Box vs Mesh ---
					if (!other.gpuMeshColliders.empty() && ctx.renderer) {
						auto& gmc = other.gpuMeshColliders[0];
						if (!gmc.enabled || gmc.isTrigger) continue;

						auto* model = ctx.renderer->GetModel(gmc.meshHandle);
						if (!model) continue;

						const auto& data = model->GetData();
						Engine::Matrix4x4 otherWorld = other.GetTransform().ToMatrix();
						Engine::Matrix4x4 invWorld = Engine::Matrix4x4::Inverse(otherWorld);

						// Box in Local Space
						Engine::Vector3 c1_local = Engine::TransformCoord(c1, invWorld);
						Engine::Vector3 axes1_local[3];
						for (int k = 0; k < 3; ++k) {
							axes1_local[k] = Engine::Normalize(Engine::TransformNormal(axes1[k], invWorld));
						}

						// Box AABB in Local Space (Broad phase)
						Engine::Vector3 boxMin_local = {FLT_MAX, FLT_MAX, FLT_MAX};
						Engine::Vector3 boxMax_local = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
						for (int i = 0; i < 8; ++i) {
							Engine::Vector3 p = c1_local;
							p.x += ((i & 1) ? 1 : -1) * axes1_local[0].x * e1.x +
							       ((i & 2) ? 1 : -1) * axes1_local[1].x * e1.y +
							       ((i & 4) ? 1 : -1) * axes1_local[2].x * e1.z;
							p.y += ((i & 1) ? 1 : -1) * axes1_local[0].y * e1.x +
							       ((i & 2) ? 1 : -1) * axes1_local[1].y * e1.y +
							       ((i & 4) ? 1 : -1) * axes1_local[2].y * e1.z;
							p.z += ((i & 1) ? 1 : -1) * axes1_local[0].z * e1.x +
							       ((i & 2) ? 1 : -1) * axes1_local[1].z * e1.y +
							       ((i & 4) ? 1 : -1) * axes1_local[2].z * e1.z;
							boxMin_local.x = std::min(boxMin_local.x, p.x);
							boxMin_local.y = std::min(boxMin_local.y, p.y);
							boxMin_local.z = std::min(boxMin_local.z, p.z);
							boxMax_local.x = std::max(boxMax_local.x, p.x);
							boxMax_local.y = std::max(boxMax_local.y, p.y);
							boxMax_local.z = std::max(boxMax_local.z, p.z);
						}

						// Mesh-level AABB check
						if (boxMax_local.x < data.min.x || boxMin_local.x > data.max.x ||
							boxMax_local.y < data.min.y || boxMin_local.y > data.max.y ||
							boxMax_local.z < data.min.z || boxMin_local.z > data.max.z) continue;

						for (size_t i = 0; i < data.indices.size(); i += 3) {
							float p0[4] = {data.vertices[data.indices[i]].position.x, data.vertices[data.indices[i]].position.y, data.vertices[data.indices[i]].position.z, 1.0f};
							float p1[4] = {data.vertices[data.indices[i+1]].position.x, data.vertices[data.indices[i+1]].position.y, data.vertices[data.indices[i+1]].position.z, 1.0f};
							float p2[4] = {data.vertices[data.indices[i+2]].position.x, data.vertices[data.indices[i+2]].position.y, data.vertices[data.indices[i+2]].position.z, 1.0f};

							// Triangle AABB check
							float triMinX = std::min({p0[0], p1[0], p2[0]});
							float triMaxX = std::max({p0[0], p1[0], p2[0]});
							if (boxMax_local.x < triMinX || boxMin_local.x > triMaxX) continue;

							float triMinY = std::min({p0[1], p1[1], p2[1]});
							float triMaxY = std::max({p0[1], p1[1], p2[1]});
							if (boxMax_local.y < triMinY || boxMin_local.y > triMaxY) continue;

							float triMinZ = std::min({p0[2], p1[1], p2[2]}); // Typo fix: p1[2]
							triMinZ = std::min({p0[2], p1[2], p2[2]});
							float triMaxZ = std::max({p0[2], p1[2], p2[2]});
							if (boxMax_local.z < triMinZ || boxMin_local.z > triMaxZ) continue;

							DirectX::XMVECTOR v[3] = { DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)p0), DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)p1), DirectX::XMLoadFloat4((DirectX::XMFLOAT4*)p2) };

							Engine::Vector3 pushAxis_local;
							float overlap_local;
							if (TestObbTriangle(c1_local, axes1_local, e1, v[0], v[1], v[2], pushAxis_local, overlap_local)) {
								Engine::Vector3 pushAxisWorld = Engine::Normalize(Engine::TransformNormal(pushAxis_local, otherWorld));
								obj.translate.x += pushAxisWorld.x * overlap_local;
								obj.translate.y += pushAxisWorld.y * overlap_local;
								obj.translate.z += pushAxisWorld.z * overlap_local;

								if (cm && pushAxisWorld.y > 0.5f) {
									cm->isGrounded = true;
									if (cm->velocityY < 0) cm->velocityY = 0;
								}

								DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
								DirectX::XMVECTOR pA = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pushAxisWorld));
								float dotV = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, pA));
								if (dotV < 0) {
									DirectX::XMVECTOR vN = DirectX::XMVectorScale(pA, dotV);
									vel = DirectX::XMVectorSubtract(vel, vN);
									DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
								}
								GetObbAxes(obj, bc, axes1, c1, e1);
								c1_local = Engine::TransformCoord(c1, invWorld);
							}
						}
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
	static bool TestObbTriangle(const Engine::Vector3& center, const Engine::Vector3 axes[3], const Engine::Vector3& extents,
		DirectX::XMVECTOR v0, DirectX::XMVECTOR v1, DirectX::XMVECTOR v2,
		Engine::Vector3& outPushAxis, float& outOverlap) {
		
		DirectX::XMVECTOR boxCenter = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&center));
		DirectX::XMVECTOR tri[3] = { v0, v1, v2 };
		DirectX::XMVECTOR edges[3] = {
			DirectX::XMVectorSubtract(v1, v0),
			DirectX::XMVectorSubtract(v2, v1),
			DirectX::XMVectorSubtract(v0, v2)
		};

		DirectX::XMVECTOR boxAxes[3];
		for (int i = 0; i < 3; ++i) boxAxes[i] = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes[i]));

		std::vector<DirectX::XMVECTOR> testAxes;
		// 1. Box normals
		for (int i = 0; i < 3; ++i) testAxes.push_back(boxAxes[i]);
		// 2. Triangle normal
		DirectX::XMVECTOR triNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(edges[0], edges[1]));
		testAxes.push_back(triNormal);
		// 3. Edge-Edge cross products
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				DirectX::XMVECTOR axis = DirectX::XMVector3Cross(boxAxes[i], edges[j]);
				if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(axis)) > 1e-6f) {
					testAxes.push_back(DirectX::XMVector3Normalize(axis));
				}
			}
		}

		float minOverlap = FLT_MAX;
		DirectX::XMVECTOR bestAxis = DirectX::XMVectorZero();

		for (const auto& axis : testAxes) {
			// Project box
			float rBox = extents.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[0]))) +
						 extents.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[1]))) +
						 extents.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[2])));
			
			// Project triangle
			float minTri = FLT_MAX, maxTri = -FLT_MAX;
			float boxProj = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxCenter));
			for (int i = 0; i < 3; ++i) {
				float p = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, tri[i]));
				minTri = std::min(minTri, p);
				maxTri = std::max(maxTri, p);
			}

			float minBox = boxProj - rBox;
			float maxBox = boxProj + rBox;

			if (maxBox < minTri || maxTri < minBox) return false;

			float overlap = std::min(maxBox, maxTri) - std::max(minBox, minTri);
			if (overlap < minOverlap) {
				minOverlap = overlap;
				bestAxis = axis;
				// 向きを調整 (Triangle -> Box)
				if (boxProj < (minTri + maxTri) * 0.5f) {
					bestAxis = DirectX::XMVectorNegate(bestAxis);
				}
			}
		}

		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&outPushAxis), bestAxis);
		outOverlap = minOverlap;
		return true;
	}

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
