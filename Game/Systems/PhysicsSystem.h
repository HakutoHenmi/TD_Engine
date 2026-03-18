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

		// --- 事前準備: オブジェクトのフィルタリングとAABB事前計算 ---
		struct CollidableBox {
			size_t originalIdx;
			Engine::Vector3 axes[3], center, extents;
			Engine::Vector3 aabbMin, aabbMax;
		};
		struct CollidableMesh {
			size_t originalIdx;
			uint32_t meshHandle;
			Engine::Vector3 aabbMin, aabbMax;
			Engine::Matrix4x4 world;
		};

		std::vector<CollidableBox> dynamics;
		std::vector<CollidableMesh> statics;

		for (size_t i = 0; i < objects.size(); ++i) {
			auto& obj = objects[i];
			if (!obj.rigidbodies.empty() && !obj.boxColliders.empty()) {
				auto& rb = obj.rigidbodies[0];
				auto& bc = obj.boxColliders[0];
				if (rb.enabled && !rb.isKinematic && bc.enabled) {
					// 物理挙動の更新（重力・移動）
					if (rb.useGravity) rb.velocity.y -= 9.8f * ctx.dt;
					obj.translate.x += rb.velocity.x * ctx.dt;
					obj.translate.y += rb.velocity.y * ctx.dt;
					obj.translate.z += rb.velocity.z * ctx.dt;

					CollidableBox cb;
					cb.originalIdx = i;
					GetObbAxes(obj, bc, cb.axes, cb.center, cb.extents);
					
					// AABB計算
					cb.aabbMin = {FLT_MAX, FLT_MAX, FLT_MAX};
					cb.aabbMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
					for (int k = 0; k < 8; ++k) {
						Engine::Vector3 p = cb.center;
						p.x += ((k & 1) ? 1 : -1) * cb.axes[0].x * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].x * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].x * cb.extents.z;
						p.y += ((k & 1) ? 1 : -1) * cb.axes[0].y * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].y * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].y * cb.extents.z;
						p.z += ((k & 1) ? 1 : -1) * cb.axes[0].z * cb.extents.x + ((k & 2) ? 1 : -1) * cb.axes[1].z * cb.extents.y + ((k & 4) ? 1 : -1) * cb.axes[2].z * cb.extents.z;
						cb.aabbMin.x = std::min(cb.aabbMin.x, p.x); cb.aabbMin.y = std::min(cb.aabbMin.y, p.y); cb.aabbMin.z = std::min(cb.aabbMin.z, p.z);
						cb.aabbMax.x = std::max(cb.aabbMax.x, p.x); cb.aabbMax.y = std::max(cb.aabbMax.y, p.y); cb.aabbMax.z = std::max(cb.aabbMax.z, p.z);
					}
					dynamics.push_back(cb);
					
					// 接地フラグリセット
					if (!obj.characterMovements.empty()) obj.characterMovements[0].isGrounded = false;
				}
			}
			if (!obj.gpuMeshColliders.empty()) {
				auto& gmc = obj.gpuMeshColliders[0];
				if (gmc.enabled && ctx.renderer) {
					auto* model = ctx.renderer->GetModel(gmc.meshHandle);
					if (model) {
						CollidableMesh cm;
						cm.originalIdx = i;
						cm.meshHandle = gmc.meshHandle;
						cm.world = obj.GetTransform().ToMatrix();
						
						const auto& data = model->GetData();
						cm.aabbMin = {FLT_MAX, FLT_MAX, FLT_MAX};
						cm.aabbMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
						for (int k = 0; k < 8; ++k) {
							Engine::Vector3 p = {(k & 1) ? data.max.x : data.min.x, (k & 2) ? data.max.y : data.min.y, (k & 4) ? data.max.z : data.min.z};
							p = Engine::TransformCoord(p, cm.world);
							cm.aabbMin.x = std::min(cm.aabbMin.x, p.x); cm.aabbMin.y = std::min(cm.aabbMin.y, p.y); cm.aabbMin.z = std::min(cm.aabbMin.z, p.z);
							cm.aabbMax.x = std::max(cm.aabbMax.x, p.x); cm.aabbMax.y = std::max(cm.aabbMax.y, p.y); cm.aabbMax.z = std::max(cm.aabbMax.z, p.z);
						}
						statics.push_back(cm);
					}
				}
			}
		}

		// --- Pass 1: Box-Box Collisions (CPU) ---
		for (size_t i = 0; i < dynamics.size(); ++i) {
			auto& d1 = dynamics[i];
			
			for (size_t j = i + 1; j < dynamics.size(); ++j) {
				auto& d2 = dynamics[j];
				// Sphere prune
				float sphereR1 = std::sqrt(d1.extents.x * d1.extents.x + d1.extents.y * d1.extents.y + d1.extents.z * d1.extents.z);
				float sphereR2 = std::sqrt(d2.extents.x * d2.extents.x + d2.extents.y * d2.extents.y + d2.extents.z * d2.extents.z);
				float dx = d1.center.x - d2.center.x, dy = d1.center.y - d2.center.y, dz = d1.center.z - d2.center.z;
				if (dx*dx + dy*dy + dz*dz > (sphereR1 + sphereR2) * (sphereR1 + sphereR2)) continue;

				// OBB SAT (簡略化のため詳細は省略するが、実際にはここで判定を行う)
				// ...
			}
		}

		// --- Pass 2: GPU Mesh Collision Batched Requests ---
		struct GpuRequest {
			size_t objIdx;
			uint32_t resultIdx;
		};
		std::vector<GpuRequest> pendingGpuRequests;
		uint32_t nextResultIdx = 0;

		if (ctx.renderer && !dynamics.empty() && !statics.empty()) {
			ctx.renderer->BeginCollisionCheck(2048); // 余裕を持って拡張

			for (auto& d : dynamics) {
				auto& obj = objects[d.originalIdx];
				for (auto& s : statics) {
					// Broad phase AABB check
					if (d.aabbMax.x < s.aabbMin.x || d.aabbMin.x > s.aabbMax.x ||
						d.aabbMax.y < s.aabbMin.y || d.aabbMin.y > s.aabbMax.y ||
						d.aabbMax.z < s.aabbMin.z || d.aabbMin.z > s.aabbMax.z) continue;

					if (nextResultIdx >= 2048) break; // 上限突破防止

					uint32_t rIdx = nextResultIdx++;
					ctx.renderer->DispatchCollision(0, s.meshHandle, obj.GetTransform(), obj.boxColliders[0], objects[s.originalIdx].GetTransform(), rIdx);
					pendingGpuRequests.push_back({d.originalIdx, rIdx});
				}
				if (nextResultIdx >= 2048) break;
			}

			// Batch Execute
			ctx.renderer->EndCollisionCheck();

			// --- Pass 3: Resolve GPU Results ---
			for (const auto& req : pendingGpuRequests) {
				ContactInfo ci{};
				if (ctx.renderer->GetCollisionResult(req.resultIdx, ci)) {
					auto& obj = objects[req.objIdx];
					obj.translate.x += ci.normal.x * ci.depth;
					obj.translate.y += ci.normal.y * ci.depth;
					obj.translate.z += ci.normal.z * ci.depth;

					if (ci.normal.y > 0.6f && !obj.characterMovements.empty()) obj.characterMovements[0].isGrounded = true;

					if (!obj.rigidbodies.empty()) {
						auto& rb = obj.rigidbodies[0];
						DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
						DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&ci.normal));
						float dotVN = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, n));
						if (dotVN < 0) {
							vel = DirectX::XMVectorSubtract(vel, DirectX::XMVectorScale(n, dotVN));
							if (ci.normal.y > 0.6f) {
								if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vel)) < 0.04f) vel = DirectX::XMVectorZero();
								else vel = DirectX::XMVectorScale(vel, 0.9f);
							}
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
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
