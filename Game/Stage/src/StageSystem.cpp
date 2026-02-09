// StageSystem.cpp
#include "StageSystem.h"
#include <algorithm>

bool StageSystem::PointInAabb(Vec3 p, const AABB& a) { return (p.x >= a.minv.x && p.x <= a.maxv.x && p.y >= a.minv.y && p.y <= a.maxv.y && p.z >= a.minv.z && p.z <= a.maxv.z); }

float StageSystem::GetParamF(const std::unordered_map<std::string, std::string>& p, const char* key, float defv) {
	auto it = p.find(key);
	if (it == p.end())
		return defv;
	try {
		return std::stof(it->second);
	} catch (...) {
		return defv;
	}
}

void StageSystem::Initialize(const StageRuntime& runtime) {
	rt = runtime;
	activated.clear();
	doorOpenT.clear();

	// 初期化：Switchはfalse、Doorは閉じ
	for (auto& e : rt.entityRuntimes) {
		if (e.type == "Switch")
			activated[e.id] = false;
		if (e.type == "Door")
			doorOpenT[e.id] = 0.0f;
	}

	cam = {}; // default
}

void StageSystem::Update(float dt, Vec3 playerPos, Vec3 ballPos) {
	UpdateSwitches(playerPos, ballPos);
	UpdateDoors(dt);
	UpdateCamera(playerPos);
}

void StageSystem::UpdateSwitches(Vec3 playerPos, Vec3 ballPos) {
	for (auto& e : rt.entityRuntimes) {
		if (e.type != "Switch")
			continue;

		bool hit = PointInAabb(playerPos, e.triggerAabb) || PointInAabb(ballPos, e.triggerAabb);
		if (!hit)
			continue;

		bool once = false;
		auto it = e.params.find("once");
		if (it != e.params.end()) {
			// "true"/"false" or "1"/"0" でもOKにする
			once = (it->second == "true" || it->second == "1");
		}

		if (once) {
			activated[e.id] = true;
		} else {
			// 踏んでる間ONにしたいならここ
			activated[e.id] = true;
		}
	}
}

void StageSystem::UpdateDoors(float dt) {
	for (auto& e : rt.entityRuntimes) {
		if (e.type != "Door")
			continue;

		bool shouldOpen = true;
		for (auto& linkId : e.links) {
			auto it = activated.find(linkId);
			if (it == activated.end() || it->second == false) {
				shouldOpen = false;
				break;
			}
		}

		float& t = doorOpenT[e.id];
		float speed = GetParamF(e.params, "speed", 1.5f); // 秒あたりの開き
		if (shouldOpen)
			t = std::min(1.0f, t + dt * speed);
		else
			t = std::max(0.0f, t - dt * speed);

		// ここであなたのDoorオブジェクトに反映したい場合は、
		// e.id で検索して Y を lerp するなどして接続する。
	}
}

void StageSystem::UpdateCamera(Vec3 playerPos) {
	// ベース設定
	CameraSettings base{};
	CameraSettings best = base;

	// 最初にヒットしたボリュームを採用（複数なら優先度など足す）
	for (auto& v : rt.cameraVolumes) {
		if (!PointInAabb(playerPos, v.aabb))
			continue;

		best.distance = GetParamF(v.params, "distance", base.distance);
		best.pitchDeg = GetParamF(v.params, "pitchDeg", base.pitchDeg);
		best.yawClampDeg = GetParamF(v.params, "yawClampDeg", base.yawClampDeg);
		best.upBlend = GetParamF(v.params, "upBlend", base.upBlend);
		break;
	}

	// ブレンドを丁寧にしたいなら dt で補間する
	cam = best;
}

bool StageSystem::IsGoalReached(Vec3 ballPos) const {
	for (auto& e : rt.entityRuntimes) {
		if (e.type != "Goal")
			continue;
		if (PointInAabb(ballPos, e.triggerAabb))
			return true;
	}
	return false;
}
