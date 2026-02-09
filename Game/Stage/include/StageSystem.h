// StageSystem.h
#pragma once
#include "StageRuntime.h"
#include <string>
#include <unordered_map>

struct CameraSettings {
	float distance = 8.0f;
	float pitchDeg = 15.0f;
	float yawClampDeg = 180.0f;
	float upBlend = 0.0f; // 0=world up, 1=gravity up (例)
};

struct StageSystem {
	StageRuntime rt;

	// スイッチなどの状態
	std::unordered_map<std::string, bool> activated;

	// ドアの開き具合（0..1）
	std::unordered_map<std::string, float> doorOpenT;

	// 現在のカメラ設定（ボリュームで上書き）
	CameraSettings cam;

	void Initialize(const StageRuntime& runtime);

	// プレイヤー位置、ボール位置を渡す（あなたの実体から取って渡す）
	void Update(float dt, Vec3 playerPos, Vec3 ballPos);

	// 判定結果（あなたの Scene に返す用）
	bool IsGoalReached(Vec3 ballPos) const;

	// カメラ（Scene側で使う）
	CameraSettings GetCameraSettings() const { return cam; }

private:
	void UpdateSwitches(Vec3 playerPos, Vec3 ballPos);
	void UpdateDoors(float dt);
	void UpdateCamera(Vec3 playerPos);

	static bool PointInAabb(Vec3 p, const AABB& a);
	static float GetParamF(const std::unordered_map<std::string, std::string>& p, const char* key, float defv);
};
