#include "ISystem.h"
#include <cmath>
#include <algorithm>
#include "../Engine/Input.h" // ★追加

namespace Game {

class CameraFollowSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying || !ctx.camera) return;

		auto view = registry.view<CameraTargetComponent, TransformComponent>();
		for (auto entity : view) {
			auto& ct = view.get<CameraTargetComponent>(entity);
			if (!ct.enabled) continue;

			// ★追加: マウスホイールによるズーム
			auto* inputIns = ::Engine::Input::GetInstance();
			if (inputIns) {
				float wheel = inputIns->GetMouseWheelDelta();
				if (std::abs(wheel) > 0.001f) {
					ct.distance -= wheel * 2.0f; // 感度調整（1クリックで2m移動）
					ct.distance = std::clamp(ct.distance, 2.0f, 30.0f); // 範囲制限
				}
			}

			auto& tc = view.get<TransformComponent>(entity);
			DirectX::XMFLOAT3 targetPos = tc.translate;

			// ★追加: ホイール押し込みによるターゲットロック
			bool currentMButton = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
			static bool prevMButton = false;
			if (currentMButton && !prevMButton) {
				if (ct.lockedTarget != entt::null) {
					ct.lockedTarget = entt::null; // ロック解除
				} else {
					// 最も近い敵を探す
					float minDist = 50.0f;
					auto enemies = registry.view<TagComponent, TransformComponent>();
					for (auto e : enemies) {
						if (enemies.get<TagComponent>(e).tag == TagType::Enemy) {
							if (registry.all_of<HealthComponent>(e) && registry.get<HealthComponent>(e).isDead) continue;
							auto& eTc = enemies.get<TransformComponent>(e);
							float dx = eTc.translate.x - targetPos.x;
							float dz = eTc.translate.z - targetPos.z;
							float dist = std::sqrt(dx*dx + dz*dz);
							if (dist < minDist) {
								minDist = dist;
								ct.lockedTarget = e;
							}
						}
					}
				}
			}
			prevMButton = currentMButton;

			// ロック対象が死んだり無効になったら、次の敵へ自動ロックオン（チェインロック）
			if (ct.lockedTarget != entt::null) {
				if (!registry.valid(ct.lockedTarget) || 
					(registry.all_of<HealthComponent>(ct.lockedTarget) && registry.get<HealthComponent>(ct.lockedTarget).isDead)) {
					
					ct.lockedTarget = entt::null; // 一旦解除

					// 次の敵を探す
					float minDist = 50.0f;
					auto enemies = registry.view<TagComponent, TransformComponent>();
					for (auto e : enemies) {
						if (enemies.get<TagComponent>(e).tag == TagType::Enemy) {
							if (registry.all_of<HealthComponent>(e) && registry.get<HealthComponent>(e).isDead) continue;
							auto& eTc = enemies.get<TransformComponent>(e);
							float dx = eTc.translate.x - targetPos.x;
							float dz = eTc.translate.z - targetPos.z;
							float dist = std::sqrt(dx*dx + dz*dz);
							if (dist < minDist) {
								minDist = dist;
								ct.lockedTarget = e;
							}
						}
					}
				}
			}

			if (registry.all_of<PlayerInputComponent>(entity)) {
				auto& pi = registry.get<PlayerInputComponent>(entity);
				if (pi.enabled) {
					auto rot = ctx.camera->Rotation();
					
					if (ct.lockedTarget != entt::null && registry.valid(ct.lockedTarget)) {
						// ターゲットの方向を向く
						auto& eTc = registry.get<TransformComponent>(ct.lockedTarget);
						float dx = eTc.translate.x - targetPos.x;
						float dz = eTc.translate.z - targetPos.z;
						float targetYaw = std::atan2(dx, dz);
						
						// スムーズにターゲットへ向かせる
						float diff = targetYaw - rot.y;
						while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
						while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
						
						rot.y += diff * std::min(1.0f, 15.0f * ctx.dt);
					} else {
						rot.y += pi.cameraYaw;
					}

					rot.x += pi.cameraPitch;

					const float PITCH_LIMIT = 1.5f;
					if (rot.x > PITCH_LIMIT) rot.x = PITCH_LIMIT;
					if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;

					ctx.camera->SetRotation(rot);
				}
			}

			auto curRot = ctx.camera->Rotation();
			float camSy = std::sin(curRot.y);
			float camCy = std::cos(curRot.y);
			float camSx = std::sin(curRot.x);
			float camCx = std::cos(curRot.x);

			DirectX::XMFLOAT3 offset = {
				-camSy * camCx * ct.distance,
				ct.height + camSx * ct.distance,
				-camCy * camCx * ct.distance
			};

			DirectX::XMFLOAT3 desiredPos = {
				targetPos.x + offset.x,
				targetPos.y + offset.y,
				targetPos.z + offset.z
			};

			// ★変更: カメラの回転と同期させるため、位置の遅延を無くし即座に追従させる
			ctx.camera->SetPosition(desiredPos);
			break;
		}
	}
};

} // namespace Game
