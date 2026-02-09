#pragma once
#include <Windows.h>
#include <vector>

// ★追加: XInput
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include "../Engine/GameObject.h"
#include "AABB.h"
#include "Renderer.h"
#include "Transform.h"
#include "hMath.h"

// ParticleSystemの前方宣言
namespace Engine {
class ParticleSystem;
}

namespace Game {

class PlayerBall final {
public:
	void Initialize(Engine::Renderer* renderer, Engine::Renderer::MeshHandle mesh, Engine::Renderer::TextureHandle tex);
	void Reset(const Engine::Vector3& pos);

	void Update(const std::vector<Engine::GameObject>& objects);

	void Draw() const;

	const Engine::Vector3& GetPosition() const { return transform_.translate; }
	float GetRadius() const { return radius_; }

	// 外部から速度を変更する関数（ハンマーやジャンプ台、演出で使用）
	void SetVelocity(const Engine::Vector3& v) { velocity_ = v; }
	const Engine::Vector3& GetVelocity() const { return velocity_; }

	// 外部ギミックが位置を固定したい場合に使う（床・トラップなど）
	void SetPosition(const Engine::Vector3& p) { transform_.translate = p; }

	// 外部ギミックが接地扱いにしたい場合に使う（床など）
	void SetGrounded(bool g) { grounded_ = g; }

	bool IsGrounded() const { return grounded_; }

	// 向き取得（カメラや他処理で利用）
	Engine::Vector3 GetDirection() const { return dir_; }

	// 外部から操作を禁止/許可する（ゴール演出用）
	void SetInputEnabled(bool enabled) { inputEnabled_ = enabled; }

	// 回転の取得・設定（ゴール演出のスピン用）
	Engine::Vector3 GetRotation() const { return transform_.rotate; }
	void SetRotation(const Engine::Vector3& r) { transform_.rotate = r; }

	// 衝突エフェクト用のパーティクルシステムをセット
	void SetParticleSystem(Engine::ParticleSystem* ps) { particleSystem_ = ps; }

	// ブースト時の演出開始
	void OnBoost();
	// 演出中かどうか
	bool IsPerformanceActive() const { return performanceTimer_ > 0.0f; }
	// 演出の進捗 (0.0 -> 1.0)
	float GetPerformanceProgress() const;

private:
	bool IsKeyDown_(int vk) const { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
	bool WasPressed_(int vk);

	// ★追加: コントローラーのボタン判定
	bool WasPadPressed(WORD button) const { return ((padState_.Gamepad.wButtons & button) != 0) && ((prevPadState_.Gamepad.wButtons & button) == 0); }

	void SolveCollisions_(const std::vector<Engine::GameObject>& objects);

private:
	Engine::Renderer* renderer_ = nullptr;
	Engine::Renderer::MeshHandle mesh_ = 0;
	Engine::Renderer::TextureHandle tex_ = 0;

	Engine::Transform transform_{};
	Engine::Vector3 velocity_{0, 0, 0};
	Engine::Vector4 color_{1, 1, 1, 1};

	float radius_ = 1.0f;

	// パラメータ
	float moveAccel_ = 0.003f;
	float maxSpeed_ = 2.0f;
	float jumpSpeed_ = 0.6f;
	float gravity_ = 0.035f / 3.0f; // 重力調整

	bool grounded_ = false;
	SHORT prevKey_[256]{};

	// ★追加: コントローラー入力状態
	XINPUT_STATE padState_{};
	XINPUT_STATE prevPadState_{};

	// 進行方向ベクトル
	Engine::Vector3 dir_{0.0f, 0.0f, 1.0f};

	// Sキー旋回クールダウン
	int sCooldown_ = 0;
	int rotateSkipFrames_ = 0;

	// 最大旋回速度
	float maxTurnSpeed_ = 0.12f;

	// 停止時の旋回速度
	float inPlaceYawSpeed_ = 0.035f;

	// 入力有効フラグ
	bool inputEnabled_ = true;

	// パーティクルシステムへのポインタ
	Engine::ParticleSystem* particleSystem_ = nullptr;

	// 演出用タイマー
	float performanceTimer_ = 0.0f;
	const float kPerformanceDuration_ = 0.6f; // 演出時間
};

} // namespace Game