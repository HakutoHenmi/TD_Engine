#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

class CharacterMovementSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		// ★バウンダリ: 初回のみGroundメッシュからマップ境界を算出してキャッシュ
		if (!boundsCached_) {
			CacheBounds(registry, ctx);
		}
		if (!ctx.isPlaying) return;

		auto view = registry.view<CharacterMovementComponent, RigidbodyComponent, TransformComponent>();
		for (auto entity : view) {
			auto& cm = view.get<CharacterMovementComponent>(entity);
			auto& rb = view.get<RigidbodyComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!cm.enabled || !rb.enabled) continue;

			DirectX::XMFLOAT2 inputDir = {0, 0};
			bool wantJump = false;
			if (registry.all_of<PlayerInputComponent>(entity)) {
				auto& pi = registry.get<PlayerInputComponent>(entity);
				if (pi.enabled) {
					inputDir = pi.moveDir;
					wantJump = pi.jumpRequested;
				}
			}

			// --- カメラ基準の移動計算 ---
			// --- 3rd Person Character Controller 方式の移動更新 ---
			float moveX = 0, moveZ = 0;
			if (ctx.camera) {
				// 入力の正規化 (斜め移動の加速防止)
				float mag = std::sqrt(inputDir.x * inputDir.x + inputDir.y * inputDir.y);
				DirectX::XMFLOAT2 normInput = inputDir;
				if (mag > 1.0f) {
					normInput.x /= mag;
					normInput.y /= mag;
				}

				auto camRot = ctx.camera->Rotation();
				float cy = std::cos(camRot.y);
				float sy = std::sin(camRot.y);
				moveX = normInput.x * cy + normInput.y * sy;
				moveZ = -normInput.x * sy + normInput.y * cy;
			}
			
			// 1. 壁判定と水平移動
			// ★追加: ダッシュ判定 (PlayerInputComponentのsprintRequestedを反映)
			cm.isSprinting = false;
			if (registry.all_of<PlayerInputComponent>(entity)) {
				auto& pi = registry.get<PlayerInputComponent>(entity);
				if (pi.sprintRequested && (std::abs(moveX) > 0.001f || std::abs(moveZ) > 0.001f)) {
					cm.isSprinting = true;
				}
			}
			float currentSpeed = cm.speed * (cm.isSprinting ? cm.sprintMultiplier : 1.0f);
			float desiredX = moveX * currentSpeed * ctx.dt;
			float desiredZ = moveZ * currentSpeed * ctx.dt;

			if (ctx.scene && (std::abs(moveX) > 0.001f || std::abs(moveZ) > 0.001f)) {
				// --- 強力な段差制限による壁判定 ---
				float futureX = tc.translate.x + desiredX;
				float futureZ = tc.translate.z + desiredZ;

				// ★バウンダリ: キャッシュされた境界外への移動を即ブロック
				if (boundsCached_) {
					float margin = 1.0f; // 端から1mの余裕
					if (futureX < boundsMinX_ + margin || futureX > boundsMaxX_ - margin ||
					    futureZ < boundsMinZ_ + margin || futureZ > boundsMaxZ_ - margin) {
						desiredX = 0;
						desiredZ = 0;
					}
				}

				if (rb.useGravity) {
					float currentFeetY = tc.translate.y - cm.heightOffset;
					// 移動先の地面高さを先読み (startY は現在地 y。自己判定回避のため中心から発射)
					float futureGround = ctx.scene->GetHeightAt(futureX, futureZ, tc.translate.y, static_cast<uint32_t>(entity));

					// ★変更: 地面が見つからない場合(-10000.0f以下)も移動をブロック (マップ外防止)
					if (futureGround <= -5000.0f) {
						desiredX = 0;
						desiredZ = 0;
					} else if (futureGround > currentFeetY + 0.4f) {
						// 移動先が 0.4m 以上高いなら壁とみなして移動をブロック
						desiredX = 0;
						desiredZ = 0;
					} else {
						// 膝くらいの高さから進行方向にレイを飛ばす (通常の壁判定も併用)
						Engine::Vector3 rayOrig = {tc.translate.x, tc.translate.y + 0.5f, tc.translate.z}; 
						Engine::Vector3 rayDir = {moveX, 0, moveZ};
						float hitDist = 0;
						if (ctx.scene->RayCast(rayOrig, rayDir, 0.6f, static_cast<uint32_t>(entity), hitDist)) {
							desiredX = 0;
							desiredZ = 0;
						}
					}
				}
			}
			tc.translate.x += desiredX;
			tc.translate.z += desiredZ;

			// 2. ジャンプ判定と重力
			if (wantJump && cm.isGrounded) {
				if (rb.useGravity) {
					rb.velocity.y = cm.jumpPower;
					cm.isGrounded = false;
				} else {
					rb.velocity.y = 0.0f; // 重力無効時はジャンプさせない
				}
			} else {
				// 自由落下
				if (rb.useGravity) {
					rb.velocity.y -= cm.gravity * ctx.dt;
				} else {
					rb.velocity.y = 0.0f;
				}
			}
			tc.translate.y += rb.velocity.y * ctx.dt;

			// 3. 接地判定とスナップ (レイキャストを使用)
			if (ctx.scene && rb.useGravity) {
				// 自身の位置から真下の地面高さを取得 (excludeIdに自分を指定)
				// 発射位置を y (中心) にすることで、自分の上半身や剣への誤判定を物理的に防ぐ
				float groundHeight = ctx.scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y, static_cast<uint32_t>(entity));
				
				// 接地判定ロジックの刷新: 
				// 1. 上昇中 (rb.velocity.y > 0.01) は絶対に接地させない (多段ジャンプ防止)
				// 2. 落下または静止中 (rb.velocity.y <= 0.01) かつ 地面を突き抜けた(埋まった)場合のみ着地・固定
				// 3. 地面から離れている場合は grounded を解除 (空中浮遊防止)
				
				float feetY = tc.translate.y - cm.heightOffset;
				if (rb.velocity.y <= 0.01f) {
					// 地面を通過したか、ほぼ地表にある場合のみ snap
					if (feetY <= groundHeight + 0.01f && groundHeight > -5000.0f) {
						tc.translate.y = groundHeight + cm.heightOffset;
						rb.velocity.y = 0.0f;
						cm.isGrounded = true;
					} else {
						cm.isGrounded = false;
					}
				} else {
					// 打ち上げ中
					cm.isGrounded = false;
				}
			} else if (!rb.useGravity) {
				// 重力無効の場合は常に空中にいる扱いにする
				cm.isGrounded = false;
			}

			// --- 4. 回転 (スムーズな補間) ---
			if (std::abs(moveX) > 0.01f || std::abs(moveZ) > 0.01f) {
				float targetRotation = std::atan2(moveX, moveZ);
				float currentRotation = tc.rotate.y;
				
				// 最短角補間
				float diff = targetRotation - currentRotation;
				while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
				while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
				
				float rotationSpeed = 20.0f; 
				tc.rotate.y += diff * std::min(1.0f, rotationSpeed * ctx.dt);
			}


		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<CharacterMovementComponent>();
		for (auto entity : view) {
			auto& cm = registry.get<CharacterMovementComponent>(entity);
			cm.isGrounded = false;
		}
		boundsCached_ = false; // ★リセット時にキャッシュを無効化
	}

private:
	// ★バウンダリ: マップ境界キャッシュ
	bool boundsCached_ = false;
	float boundsMinX_ = -350.0f;
	float boundsMaxX_ =  350.0f;
	float boundsMinZ_ = -350.0f;
	float boundsMaxZ_ =  350.0f;

	// ★Groundオブジェクトのメッシュ頂点からAABBを算出してキャッシュ
	void CacheBounds(entt::registry& registry, GameContext& ctx) {
		auto view = registry.view<NameComponent, TransformComponent>();
		for (auto entity : view) {
			auto& nc = registry.get<NameComponent>(entity);
			if (nc.name != "Ground") continue;

			auto& tc = registry.get<TransformComponent>(entity);

			// メッシュからAABBを取得
			uint32_t modelHandle = 0;
			if (registry.all_of<MeshRendererComponent>(entity)) {
				modelHandle = registry.get<MeshRendererComponent>(entity).modelHandle;
			}
			if (modelHandle != 0 && ctx.renderer) {
				auto* model = ctx.renderer->GetModel(modelHandle);
				if (model) {
					const auto& data = model->GetData();
					if (!data.vertices.empty()) {
						float minX = 1e9f, maxX = -1e9f;
						float minZ = 1e9f, maxZ = -1e9f;
						for (const auto& v : data.vertices) {
							// ワールド座標 = ローカル頂点 * スケール + 位置
							float wx = v.position.x * tc.scale.x + tc.translate.x;
							float wz = v.position.z * tc.scale.z + tc.translate.z;
							minX = std::min(minX, wx);
							maxX = std::max(maxX, wx);
							minZ = std::min(minZ, wz);
							maxZ = std::max(maxZ, wz);
						}
						boundsMinX_ = minX;
						boundsMaxX_ = maxX;
						boundsMinZ_ = minZ;
						boundsMaxZ_ = maxZ;
					}
				}
			}
			boundsCached_ = true;
			return;
		}
		// Groundが見つからない場合はデフォルト値のまま
		boundsCached_ = true;
	}
};

} // namespace Game
