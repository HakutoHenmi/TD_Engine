#pragma once
#include "StageData.h"
#include <string>
#include <unordered_map>
#include <vector>

struct OBB {
	Vec3 center;
	Vec3 axisX; // normalized
	Vec3 axisY;
	Vec3 axisZ;
	Vec3 half; // half extents
};

struct EdgeRuntime {
	std::string edgeId;
	std::vector<OBB> floorColliders;
	std::vector<OBB> guardColliders;
	GravityInfo gravity;
	float friction = 0.9f;
};

struct EntityRuntime {
	std::string id;
	std::string type;

	Vec3 pos;
	AABB triggerAabb;

	std::vector<std::string> links;
	std::unordered_map<std::string, std::string> params;
};

struct StageRuntime {
	std::vector<EdgeRuntime> edgeRuntimes;
	std::vector<EntityRuntime> entityRuntimes;
	std::vector<CameraVolume> cameraVolumes;

	// lookup
	std::unordered_map<std::string, int> entityIndex;
};
