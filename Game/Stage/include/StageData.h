#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct Vec3 {
	float x, y, z;
};
struct AABB {
	Vec3 minv, maxv;
};
static inline Vec3 V3(float x, float y, float z) { return {x, y, z}; }

struct StageMeta {
	std::string name;
	float targetTimeSec = 60.0f;
};

struct SpawnInfo {
	Vec3 playerPos = V3(0, 0, 0);
	Vec3 ballPos = V3(0, 0, 0);
	float ballRadius = 0.6f;
};

enum class GravityMode { World, Custom };

struct GravityInfo {
	GravityMode mode = GravityMode::World;
	Vec3 dir = V3(0, -1, 0);
};

struct RouteNode {
	std::string id;
	Vec3 pos;
};

struct RouteEdge {
	std::string id;
	std::string fromId;
	std::string toId;

	float width = 3.0f;
	bool guard = false;
	float friction = 0.9f;
	GravityInfo gravity;
};

struct Entity {
	std::string id;
	std::string type;
	Vec3 pos;
	Vec3 rot = V3(0, 0, 0);
	Vec3 scale = V3(1, 1, 1);

	std::unordered_map<std::string, std::string> params;
	std::vector<std::string> links;
};

struct CameraVolume {
	std::string id;
	AABB aabb;
	std::unordered_map<std::string, std::string> params;
};

struct StageData {
	StageMeta meta;
	SpawnInfo spawn;

	std::vector<RouteNode> nodes;
	std::vector<RouteEdge> edges;

	std::vector<Entity> entities;
	std::vector<CameraVolume> cameraVolumes;

	// id->index
	std::unordered_map<std::string, int> nodeIndex;
};
