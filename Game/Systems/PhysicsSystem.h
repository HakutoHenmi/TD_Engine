#pragma once
#include "ISystem.h"
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>

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

							DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
							DirectX::XMVECTOR pA = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pushAxis));
							float dotV = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, pA));
							if (dotV < 0) {
								if (pushAxis.y > 0.6f) { // 接地
									if (cm) {
										cm->isGrounded = true;
									}
									DirectX::XMVECTOR vN = DirectX::XMVectorScale(pA, dotV);
									vel = DirectX::XMVectorSubtract(vel, vN);
									vel = DirectX::XMVectorScale(vel, 0.95f);
								} else if (std::abs(pushAxis.y) < 0.3f) { // 壁
									vel = DirectX::XMVectorSubtract(vel, DirectX::XMVectorScale(pA, 1.2f * dotV));
								} else { // スロープ
									DirectX::XMVECTOR vN = DirectX::XMVectorScale(pA, dotV);
									vel = DirectX::XMVectorSubtract(vel, vN);
								}
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

						// --- Broad Phase: Box World AABB ---
						Engine::Vector3 boxMin_world, boxMax_world;
						auto UpdateBoxAABB = [&]() {
							boxMin_world = {FLT_MAX, FLT_MAX, FLT_MAX};
							boxMax_world = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
							for (int k = 0; k < 8; ++k) {
								Engine::Vector3 p = c1;
								p.x += ((k & 1) ? 1 : -1) * axes1[0].x * e1.x + ((k & 2) ? 1 : -1) * axes1[1].x * e1.y + ((k & 4) ? 1 : -1) * axes1[2].x * e1.z;
								p.y += ((k & 1) ? 1 : -1) * axes1[0].y * e1.x + ((k & 2) ? 1 : -1) * axes1[1].y * e1.y + ((k & 4) ? 1 : -1) * axes1[2].y * e1.z;
								p.z += ((k & 1) ? 1 : -1) * axes1[0].z * e1.x + ((k & 2) ? 1 : -1) * axes1[1].z * e1.y + ((k & 4) ? 1 : -1) * axes1[2].z * e1.z;
								boxMin_world.x = std::min(boxMin_world.x, p.x); boxMin_world.y = std::min(boxMin_world.y, p.y); boxMin_world.z = std::min(boxMin_world.z, p.z);
								boxMax_world.x = std::max(boxMax_world.x, p.x); boxMax_world.y = std::max(boxMax_world.y, p.y); boxMax_world.z = std::max(boxMax_world.z, p.z);
							}
						};
						UpdateBoxAABB();

						// --- Broad Phase: Mesh World AABB ---
						Engine::Vector3 meshMin_world = {FLT_MAX, FLT_MAX, FLT_MAX}, meshMax_world = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
						for (int k = 0; k < 8; ++k) {
							Engine::Vector3 p = {(k & 1) ? data.max.x : data.min.x, (k & 2) ? data.max.y : data.min.y, (k & 4) ? data.max.z : data.min.z};
							p = Engine::TransformCoord(p, otherWorld);
							meshMin_world.x = std::min(meshMin_world.x, p.x); meshMin_world.y = std::min(meshMin_world.y, p.y); meshMin_world.z = std::min(meshMin_world.z, p.z);
							meshMax_world.x = std::max(meshMax_world.x, p.x); meshMax_world.y = std::max(meshMax_world.y, p.y); meshMax_world.z = std::max(meshMax_world.z, p.z);
						}

						if (boxMax_world.x < meshMin_world.x || boxMin_world.x > meshMax_world.x ||
							boxMax_world.y < meshMin_world.y || boxMin_world.y > meshMax_world.y ||
							boxMax_world.z < meshMin_world.z || boxMin_world.z > meshMax_world.z) continue;

						DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&otherWorld));
						Engine::Matrix4x4 invWorld = Engine::Matrix4x4::Inverse(otherWorld);

						// --- Local Space OBB & AABB ---
						Engine::Vector3 c1_local, axes1_local[3], boxMin_local, boxMax_local;
						auto UpdateLocalBounds = [&]() {
							c1_local = Engine::TransformCoord(c1, invWorld);
							axes1_local[0] = Engine::TransformNormal(axes1[0], invWorld);
							axes1_local[1] = Engine::TransformNormal(axes1[1], invWorld);
							axes1_local[2] = Engine::TransformNormal(axes1[2], invWorld);
							boxMin_local = {FLT_MAX, FLT_MAX, FLT_MAX};
							boxMax_local = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
							for (int k = 0; k < 8; ++k) {
								Engine::Vector3 p = c1_local;
								p.x += ((k & 1) ? 1 : -1) * axes1_local[0].x * e1.x + ((k & 2) ? 1 : -1) * axes1_local[1].x * e1.y + ((k & 4) ? 1 : -1) * axes1_local[2].x * e1.z;
								p.y += ((k & 1) ? 1 : -1) * axes1_local[0].y * e1.x + ((k & 2) ? 1 : -1) * axes1_local[1].y * e1.y + ((k & 4) ? 1 : -1) * axes1_local[2].y * e1.z;
								p.z += ((k & 1) ? 1 : -1) * axes1_local[0].z * e1.x + ((k & 2) ? 1 : -1) * axes1_local[1].z * e1.y + ((k & 4) ? 1 : -1) * axes1_local[2].z * e1.z;
								boxMin_local.x = (std::min)(boxMin_local.x, p.x); boxMin_local.y = (std::min)(boxMin_local.y, p.y); boxMin_local.z = (std::min)(boxMin_local.z, p.z);
								boxMax_local.x = (std::max)(boxMax_local.x, p.x); boxMax_local.y = (std::max)(boxMax_local.y, p.y); boxMax_local.z = (std::max)(boxMax_local.z, p.z);
							}
						};

						// --- Iterative Collision Resolution ---
						const int maxIterations = 5; 
						for (int iter = 0; iter < maxIterations; ++iter) {
							bool collisionOccurred = false;
							UpdateLocalBounds();
							UpdateBoxAABB();

							struct ContactInfo {
								Engine::Vector3 normal;
								float depth;
								bool found = false;
							} bestContact;

							auto FilterDeepest = [&](DirectX::XMVECTOR v_world[3]) {
								Engine::Vector3 pAxisW; float ov;
								if (TestObbTriangle(c1, axes1, e1, v_world[0], v_world[1], v_world[2], pAxisW, ov)) {
									if (!bestContact.found || ov > bestContact.depth) {
										bestContact.normal = pAxisW;
										bestContact.depth = ov;
										bestContact.found = true;
									}
								}
							};

							if (gmc.collisionType == MeshCollisionType::Mesh) {
								if (!data.bvhNodes.empty()) {
									std::vector<uint32_t> stack;
									stack.push_back(0); 
									while (!stack.empty()) {
										uint32_t nodeIdx = stack.back(); stack.pop_back();
										const auto& node = data.bvhNodes[nodeIdx];
										if (boxMax_local.x < node.min.x || boxMin_local.x > node.max.x ||
											boxMax_local.y < node.min.y || boxMin_local.y > node.max.y ||
											boxMax_local.z < node.min.z || boxMin_local.z > node.max.z) continue;

										if (node.leftChild == -1) {
											for (uint32_t i = 0; i < node.triangleCount; ++i) {
												uint32_t triIdx = data.bvhIndices[node.firstTriangle + i];
												DirectX::XMVECTOR v_world[3] = {
													DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[triIdx * 3]].position)), worldMat),
													DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[triIdx * 3 + 1]].position)), worldMat),
													DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[triIdx * 3 + 2]].position)), worldMat)
												};
												FilterDeepest(v_world);
											}
										} else {
											stack.push_back(node.leftChild);
											stack.push_back(node.rightChild);
										}
									}
								} else {
									for (size_t i = 0; i < data.indices.size(); i += 3) {
										DirectX::XMVECTOR v_world[3] = {
											DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[i]].position)), worldMat),
											DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[i+1]].position)), worldMat),
											DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&data.vertices[data.indices[i+2]].position)), worldMat)
										};
										FilterDeepest(v_world);
									}
								}
							} else if (gmc.collisionType == MeshCollisionType::Convex) {
								float overlapAxes[3] = {
									(std::min)(boxMax_world.x, meshMax_world.x) - (std::max)(boxMin_world.x, meshMin_world.x),
									(std::min)(boxMax_world.y, meshMax_world.y) - (std::max)(boxMin_world.y, meshMin_world.y),
									(std::min)(boxMax_world.z, meshMax_world.z) - (std::max)(boxMin_world.z, meshMin_world.z)
								};
								if (overlapAxes[0] > 0 && overlapAxes[1] > 0 && overlapAxes[2] > 0) {
									int minAxis = overlapAxes[1] < overlapAxes[0] ? (overlapAxes[2] < overlapAxes[1] ? 2 : 1) : (overlapAxes[2] < overlapAxes[0] ? 2 : 0);
									bestContact.depth = overlapAxes[minAxis];
									bestContact.found = (bestContact.depth < (minAxis == 0 ? e1.x : (minAxis == 1 ? e1.y : e1.z)) * 4.0f);
									if (bestContact.found) {
										bestContact.normal = {0,0,0};
										if (minAxis == 0) bestContact.normal.x = (boxMin_world.x < meshMin_world.x) ? -1.0f : 1.0f;
										if (minAxis == 1) bestContact.normal.y = (boxMin_world.y < meshMin_world.y) ? -1.0f : 1.0f;
										if (minAxis == 2) bestContact.normal.z = (boxMin_world.z < meshMin_world.z) ? -1.0f : 1.0f;
									}
								}
							}

							if (bestContact.found) {
								// めり込み量分だけ押し戻す
								obj.translate.x += bestContact.normal.x * bestContact.depth;
								obj.translate.y += bestContact.normal.y * bestContact.depth;
								obj.translate.z += bestContact.normal.z * bestContact.depth;

								// 接地判定は速度に関わらず法線の向きで判断
								if (bestContact.normal.y > 0.6f) {
									if (cm) {
										cm->isGrounded = true;
									}
								}

								DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
								DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&bestContact.normal));
								float dotVN = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, n));
								if (dotVN < 0) {
									vel = DirectX::XMVectorSubtract(vel, DirectX::XMVectorScale(n, dotVN));
									if (bestContact.normal.y > 0.6f) { // Ground
										float speed = DirectX::XMVectorGetX(DirectX::XMVector3Length(vel));
										if (speed < 0.2f) vel = DirectX::XMVectorZero();
										else vel = DirectX::XMVectorScale(vel, 0.9f);
									}
									DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
								}
								collisionOccurred = true;
								GetObbAxes(obj, bc, axes1, c1, e1);
							}
							if (!collisionOccurred) break;
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
		
		// 1. 三角形の法線
		DirectX::XMVECTOR triNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(edges[0], edges[1]));
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(triNormal)) < 1e-6f) return false; 
		testAxes.push_back(triNormal);

		// 2. Boxの各軸
		for (int i = 0; i < 3; ++i) testAxes.push_back(boxAxes[i]);

		// 3. 辺同士の外積
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

		for (size_t i = 0; i < testAxes.size(); ++i) {
			const auto& axis = testAxes[i];
			float rBox = extents.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[0]))) +
						 extents.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[1]))) +
						 extents.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxAxes[2])));
			
			float boxProj = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, boxCenter));
			float minBox = boxProj - rBox;
			float maxBox = boxProj + rBox;

			float minTri = FLT_MAX, maxTri = -FLT_MAX;
			for (int v = 0; v < 3; ++v) {
				float p = DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, tri[v]));
				minTri = (std::min)(minTri, p);
				maxTri = (std::max)(maxTri, p);
			}

			// 分離しているかチェック (Separating Axis Theorem)
			float overlap0 = maxBox - minTri;
			float overlap1 = maxTri - minBox;
			if (overlap0 <= 0.0f || overlap1 <= 0.0f) return false;

			// 重なり量（押し戻しに必要な最小距離）を計算
			float overlap = (std::min)(overlap0, overlap1);
			
			// 平地での安定性のために法線（i=0）を優先
			float bias = (i == 0) ? 0.01f : 0.0f;
			if (overlap < minOverlap - bias) {
				minOverlap = overlap;
				bestAxis = axis;
				
				// 向きを「三角形からボックスへ向かう」方向に統一
				DirectX::XMVECTOR triCenter = DirectX::XMVectorScale(DirectX::XMVectorAdd(DirectX::XMVectorAdd(tri[0], tri[1]), tri[2]), 1.0f / 3.0f);
				if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(bestAxis, DirectX::XMVectorSubtract(boxCenter, triCenter))) < 0) {
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
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&mat));
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
