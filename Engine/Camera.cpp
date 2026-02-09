#include "Camera.h"
#include <dinput.h>
using namespace DirectX;

namespace Engine {

// ===============================
// 初期化（既存）
// ===============================
void Camera::Initialize() {
	pos_ = {8.0f, 10.0f, -20.0f};
	rot_ = {0, 0, 0};
	shakeTime_ = 9999.0f;
	shakeDuration_ = 0.0f;
	shakeAmpPos_ = shakeAmpRot_ = 0.0f;
	shakeOfs_ = {0, 0, 0};
	shakeRot_ = {0, 0, 0};
	UpdateView();
}

// ===============================
// デバッグ更新（既存APIは維持）
// ===============================
void Camera::Update(const Input& in) {
	// 矢印キーで回転
	if (in.Down(DIK_LEFT))
		rot_.y -= rotSpd_;
	if (in.Down(DIK_RIGHT))
		rot_.y += rotSpd_;
	if (in.Down(DIK_UP))
		rot_.x -= rotSpd_;
	if (in.Down(DIK_DOWN))
		rot_.x += rotSpd_;

	// WASD+QEで移動
	XMFLOAT3 mv{};
	if (in.Down(DIK_W))
		mv.z += moveSpd_;
	if (in.Down(DIK_S))
		mv.z -= moveSpd_;
	if (in.Down(DIK_A))
		mv.x -= moveSpd_;
	if (in.Down(DIK_D))
		mv.x += moveSpd_;
	if (in.Down(DIK_Q))
		mv.y += moveSpd_;
	if (in.Down(DIK_E))
		mv.y -= moveSpd_;

	// 回転を考慮したローカル→ワールド移動
	XMMATRIX r = XMMatrixRotationRollPitchYaw(rot_.x, rot_.y, rot_.z);
	XMVECTOR v = XMVector3TransformCoord(XMLoadFloat3(&mv), r);
	XMStoreFloat3(&pos_, XMLoadFloat3(&pos_) + v);

	UpdateView();
}

// ===============================
// 1フレーム進める（シェイクのみ）
// ===============================
void Camera::Tick(float dt) {
	if (!IsShaking()) {
		shakeOfs_ = {0, 0, 0};
		shakeRot_ = {0, 0, 0};
		return;
	}

	shakeTime_ += dt;
	float t = shakeTime_ / shakeDuration_;
	if (t >= 1.0f) {
		StopShake();
		return;
	}

	// だんだん弱くなる減衰（カーブはお好みで）
	float atten = 1.0f - t; // 線形減衰

	shakeOfs_.x = unit_(rng_) * shakeAmpPos_ * atten;
	shakeOfs_.y = unit_(rng_) * shakeAmpPos_ * atten;
	shakeOfs_.z = unit_(rng_) * shakeAmpPos_ * atten;

	shakeRot_.x = unit_(rng_) * shakeAmpRot_ * atten;
	shakeRot_.y = unit_(rng_) * shakeAmpRot_ * atten;
	shakeRot_.z = unit_(rng_) * shakeAmpRot_ * atten;

	// シェイクは view 再構築に反映されるので、ここで更新
	UpdateView();
}

// ===============================
// View再計算（位置・回転にシェイクを足してから）
// ===============================
void Camera::UpdateView() {
	// 位置/回転 + シェイク
	XMFLOAT3 p = {pos_.x + shakeOfs_.x, pos_.y + shakeOfs_.y, pos_.z + shakeOfs_.z};
	XMFLOAT3 rAdd = {rot_.x + shakeRot_.x, rot_.y + shakeRot_.y, rot_.z + shakeRot_.z};

	XMMATRIX r = XMMatrixRotationRollPitchYaw(rAdd.x, rAdd.y, rAdd.z);
	XMMATRIX t = XMMatrixTranslation(p.x, p.y, p.z);
	view_ = XMMatrixInverse(nullptr, r * t);
}

// ===============================
// 射影行列（既存）
// ===============================
void Camera::SetProjection(float fovY, float aspect, float nearZ, float farZ) { proj_ = XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ); }

// ===============================
// 位置セット（追加）
// ===============================
void Camera::SetPosition(const XMFLOAT3& p) {
	pos_ = p;
	UpdateView();
}
void Camera::SetPosition(float x, float y, float z) {
	pos_ = {x, y, z};
	UpdateView();
}

// ===============================
// 回転セット（必要なら）
// ===============================
void Camera::SetRotation(const XMFLOAT3& r) {
	rot_ = r;
	UpdateView();
}
void Camera::SetRotation(float pitch, float yaw, float roll) {
	rot_ = {pitch, yaw, roll};
	UpdateView();
}

// ===============================
// LookAt（ビュー行列を直接構築）
//  - シェイク中は eye/at を同じだけオフセットしてブレさせる
// ===============================
void Camera::LookAt(const XMFLOAT3& target, const XMFLOAT3& up) {
	XMFLOAT3 eyeP = {pos_.x + shakeOfs_.x, pos_.y + shakeOfs_.y, pos_.z + shakeOfs_.z};
	XMFLOAT3 atP = {target.x + shakeOfs_.x, target.y + shakeOfs_.y, target.z + shakeOfs_.z};

	XMVECTOR eye = XMLoadFloat3(&eyeP);
	XMVECTOR at = XMLoadFloat3(&atP);
	XMVECTOR upv = XMLoadFloat3(&up);
	view_ = XMMatrixLookAtLH(eye, at, upv);

	// 回転ノイズは簡易対応：LookAt後に微回転を適用
	if (IsShaking()) {
		XMMATRIX r = XMMatrixRotationRollPitchYaw(shakeRot_.x, shakeRot_.y, shakeRot_.z);
		view_ = r * view_;
	}
}

void Camera::LookAt(float tx, float ty, float tz, float ux, float uy, float uz) { LookAt(XMFLOAT3{tx, ty, tz}, XMFLOAT3{ux, uy, uz}); }

// ===============================
// シェイク制御
// ===============================
void Camera::StartShake(float duration, float ampPos, float ampRot) {
	shakeDuration_ = (duration > 0.0f) ? duration : 0.0001f;
	shakeTime_ = 0.0f;
	shakeAmpPos_ = ampPos;
	shakeAmpRot_ = ampRot;
}

void Camera::StopShake() {
	shakeTime_ = 9999.0f;
	shakeDuration_ = 0.0f;
	shakeOfs_ = {0, 0, 0};
	shakeRot_ = {0, 0, 0};
	UpdateView();
}

} // namespace Engine
