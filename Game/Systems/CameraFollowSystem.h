#include "ISystem.h"
#include <cmath>
#include <algorithm>
#include "../Engine/Input.h" // ★追加
#include "../Scripts/PhaseSystemScript.h" // ★追加: 準備フェーズ判定用

namespace Game {

class CameraFollowSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying || !ctx.camera) return;
		if (Game::PhaseSystemScript::IsPhase() == Game::PhaseSystemScript::InsertPhase) return; // ★インサート中はカメラ上書きを停止

		auto view = registry.view<CameraTargetComponent, TransformComponent>();
		for (auto entity : view) {
			auto& ct = view.get<CameraTargetComponent>(entity);
			if (!ct.enabled) continue;

			if (ct.distance < 10.0f) {
				ct.distance = 10.0f;
			}
			// ※ height はフェーズごとに制御するため、ここでの強制上書きは削除

			// ★追加: マウスホイールによるズーム（準備フェーズ中は無効）
			bool isPrep = (Game::PhaseSystemScript::IsPhase() == Game::PhaseSystemScript::PreparationPhase);
			auto* inputIns = ::Engine::Input::GetInstance();
			if (inputIns && !isPrep) {
				float wheel = inputIns->GetMouseWheelDelta();
				if (std::abs(wheel) > 0.001f) {
					ct.distance -= wheel * 2.0f; // 感度調整（1クリックで2m移動）
					// 下限値を初期距離の10.0fとし、これ以上ズームインしてカメラが下がらないように制限
					ct.distance = std::clamp(ct.distance, 10.0f, 35.0f); // 範囲制限
				}
			}

			auto& tc = view.get<TransformComponent>(entity);
			DirectX::XMFLOAT3 targetPos = tc.translate;
			targetPos.y += 1.0f; // ★追加: プレイヤーの原点が足元になったため、カメラの注視点を中心(胸元)にずらす

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
					
					float yawInput = pi.cameraYaw;
					float pitchInput = pi.cameraPitch;
					// 準備フェーズ中もPlayerInputSystem側でマウス/スティックの入力を処理しているため、ここでの上書きは不要

					if (ct.lockedTarget != entt::null && registry.valid(ct.lockedTarget)) {
						auto& eTc = registry.get<TransformComponent>(ct.lockedTarget);
						float dx = eTc.translate.x - targetPos.x;
						float dz = eTc.translate.z - targetPos.z;
						float targetYaw = std::atan2(dx, dz);
						
						float diff = targetYaw - rot.y;
						while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
						while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
						
						rot.y += diff * std::min(1.0f, 15.0f * ctx.dt);
					} else {
						rot.y += yawInput;
					}

					rot.x += pitchInput;

					const float PITCH_LIMIT = 1.5f;
					if (rot.x > PITCH_LIMIT) rot.x = PITCH_LIMIT;
					if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;

					ctx.camera->SetRotation(rot);
				}
			}

			// ★調整: 『鳴潮』風のフレーミング（キャラクターを中央より少し下に配置）
			float verticalFraming = 0.8f; // キャラクターが画面中央より少し下に見えるように調整
			targetPos.y += verticalFraming;

			// ★速度計算と動的スムージング
			DirectX::XMFLOAT3 prevPos = ctx.camera->Position();
			static DirectX::XMFLOAT3 lastTargetPos = targetPos;
			float dx = targetPos.x - lastTargetPos.x;
			float dz = targetPos.z - lastTargetPos.z;
			float moveDist = std::sqrt(dx * dx + dz * dz);
			float currentSpeed = moveDist / (ctx.dt > 0 ? ctx.dt : 0.016f);
			
			// ★追加: ターゲットがワープ（フェーズ移行時など）した場合はスピードを0にする
			if (moveDist > 5.0f) {
				currentSpeed = 0.0f;
			}
			
			lastTargetPos = targetPos;

			// ★動的距離調整: スピードに合わせて距離を引く
			float targetDistance = ct.distance + std::min(currentSpeed * 0.15f, 12.0f);
			
			auto curRot = ctx.camera->Rotation();
			float camSy = std::sin(curRot.y);
			float camCy = std::cos(curRot.y);
			float camSx = std::sin(curRot.x);
			float camCx = std::cos(curRot.x);

			DirectX::XMFLOAT3 offset = {
				-camSy * camCx * targetDistance,
				ct.height + camSx * targetDistance,
				-camCy * camCx * targetDistance
			};

			DirectX::XMFLOAT3 desiredPos = {
				targetPos.x + offset.x,
				targetPos.y + offset.y,
				targetPos.z + offset.z
			};

			// ★スピードに応じて追従の「粘り」を変える
			float followStiffness = (currentSpeed > 30.0f) ? 0.005f : 0.0001f;
			float lerpFactor = 1.0f - std::pow(followStiffness, ctx.dt);
			
			DirectX::XMFLOAT3 nextPos;
			nextPos.x = prevPos.x + (desiredPos.x - prevPos.x) * lerpFactor;
			nextPos.y = prevPos.y + (desiredPos.y - prevPos.y) * lerpFactor;
			nextPos.z = prevPos.z + (desiredPos.z - prevPos.z) * lerpFactor;

			// ★FOV Kick: 高速移動時に視野を広げてがたつきを抑える
			float baseFov = 0.785f;
			float targetFov = baseFov + std::min(currentSpeed * 0.002f, 0.25f);
			static float currentFov = baseFov;
			currentFov += (targetFov - currentFov) * std::min(1.0f, 5.0f * ctx.dt);
			ctx.camera->SetProjection(currentFov, 1280.0f/720.0f, 0.1f, 1000.0f);

			ctx.camera->SetPosition(nextPos);
			break;
		}
	}
};

} // namespace Game
