#pragma once
#include "ISystem.h"
#include <cmath>
#include <cfloat>
#include <vector>
#include <algorithm>
#include "../Engine/QuadTree.h"

namespace Game {

class PhysicsSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// --- 事前準備: エンティティのフィルタリングとAABB事前計算 ---
		struct CollidableBox {
			entt::entity entity;
			::Engine::Vector3 axes[3], center, extents;
			::Engine::Vector3 aabbMin, aabbMax;
		};
		struct CollidableMesh {
			entt::entity entity;
			uint32_t meshHandle;
			::Engine::Vector3 aabbMin, aabbMax;
			::Engine::Matrix4x4 world;
		};

		std::vector<CollidableBox> dynamics;
		std::vector<CollidableMesh> statics;

		// 動的オブジェクト (Rigidbody + BoxCollider + Transform)
		auto dynamicView = registry.view<RigidbodyComponent, BoxColliderComponent, TransformComponent>();
		for (auto entity : dynamicView) {
			auto& rb = dynamicView.get<RigidbodyComponent>(entity);
			auto& bc = dynamicView.get<BoxColliderComponent>(entity);
			auto& tc = dynamicView.get<TransformComponent>(entity);
			if (!rb.enabled || !bc.enabled) continue; // isKinematic でもリストには入れる

			// 物理挙動の更新（重力・移動・減衰）
			float damping = 3.0f; // 毎秒の減衰係数
			bool isGrounded = false;
			bool hasCMS = false;
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				isGrounded = cm->isGrounded;
				hasCMS = true;
			}

			if (rb.useGravity && !isGrounded && !hasCMS) { // CMSがある場合はCMS側で重力を制御する
				float gravity = 9.8f;
				rb.velocity.y -= gravity * ctx.dt;
			}
			if (!rb.isKinematic) {
				// 水平方向の減衰（地上や空中での自然停止）
				rb.velocity.x -= rb.velocity.x * damping * ctx.dt;
				rb.velocity.z -= rb.velocity.z * damping * ctx.dt;

				tc.translate.x += rb.velocity.x * ctx.dt;
				tc.translate.y += rb.velocity.y * ctx.dt;
				tc.translate.z += rb.velocity.z * ctx.dt;
			}

			CollidableBox cb;
			cb.entity = entity;
			::Engine::Matrix4x4 world = ctx.scene ? ctx.scene->GetWorldMatrix(static_cast<int>(entity)) : tc.ToMatrix();
			GetObbAxes(world, bc, cb.axes, cb.center, cb.extents);
			
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
			
			/* 
			// 接地フラグリセット (CharacterMovementSystem側で管理するためPhysicsSystemでの一律リセットは廃止)
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				cm->isGrounded = false;
			}
			*/
		}

		// 静的オブジェクト (GpuMeshCollider + Transform)
		auto staticView = registry.view<GpuMeshColliderComponent, TransformComponent>();
		for (auto entity : staticView) {
			auto& gmc = staticView.get<GpuMeshColliderComponent>(entity);
			auto& tc = staticView.get<TransformComponent>(entity);
			if (!gmc.enabled || !ctx.renderer) continue;

			auto* model = ctx.renderer->GetModel(gmc.meshHandle);
			if (!model) continue;

			CollidableMesh cm;
			cm.entity = entity;
			cm.meshHandle = gmc.meshHandle;
			
			// TransformComponentからワールドマトリクスを構築
			::Engine::Transform engineTc;
			engineTc.translate = {tc.translate.x, tc.translate.y, tc.translate.z};
			engineTc.rotate = {tc.rotate.x, tc.rotate.y, tc.rotate.z};
			engineTc.scale = {tc.scale.x, tc.scale.y, tc.scale.z};
			cm.world = engineTc.ToMatrix();
			
			const auto& data = model->GetData();
			cm.aabbMin = {FLT_MAX, FLT_MAX, FLT_MAX};
			cm.aabbMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
			for (int k = 0; k < 8; ++k) {
				::Engine::Vector3 p = {(k & 1) ? data.max.x : data.min.x, (k & 2) ? data.max.y : data.min.y, (k & 4) ? data.max.z : data.min.z};
				p = ::Engine::TransformCoord(p, cm.world);
				cm.aabbMin.x = std::min(cm.aabbMin.x, p.x); cm.aabbMin.y = std::min(cm.aabbMin.y, p.y); cm.aabbMin.z = std::min(cm.aabbMin.z, p.z);
				cm.aabbMax.x = std::max(cm.aabbMax.x, p.x); cm.aabbMax.y = std::max(cm.aabbMax.y, p.y); cm.aabbMax.z = std::max(cm.aabbMax.z, p.z);
			}
			statics.push_back(cm);
		}

		// --- QuadTreeの構築 ---
		::Engine::PhysicsQuadTree dynamicQT(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);
		::Engine::PhysicsQuadTree staticQT(-4000.0f, -4000.0f, 4000.0f, 4000.0f, 6, 10);

		for (size_t i = 0; i < dynamics.size(); ++i) {
			dynamicQT.Insert((uint32_t)i, dynamics[i].aabbMin.x, dynamics[i].aabbMin.z, dynamics[i].aabbMax.x, dynamics[i].aabbMax.z);
		}
		for (size_t i = 0; i < statics.size(); ++i) {
			staticQT.Insert((uint32_t)i, statics[i].aabbMin.x, statics[i].aabbMin.z, statics[i].aabbMax.x, statics[i].aabbMax.z);
		}

		// --- Pass 1: Box-Box Collisions (CPU / OBB SAT) ---
		// 反復処理をさらに強化 (8回) して密集・埋まりを完全に解決
		for (int iteration = 0; iteration < 8; ++iteration) {
			for (size_t i = 0; i < dynamics.size(); ++i) {
				auto& d1 = dynamics[i];
				std::vector<uint32_t> nearbyDynamics;
				dynamicQT.Query(d1.aabbMin.x, d1.aabbMin.z, d1.aabbMax.x, d1.aabbMax.z, nearbyDynamics);

				for (uint32_t j : nearbyDynamics) {
					if (j <= i) continue;
					auto& d2 = dynamics[j];

					// 球体による粗い判定で高速化
					float sphereR1 = std::sqrt(d1.extents.x * d1.extents.x + d1.extents.y * d1.extents.y + d1.extents.z * d1.extents.z);
					float sphereR2 = std::sqrt(d2.extents.x * d2.extents.x + d2.extents.y * d2.extents.y + d2.extents.z * d2.extents.z);
					float dx = d1.center.x - d2.center.x, dy = d1.center.y - d2.center.y, dz = d1.center.z - d2.center.z;
					if (dx * dx + dy * dy + dz * dz > (sphereR1 + sphereR2) * (sphereR1 + sphereR2)) continue;

					// キャラクター（Player/Enemy）判定
					bool isC1 = false, isC2 = false;
					if (auto* tag1 = registry.try_get<TagComponent>(d1.entity)) isC1 = (tag1->tag == "Player" || tag1->tag == "Enemy");
					if (auto* tag2 = registry.try_get<TagComponent>(d2.entity)) isC2 = (tag2->tag == "Player" || tag2->tag == "Enemy");

					// OBB SAT による詳細判定
					::Engine::Vector3 axes[15];
					for (int k = 0; k < 3; ++k) axes[k] = d1.axes[k];
					for (int k = 0; k < 3; ++k) axes[k + 3] = d2.axes[k];
					for (int k = 0; k < 3; ++k) {
						for (int l = 0; l < 3; ++l) {
							DirectX::XMVECTOR a = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d1.axes[k]));
							DirectX::XMVECTOR b = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d2.axes[l]));
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[6 + k * 3 + l]), DirectX::XMVector3Cross(a, b));
						}
					}

					float minOverlap = FLT_MAX;
					::Engine::Vector3 mtv = {0, 0, 0};
					bool collision = true;

					DirectX::XMVECTOR relPos = DirectX::XMVectorSet(dx, dy, dz, 0.0f);

					for (int k = 0; k < 15; ++k) {
						DirectX::XMVECTOR L = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes[k]));
						float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(L));
						if (lenSq < 0.001f) continue;
						L = DirectX::XMVectorScale(L, 1.0f / std::sqrt(lenSq));

						float rA = 0, rB = 0;
						for (int m = 0; m < 3; ++m) {
							float extA = (m == 0) ? d1.extents.x : (m == 1) ? d1.extents.y : d1.extents.z;
							float extB = (m == 0) ? d2.extents.x : (m == 1) ? d2.extents.y : d2.extents.z;
							rA += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d1.axes[m])), L))) * extA;
							rB += std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&d2.axes[m])), L))) * extB;
						}

						float distance = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)));
						float overlap = rA + rB - distance;

						if (overlap <= 0.0f) {
							collision = false;
							break;
						}
						
						// キャラクター間衝突の場合、垂直方向(Y)の重なりは事実上無視してXZ平面で解決する（のぼり防止）
						float weight = 1.0f;
						if (isC1 && isC2) {
							float ly = std::abs(DirectX::XMVectorGetY(L));
							if (ly > 0.5f) weight = 10.0f; // Y成分が強い軸を避ける
						}

						if (overlap / weight < minOverlap) {
							minOverlap = overlap;
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&mtv), L);
							if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(relPos, L)) < 0) {
								mtv.x = -mtv.x; mtv.y = -mtv.y; mtv.z = -mtv.z;
							}
						}
					}

					if (collision) {
						auto& rb1 = registry.get<RigidbodyComponent>(d1.entity);
						auto& rb2 = registry.get<RigidbodyComponent>(d2.entity);
						
						float move1 = 0.5f;
						float move2 = 0.5f;
						if (rb1.isKinematic && rb2.isKinematic) { move1 = 0; move2 = 0; }
						else if (rb1.isKinematic) { move1 = 0; move2 = 1.0f; }
						else if (rb2.isKinematic) { move1 = 1.0f; move2 = 0; }

						// キャラクター同士の特殊処理: Y軸をゼロにしてXZのみで押し出す
						if (isC1 && isC2) {
							mtv.y = 0; // 垂直移動を完全にカット
							float xzLen = std::sqrt(mtv.x * mtv.x + mtv.z * mtv.z);
							if (xzLen > 0.001f) {
								mtv.x /= xzLen; mtv.z /= xzLen;
							}
							
							// 敵の頭上にいる場合の接地判定
							// (※CMS側で管理するため、ここでの接地フラグ操作はコメントアウト検討)
							/*
							if (dy > 0.5f) { // d1 が上
								if (auto* cm1 = registry.try_get<CharacterMovementComponent>(d1.entity)) cm1->isGrounded = true;
							}
							else if (dy < -0.5f) { // d2 が上
								if (auto* cm2 = registry.try_get<CharacterMovementComponent>(d2.entity)) cm2->isGrounded = true;
							}
							*/
						}

						const float correctionFactor = 1.0f; 
						if (move1 > 0) {
							auto& tc1 = registry.get<TransformComponent>(d1.entity);
							tc1.translate.x += mtv.x * minOverlap * move1 * correctionFactor;
							tc1.translate.y += mtv.y * minOverlap * move1 * correctionFactor;
							tc1.translate.z += mtv.z * minOverlap * move1 * correctionFactor;
							d1.center.x += mtv.x * minOverlap * move1 * correctionFactor;
							d1.center.y += mtv.y * minOverlap * move1 * correctionFactor;
							d1.center.z += mtv.z * minOverlap * move1 * correctionFactor;
						}
						if (move2 > 0) {
							auto& tc2 = registry.get<TransformComponent>(d2.entity);
							tc2.translate.x -= mtv.x * minOverlap * move2 * correctionFactor;
							tc2.translate.y -= mtv.y * minOverlap * move2 * correctionFactor;
							tc2.translate.z -= mtv.z * minOverlap * move2 * correctionFactor;
							d2.center.x -= mtv.x * minOverlap * move2 * correctionFactor;
							d2.center.y -= mtv.y * minOverlap * move2 * correctionFactor;
							d2.center.z -= mtv.z * minOverlap * move2 * correctionFactor;
						}

						DirectX::XMVECTOR v1 = rb1.isKinematic ? DirectX::XMVectorZero() : DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb1.velocity));
						DirectX::XMVECTOR v2 = rb2.isKinematic ? DirectX::XMVectorZero() : DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb2.velocity));
						DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&mtv));
						float relVel = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(v1, v2), n));
						if (relVel < 0) {
							DirectX::XMVECTOR impulse = DirectX::XMVectorScale(n, -relVel * 0.5f);
							if (!rb1.isKinematic) {
								DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb1.velocity), DirectX::XMVectorAdd(v1, impulse));
							}
							if (!rb2.isKinematic) {
								DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb2.velocity), DirectX::XMVectorSubtract(v2, impulse));
							}
						}
					}
				}
			}
		} // <- 反復ループの終端

		// --- Pass 2: GPU Mesh Collision Batched Requests ---
		struct GpuRequest {
			entt::entity entity;
			uint32_t resultIdx;
		};
		std::vector<GpuRequest> pendingGpuRequests;
		uint32_t nextResultIdx = 0;

		if (ctx.renderer && !dynamics.empty() && !statics.empty()) {
			ctx.renderer->BeginCollisionCheck(2048);

			for (auto& d : dynamics) {
				auto& tc = registry.get<TransformComponent>(d.entity);
				auto& bc = registry.get<BoxColliderComponent>(d.entity);
				
				::Engine::Transform dynTransform;
				dynTransform.translate = {tc.translate.x, tc.translate.y, tc.translate.z};
				dynTransform.rotate = {tc.rotate.x, tc.rotate.y, tc.rotate.z};
				dynTransform.scale = {tc.scale.x, tc.scale.y, tc.scale.z};
				
				std::vector<uint32_t> nearbyStatics;
				staticQT.Query(d.aabbMin.x, d.aabbMin.z, d.aabbMax.x, d.aabbMax.z, nearbyStatics);

				for (uint32_t sIdx : nearbyStatics) {
					auto& s = statics[sIdx];
					if (d.aabbMax.y < s.aabbMin.y || d.aabbMin.y > s.aabbMax.y ||
					    d.aabbMax.x < s.aabbMin.x || d.aabbMin.x > s.aabbMax.x ||
					    d.aabbMax.z < s.aabbMin.z || d.aabbMin.z > s.aabbMax.z) continue;

					if (nextResultIdx >= 2048) break;

					auto& sTc = registry.get<TransformComponent>(s.entity);
					::Engine::Transform staticTransform;
					staticTransform.translate = {sTc.translate.x, sTc.translate.y, sTc.translate.z};
					staticTransform.rotate = {sTc.rotate.x, sTc.rotate.y, sTc.rotate.z};
					staticTransform.scale = {sTc.scale.x, sTc.scale.y, sTc.scale.z};

					uint32_t rIdx = nextResultIdx++;
					ctx.renderer->DispatchCollision(0, s.meshHandle, dynTransform, bc, staticTransform, rIdx);
					pendingGpuRequests.push_back({d.entity, rIdx});
				}
				if (nextResultIdx >= 2048) break;
			}

			// Batch Execute
			ctx.renderer->EndCollisionCheck();

			// --- Pass 3: Resolve GPU Results ---
			for (const auto& req : pendingGpuRequests) {
				ContactInfo ci{};
				if (ctx.renderer->GetCollisionResult(req.resultIdx, ci)) {
					auto& tc = registry.get<TransformComponent>(req.entity);
					if (registry.all_of<RigidbodyComponent>(req.entity)) {
						auto& rb = registry.get<RigidbodyComponent>(req.entity);
						if (ci.depth > 0.0f) {
							// Kinematic オブジェクトは PhysicsSystem では移動・解決を行わず、スクリプト(CMS等)に完全に委ねる
							if (!rb.isKinematic) {
								tc.translate.x += ci.normal.x * ci.depth;
								tc.translate.y += ci.normal.y * ci.depth;
								tc.translate.z += ci.normal.z * ci.depth;
							}
						}
						DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
						DirectX::XMVECTOR n = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&ci.normal));
						float dotVN = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, n));
						if (dotVN < 0 && !rb.isKinematic) {
							// 法線方向の速度成分を打ち消す（物理的な押し出し）
							vel = DirectX::XMVectorSubtract(vel, DirectX::XMVectorScale(n, dotVN));
							
							// ★修正: 接地判定はCMS側で行うため、ここでは行わない
							/*
							if (ci.normal.y > 0.8f) {
								if (ci.position.y < tc.translate.y) {
									if (auto* cm = registry.try_get<CharacterMovementComponent>(req.entity)) {
										cm->isGrounded = true;
									}
								}
								
								// 地面との衝突時は微小な速度をゼロにする（スリープ処理）
								if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(vel)) < 0.01f) vel = DirectX::XMVectorZero();
							}
							*/
							DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
						}
					}

				}
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<RigidbodyComponent>();
		for (auto entity : view) {
			auto& rb = registry.get<RigidbodyComponent>(entity);
			rb.velocity = {0, 0, 0};
		}
	}

private:
	static void GetObbAxes(const ::Engine::Matrix4x4& mat, const BoxColliderComponent& cb,
		::Engine::Vector3 axes[3], ::Engine::Vector3& center, ::Engine::Vector3& extents) {
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

		DirectX::XMVECTOR aScale, aRot, aTrans;
		DirectX::XMMatrixDecompose(&aScale, &aRot, &aTrans, worldMat);

		extents.x = cb.size.x * 0.5f * std::abs(DirectX::XMVectorGetX(aScale));
		extents.y = cb.size.y * 0.5f * std::abs(DirectX::XMVectorGetY(aScale));
		extents.z = cb.size.z * 0.5f * std::abs(DirectX::XMVectorGetZ(aScale));
	}
};

} // namespace Game
