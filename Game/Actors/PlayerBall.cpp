// Game/PlayerBall.cpp
#define NOMINMAX

#include "PlayerBall.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "../ObjectTypes.h"

// ギミックのパス
#include "../gimmick/BoostRingGimmick.h"
#include "../gimmick/GimmickBase.h"
#include "../gimmick/JumpPadGimmick.h"
#include "../gimmick/OnOffComponent.h"
#include "../gimmick/TimedBlock.h"

// パーティクル用
#include "../Engine/Particle.h"

using namespace DirectX;

namespace Game {

// helper: normalize angle to [-pi, pi]
static float NormalizeAngle(float a) {
	const float pi = 3.14159265358979f;
	while (a <= -pi)
		a += 2.0f * pi;
	while (a > pi)
		a -= 2.0f * pi;
	return a;
}

static const int kSCooldownFrames = 30; // frames of cooldown after S-rotate

struct ColliderData {
	std::vector<XMVECTOR> vertices;
	std::vector<int> indices;
};

// メッシュコライダーデータ取得
static void GetColliderData(const Engine::GameObject& obj, ColliderData& outData) {
	outData.vertices.clear();
	outData.indices.clear();

	if (obj.collisionMesh != nullptr) {
		const auto* meshData = static_cast<const Game::CollisionMeshData*>(obj.collisionMesh);
		outData.vertices = meshData->vertices;
		outData.indices = meshData->indices;
		return;
	}

	Game::ObjectType type = static_cast<Game::ObjectType>(obj.type);
	if (type == Game::ObjectType::Slope) {
		outData.vertices = {XMVectorSet(-1, -1, -1, 1), XMVectorSet(1, -1, -1, 1), XMVectorSet(-1, -1, 1, 1), XMVectorSet(1, -1, 1, 1), XMVectorSet(-1, 1, 1, 1), XMVectorSet(1, 1, 1, 1)};
		outData.indices = {0, 4, 1, 1, 4, 5, 2, 3, 5, 2, 5, 4, 0, 1, 3, 0, 3, 2, 0, 2, 4, 1, 5, 3};
	}
}

// 三角形上の最近接点を求める
static XMVECTOR ClosestPtPointTriangle(XMVECTOR p, XMVECTOR a, XMVECTOR b, XMVECTOR c) {
	XMVECTOR ab = b - a;
	XMVECTOR ac = c - a;
	XMVECTOR ap = p - a;
	float d1 = XMVectorGetX(XMVector3Dot(ab, ap));
	float d2 = XMVectorGetX(XMVector3Dot(ac, ap));
	if (d1 <= 0.0f && d2 <= 0.0f)
		return a;

	XMVECTOR bp = p - b;
	float d3 = XMVectorGetX(XMVector3Dot(ab, bp));
	float d4 = XMVectorGetX(XMVector3Dot(ac, bp));
	if (d3 >= 0.0f && d4 <= d3)
		return b;

	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		float v = d1 / (d1 - d3);
		return a + v * ab;
	}

	XMVECTOR cp = p - c;
	float d5 = XMVectorGetX(XMVector3Dot(ab, cp));
	float d6 = XMVectorGetX(XMVector3Dot(ac, cp));
	if (d6 >= 0.0f && d5 <= d6)
		return c;

	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		float w = d2 / (d2 - d6);
		return a + w * ac;
	}

	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + w * (c - b);
	}

	float denom = 1.0f / (va + vb + vc);
	float v = vb * denom;
	float w = vc * denom;
	return a + ab * v + ac * w;
}

bool PlayerBall::WasPressed_(int vk) {
	SHORT now = GetAsyncKeyState(vk);
	bool pressed = ((now & 0x8000) != 0) && ((prevKey_[vk] & 0x8000) == 0);
	prevKey_[vk] = now;
	return pressed;
}

void PlayerBall::Initialize(Engine::Renderer* renderer, Engine::Renderer::MeshHandle mesh, Engine::Renderer::TextureHandle tex) {
	renderer_ = renderer;
	mesh_ = mesh;
	tex_ = tex;

	transform_.translate = Engine::Vector3{0.0f, radius_, 0.0f};
	transform_.rotate = Engine::Vector3{0.0f, 0.0f, 0.0f};
	transform_.scale = Engine::Vector3{radius_ * 2.0f, radius_ * 2.0f, radius_ * 2.0f};

	velocity_ = Engine::Vector3{0.0f, 0.0f, 0.0f};
	grounded_ = false;

	for (int i = 0; i < 256; ++i) {
		prevKey_[i] = GetAsyncKeyState(i);
	}

	// ★追加: コントローラー初期化
	ZeroMemory(&padState_, sizeof(XINPUT_STATE));
	ZeroMemory(&prevPadState_, sizeof(XINPUT_STATE));
}

void PlayerBall::Reset(const Engine::Vector3& pos) {
	transform_.translate = pos;
	velocity_ = Engine::Vector3{0.0f, 0.0f, 0.0f};
	grounded_ = false;
	inputEnabled_ = true;
	performanceTimer_ = 0.0f; // リセット
}

// 物理的な強制ジャンプ(velocity_.y=0.8)を削除
// これにより、リングギミック側で計算された「進行方向への加速（ノーズダイブ含む）」が維持されます。
void PlayerBall::OnBoost() {
	// 演出タイマーセット (GameScene.cppのDrawで見た目だけ跳ねさせる)
	performanceTimer_ = kPerformanceDuration_;

	// 派手なパーティクル爆発
	if (particleSystem_) {
		for (int i = 0; i < 50; ++i) {
			Engine::Vector3 spawnPos = transform_.translate;
			spawnPos.x += ((rand() % 100) - 50) * 0.01f;
			spawnPos.y += ((rand() % 100) - 50) * 0.01f;
			spawnPos.z += ((rand() % 100) - 50) * 0.01f;

			Engine::Vector3 pVel;
			pVel.x = ((rand() % 200) - 100) * 0.03f;
			pVel.y = ((rand() % 200) - 50) * 0.03f;
			pVel.z = ((rand() % 200) - 100) * 0.03f;

			Engine::Vector3 angVel;
			angVel.x = ((rand() % 100) - 50) * 0.2f;
			angVel.y = ((rand() % 100) - 50) * 0.2f;
			angVel.z = ((rand() % 100) - 50) * 0.2f;

			float r = 0.5f + (rand() % 50) * 0.01f;
			float g = 0.5f + (rand() % 50) * 0.01f;
			float b = 0.5f + (rand() % 50) * 0.01f;
			Engine::Vector4 col = {r, g, b, 1.0f};

			float life = 0.5f + (rand() % 50) * 0.01f;
			particleSystem_->Emit(spawnPos, pVel, {0.3f, 0.3f, 0.3f}, col, life, angVel);
		}
	}
}

// 演出進捗
float PlayerBall::GetPerformanceProgress() const {
	if (performanceTimer_ <= 0.0f)
		return 0.0f;
	return 1.0f - (performanceTimer_ / kPerformanceDuration_);
}

void PlayerBall::Update(const std::vector<Engine::GameObject>& objects) {
	// ★追加: コントローラー入力更新
	prevPadState_ = padState_;
	if (XInputGetState(0, &padState_) != ERROR_SUCCESS) {
		ZeroMemory(&padState_, sizeof(XINPUT_STATE));
	}

	Engine::Vector3 input{0, 0, 0};

	// 演出タイマー更新 & 継続エフェクト放出
	if (performanceTimer_ > 0.0f) {
		performanceTimer_ -= 1.0f / 60.0f;
		if (performanceTimer_ < 0.0f)
			performanceTimer_ = 0.0f;

		// スピン中も継続的にパーティクルを放出
		if (particleSystem_) {
			for (int i = 0; i < 3; ++i) {
				Engine::Vector3 spawnPos = transform_.translate;
				spawnPos.y += 0.5f;
				spawnPos.x += ((rand() % 100) - 50) * 0.02f;
				spawnPos.z += ((rand() % 100) - 50) * 0.02f;

				Engine::Vector3 pVel;
				pVel.x = ((rand() % 100) - 50) * 0.01f;
				pVel.y = ((rand() % 100) - 50) * 0.01f;
				pVel.z = ((rand() % 100) - 50) * 0.01f;

				Engine::Vector4 col = {1.0f, 1.0f, 0.5f + (rand() % 50) * 0.01f, 1.0f};
				particleSystem_->Emit(spawnPos, pVel, {0.15f, 0.15f, 0.15f}, col, 0.4f);
			}
		}
	}

	// ----------------------------------------------------
	// 1. 入力処理 (inputEnabled_ が true の時だけ)
	// ----------------------------------------------------
	if (inputEnabled_) {
		// キーボード入力
		if (IsKeyDown_('W'))
			input.z += 1.0f;
		if (IsKeyDown_('D'))
			input.x += 1.0f;
		if (IsKeyDown_('A'))
			input.x -= 1.0f;

		// ★追加: コントローラー左スティック
		float padX = (float)padState_.Gamepad.sThumbLX / 32767.0f;
		float padZ = (float)padState_.Gamepad.sThumbLY / 32767.0f;

		// デッドゾーン
		if (std::abs(padX) < 0.2f)
			padX = 0.0f;
		if (std::abs(padZ) < 0.2f)
			padZ = 0.0f;

		input.x += padX;
		input.z += padZ;

		// 正規化 (キー+パッドで1.0を超えないようにするが、アナログ操作のために長さ1.0以下は許容)
		float len = std::sqrt(input.x * input.x + input.z * input.z);
		if (len > 1.0f) {
			input.x /= len;
			input.z /= len;
		}

		// --- 停止時のインプレース回転 ---
		float planarSpeedNow = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
		const float kStopThreshold = 0.02f;
		// 修正: 入力(前進)がない場合のみ回転可能
		if (planarSpeedNow < kStopThreshold && !IsKeyDown_('W') && std::abs(padZ) < 0.1f) {
			bool turned = false;
			// Aキーまたはスティック左
			if (IsKeyDown_('A') || padX < -0.5f) {
				transform_.rotate.y -= inPlaceYawSpeed_;
				turned = true;
			}
			// Dキーまたはスティック右
			if (IsKeyDown_('D') || padX > 0.5f) {
				transform_.rotate.y += inPlaceYawSpeed_;
				turned = true;
			}
			if (turned) {
				transform_.rotate.y = NormalizeAngle(transform_.rotate.y);
				dir_.x = std::sin(transform_.rotate.y);
				dir_.y = 0.0f;
				dir_.z = std::cos(transform_.rotate.y);
				input.x = 0.0f;
				input.z = 0.0f;
				len = 0.0f;
			}
		}

		// SキーまたはBボタン：減速
		if (WasPressed_('S') || WasPadPressed(XINPUT_GAMEPAD_A)) {
			const float brakeFactor = 0.85f;
			velocity_.x *= brakeFactor;
			velocity_.z *= brakeFactor;
			const float planarSpeed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
			if (planarSpeed < 0.01f) {
				velocity_.x = 0.0f;
				velocity_.z = 0.0f;
			}
		}

		//// ジャンプ (Space または Aボタン)
		//if (grounded_ && (WasPressed_(VK_SPACE) || WasPadPressed(XINPUT_GAMEPAD_A))) {
		//	velocity_.y = jumpSpeed_;
		//	grounded_ = false;
		//}
	} else {
		// 入力無効時（演出中）の減速処理
		if (grounded_) {
			velocity_.x *= 0.9f;
			velocity_.z *= 0.9f;
		}
	}

	// クールダウン更新
	if (sCooldown_ > 0)
		--sCooldown_;
	if (rotateSkipFrames_ > 0)
		--rotateSkipFrames_;

	// 2. 加速計算
	// 入力をワールド座標系（dir_基準）に変換
	Engine::Vector3 right{dir_.z, 0.0f, -dir_.x};
	Engine::Vector3 moveDir{0.0f, 0.0f, 0.0f};
	moveDir.x = dir_.x * input.z + right.x * input.x;
	moveDir.z = dir_.z * input.z + right.z * input.x;

	Engine::Vector3 planarVel{velocity_.x, 0.0f, velocity_.z};
	float len = std::sqrt(input.x * input.x + input.z * input.z);

	// ★修正: アナログスティック対応 (lenが小さい場合は速度も小さくする)
	// 元のコードの挙動(入力あればMaxSpeed)を維持しつつ、コントローラーのアナログ感を出すため、
	// 係数を (len > 0.0001f ? 1.0f : 0.0f) から len に変更
	float speedFactor = std::min(len, 1.0f);
	Engine::Vector3 targetPlanarVel{moveDir.x * maxSpeed_ * speedFactor, 0.0f, moveDir.z * maxSpeed_ * speedFactor};

	// 補間係数
	const float accelLerp = std::clamp(moveAccel_, 0.001f, 0.5f);
	planarVel.x += (targetPlanarVel.x - planarVel.x) * accelLerp;
	planarVel.z += (targetPlanarVel.z - planarVel.z) * accelLerp;

	// 入力なし時の減衰
	if (len <= 0.0001f) {
		const float idleDamp = 0.94f;
		planarVel.x *= idleDamp;
		planarVel.z *= idleDamp;
		if (std::sqrt(planarVel.x * planarVel.x + planarVel.z * planarVel.z) < 0.001f) {
			planarVel.x = 0.0f;
			planarVel.z = 0.0f;
		}
	}

	// 速度制限
	float planarSpeed = std::sqrt(planarVel.x * planarVel.x + planarVel.z * planarVel.z);
	if (planarSpeed > maxSpeed_) {
		float s = maxSpeed_ / (planarSpeed + 1e-6f);
		planarVel.x *= s;
		planarVel.z *= s;
	}

	velocity_.x = planarVel.x;
	velocity_.z = planarVel.z;

	// 3. 重力
	velocity_.y -= gravity_;

	// 4. 物理サブステップ & 移動
	float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z);
	int steps = 2 + (int)(speed / 0.15f);
	if (steps > 20)
		steps = 20;
	float dt = 1.0f / (float)steps;
	grounded_ = false;

	for (int s = 0; s < steps; ++s) {
		transform_.translate.x += velocity_.x * dt;
		transform_.translate.y += velocity_.y * dt;
		transform_.translate.z += velocity_.z * dt;
		SolveCollisions_(objects);
	}

	// 5. 回転演出（ボールの転がり）
	static Engine::Vector3 prevPos = transform_.translate;
	Engine::Vector3 dpFull = {transform_.translate.x - prevPos.x, transform_.translate.y - prevPos.y, transform_.translate.z - prevPos.z};
	Engine::Vector3 dp = {dpFull.x, 0.0f, dpFull.z};
	prevPos = transform_.translate;

	const float invR = (radius_ > 0.0001f) ? (1.0f / radius_) : 1.0f;
	Engine::Vector3 forward = {dir_.x, 0.0f, dir_.z};
	float fLen = std::sqrt(forward.x * forward.x + forward.z * forward.z);
	if (fLen > 1e-6f) {
		forward.x /= fLen;
		forward.z /= fLen;
	} else {
		forward.x = 0.0f;
		forward.z = 1.0f;
	}

	float forwardMove = dp.x * forward.x + dp.z * forward.z;
	const float rollMul = 0.6f;
	transform_.rotate.x += (forwardMove * invR) * rollMul;
	transform_.rotate.z = 0.0f; // Z回転は固定

	// 向きベクトル更新
	dir_.x = std::sin(transform_.rotate.y);
	dir_.y = 0.0f;
	dir_.z = std::cos(transform_.rotate.y);

	// 6. 進行方向へのY軸回転（演出中は自動回転しない）
	if (inputEnabled_) {
		float planarMoveLen = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
		if (planarMoveLen > 0.0001f && rotateSkipFrames_ <= 0) {
			float targetYaw = std::atan2(velocity_.x, velocity_.z);
			float diff = NormalizeAngle(targetYaw - transform_.rotate.y);
			const float turnSpeed = 0.08f;
			float desiredDelta = diff * turnSpeed;
			float maxDelta = maxTurnSpeed_;
			desiredDelta = std::clamp(desiredDelta, -maxDelta, maxDelta);

			transform_.rotate.y += desiredDelta;
			transform_.rotate.y = NormalizeAngle(transform_.rotate.y);
		}
	}
}

// 衝突解決
void PlayerBall::SolveCollisions_(const std::vector<Engine::GameObject>& objects) {
	XMVECTOR SpherePos = XMLoadFloat3((XMFLOAT3*)&transform_.translate);
	float r = radius_;
	float rSq = r * r;
	static ColliderData colData;

	const int kIterations = 4;

	for (int iter = 0; iter < kIterations; ++iter) {
		bool collided = false;
		float maxPenetration = -1.0f;
		XMVECTOR bestNormal = XMVectorZero();
		const Engine::GameObject* hitObj = nullptr;

		// ----------------------------------------------------
		// トリガー判定（衝突レスポンスなし）
		// ----------------------------------------------------
		for (const auto& obj : objects) {
			if (!obj.isVisible || !obj.gimmick)
				continue;

			// BoostRing
			if (auto* br = dynamic_cast<Game::BoostRingGimmick*>(obj.gimmick)) {
				br->CheckTrigger(this);
			}
			// JumpPad
			if (auto* jp = dynamic_cast<Game::JumpPadGimmick*>(obj.gimmick)) {
				jp->OnCollision(this);
			}
		}

		// ----------------------------------------------------
		// 物理衝突チェック
		// ----------------------------------------------------
		for (const auto& obj : objects) {
			if (!obj.isVisible)
				continue;
			if (!obj.useMeshCollision)
				continue;

			// ワールド行列
			XMMATRIX S = XMMatrixScaling(obj.transform.scale.x, obj.transform.scale.y, obj.transform.scale.z);
			XMMATRIX R = XMMatrixRotationRollPitchYaw(obj.transform.rotate.x, obj.transform.rotate.y, obj.transform.rotate.z);
			XMMATRIX T = XMMatrixTranslation(obj.transform.translate.x, obj.transform.translate.y, obj.transform.translate.z);
			XMMATRIX worldMatrix = S * R * T;

			// --- Cube Primitive (OBB) ---
			if (obj.type == (uint32_t)Game::ObjectType::Cube) {
				XMVECTOR center = T.r[3];
				XMVECTOR axis[3] = {XMVector3Normalize(R.r[0]), XMVector3Normalize(R.r[1]), XMVector3Normalize(R.r[2])};
				float halfSize[3] = {
				    (obj.localAABBMax.x - obj.localAABBMin.x) * 0.5f * std::abs(obj.transform.scale.x), (obj.localAABBMax.y - obj.localAABBMin.y) * 0.5f * std::abs(obj.transform.scale.y),
				    (obj.localAABBMax.z - obj.localAABBMin.z) * 0.5f * std::abs(obj.transform.scale.z)};

				XMVECTOR diff = SpherePos - center;
				float dists[3] = {XMVectorGetX(XMVector3Dot(diff, axis[0])), XMVectorGetX(XMVector3Dot(diff, axis[1])), XMVectorGetX(XMVector3Dot(diff, axis[2]))};
				float clamps[3] = {std::clamp(dists[0], -halfSize[0], halfSize[0]), std::clamp(dists[1], -halfSize[1], halfSize[1]), std::clamp(dists[2], -halfSize[2], halfSize[2])};

				bool isInside = true;
				for (int i = 0; i < 3; ++i)
					if (std::abs(dists[i] - clamps[i]) > 1e-5f)
						isInside = false;

				float pen = 0.0f;
				XMVECTOR norm = XMVectorZero();

				if (isInside) {
					float minD = FLT_MAX;
					for (int i = 0; i < 3; ++i) {
						float d1 = halfSize[i] - dists[i];
						float d2 = -halfSize[i] - dists[i];
						if (std::abs(d1) < minD) {
							minD = std::abs(d1);
							norm = axis[i];
						}
						if (std::abs(d2) < minD) {
							minD = std::abs(d2);
							norm = -axis[i];
						}
					}
					pen = minD + r;
				} else {
					XMVECTOR closestP = center + axis[0] * clamps[0] + axis[1] * clamps[1] + axis[2] * clamps[2];
					XMVECTOR pushVec = SpherePos - closestP;
					float distSq = XMVectorGetX(XMVector3LengthSq(pushVec));
					if (distSq < rSq && distSq > 1e-6f) {
						float dist = std::sqrt(distSq);
						pen = r - dist;
						norm = pushVec / dist;
					}
				}

				if (pen > 0.0f && pen > maxPenetration) {
					maxPenetration = pen;
					bestNormal = norm;
					collided = true;
					hitObj = &obj;
				}
				continue;
			}

			// --- Mesh Collision ---
			GetColliderData(obj, colData);
			if (colData.vertices.empty())
				continue;

			std::vector<XMVECTOR> worldVerts = colData.vertices;
			for (auto& v : worldVerts)
				v = XMVector3TransformCoord(v, worldMatrix);

			for (size_t i = 0; i < colData.indices.size(); i += 3) {
				XMVECTOR p0 = worldVerts[colData.indices[i]];
				XMVECTOR p1 = worldVerts[colData.indices[i + 1]];
				XMVECTOR p2 = worldVerts[colData.indices[i + 2]];

				XMVECTOR triNorm = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
				XMVECTOR cp = ClosestPtPointTriangle(SpherePos, p0, p1, p2);
				XMVECTOR diff = SpherePos - cp;
				float dSq = XMVectorGetX(XMVector3LengthSq(diff));

				if (dSq < rSq) {
					float dist = std::sqrt(dSq);
					float pen = r - dist;
					XMVECTOR norm;

					if (dist < 1e-6f) {
						norm = triNorm;
					} else {
						norm = diff / dist;
					}

					// ポリゴン継ぎ目対策
					if (XMVectorGetY(triNorm) > 0.1f) {
						norm = triNorm;
						float planeDist = XMVectorGetX(XMVector3Dot(SpherePos - p0, norm));
						pen = r - planeDist;
					}

					if (pen > 0.0f && pen > maxPenetration) {
						maxPenetration = pen;
						bestNormal = norm;
						collided = true;
						hitObj = &obj;
					}
				}
			}
		}

		if (collided) {
			// ★ON/OFFブロック: OFFなら無視
			if (hitObj != nullptr && hitObj->gimmick != nullptr) {
				auto* onoff = dynamic_cast<Game::OnOffComponent*>(hitObj->gimmick);
				if (onoff && !onoff->IsSolidNow()) {
					collided = false;
					maxPenetration = -1.0f;
					bestNormal = XMVectorZero();
					hitObj = nullptr;
					continue;
				}
			}
			// ★TimedBlock: OFFなら無視
			if (hitObj != nullptr && hitObj->gimmick != nullptr) {
				auto* tb = dynamic_cast<Game::TimedBlock*>(hitObj->gimmick);
				if (tb && !tb->IsSolidNow()) {
					collided = false;
					maxPenetration = -1.0f;
					bestNormal = XMVectorZero();
					hitObj = nullptr;
					continue;
				}
			}

			// ★★★★★ 衝突通知 ★★★★★
			if (hitObj != nullptr && hitObj->gimmick != nullptr) {
				hitObj->gimmick->OnCollision(this);
			}

			// ★★★★★ 衝突エフェクト (plane.objによる破片) ★★★★★
			if (particleSystem_ != nullptr) {
				Engine::Vector3 nVec;
				XMStoreFloat3((XMFLOAT3*)&nVec, bestNormal);

				// 法線方向への衝突速度成分 (負なら向かっている)
				float dot = velocity_.x * nVec.x + velocity_.y * nVec.y + velocity_.z * nVec.z;

				// 一定以上の衝撃（-0.2f以上の速度でぶつかった）ならパーティクル発生
				// ※地面を転がっているだけの時は dot はほぼ0になるので発生しない
				if (dot < -0.2f) {
					// 発生位置: 球の中心から法線の逆方向に半径分だけ進んだ点（接触点付近）
					Engine::Vector3 spawnPos = transform_.translate;
					spawnPos.x -= nVec.x * radius_;
					spawnPos.y -= nVec.y * radius_;
					spawnPos.z -= nVec.z * radius_;

					for (int i = 0; i < 4; ++i) {
						// 速度: 反射ベクトル + ランダム
						Engine::Vector3 pVel;
						pVel.x = nVec.x * 2.0f + ((rand() % 100) - 50) * 0.02f;
						pVel.y = nVec.y * 2.0f + ((rand() % 100) - 50) * 0.02f;
						pVel.z = nVec.z * 2.0f + ((rand() % 100) - 50) * 0.02f;

						// 回転
						Engine::Vector3 angVel;
						angVel.x = ((rand() % 100) - 50) * 0.1f;
						angVel.y = ((rand() % 100) - 50) * 0.1f;
						angVel.z = ((rand() % 100) - 50) * 0.1f;

						// 色 (適当に破片っぽく白〜グレー)
						float c = 0.5f + (rand() % 50) * 0.01f;
						Engine::Vector4 col = {c, c, c, 1.0f};

						// Emit
						particleSystem_->Emit(spawnPos, pVel, {0.3f, 0.3f, 0.3f}, col, 0.5f, angVel);
					}
				}
			}
			// ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★

			// 1. 位置の修正
			SpherePos += bestNormal * (maxPenetration + 0.001f);

			// 2. 速度のスライド
			Engine::Vector3 nVec;
			XMStoreFloat3((XMFLOAT3*)&nVec, bestNormal);
			float dot = velocity_.x * nVec.x + velocity_.y * nVec.y + velocity_.z * nVec.z;
			if (dot < 0.0f) {
				velocity_.x -= nVec.x * dot;
				velocity_.y -= nVec.y * dot;
				velocity_.z -= nVec.z * dot;
			}

			// 3. 接地判定
			if (nVec.y > 0.5f && velocity_.y <= 0.02f) {
				grounded_ = true;
			}
		} else {
			break;
		}
	}

	XMStoreFloat3((XMFLOAT3*)&transform_.translate, SpherePos);
}

void PlayerBall::Draw() const {
	if (!renderer_)
		return;
	renderer_->DrawMesh(mesh_, tex_, transform_, color_);
}

} // namespace Game