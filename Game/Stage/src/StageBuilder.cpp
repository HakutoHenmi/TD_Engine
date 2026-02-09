// StageBuilder.cpp
#include "StageBuilder.h"
#include <cmath>

static inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float len(Vec3 a) { return std::sqrt(dot(a, a)); }
static inline Vec3 norm(Vec3 a) {
	float l = len(a);
	return (l > 1e-6f) ? mul(a, 1.0f / l) : V3(0, 0, 0);
}
static inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

static void make_basis(Vec3 forward, Vec3 up, Vec3& outX, Vec3& outY, Vec3& outZ) {
	outZ = norm(forward);
	outY = norm(up);
	outX = norm(cross(outY, outZ));
	outY = norm(cross(outZ, outX));
}

static OBB make_obb(Vec3 center, Vec3 axisX, Vec3 axisY, Vec3 axisZ, Vec3 half) {
	OBB b{};
	b.center = center;
	b.axisX = axisX;
	b.axisY = axisY;
	b.axisZ = axisZ;
	b.half = half;
	return b;
}

static AABB makeAabbFromCenter(Vec3 c, float r) { return {V3(c.x - r, c.y - r, c.z - r), V3(c.x + r, c.y + r, c.z + r)}; }

StageRuntime StageBuilder::Build(const StageData& data) {
	StageRuntime rt{};
	rt.cameraVolumes = data.cameraVolumes;

	auto getNodePos = [&](const std::string& id) -> Vec3 {
		auto it = data.nodeIndex.find(id);
		if (it == data.nodeIndex.end())
			return V3(0, 0, 0);
		return data.nodes[it->second].pos;
	};

	// ---- edges -> OBB list ----
	for (const auto& e : data.edges) {
		EdgeRuntime er{};
		er.edgeId = e.id;
		er.gravity = e.gravity;
		er.friction = e.friction;

		Vec3 p0 = getNodePos(e.fromId);
		Vec3 p1 = getNodePos(e.toId);
		Vec3 d = sub(p1, p0);
		float L = len(d);
		if (L < 0.01f)
			continue;

		const float segment = 2.0f; // ←調整ポイント
		int n = (int)std::ceil(L / segment);
		if (n < 1)
			n = 1;

		Vec3 up = V3(0, 1, 0);
		if (e.gravity.mode == GravityMode::Custom) {
			up = mul(norm(e.gravity.dir), -1.0f);
		}

		Vec3 axisX, axisY, axisZ;
		make_basis(d, up, axisX, axisY, axisZ);

		for (int i = 0; i < n; i++) {
			float t0 = (float)i / (float)n;
			float t1 = (float)(i + 1) / (float)n;
			Vec3 a = add(p0, mul(d, t0));
			Vec3 b = add(p0, mul(d, t1));
			Vec3 c = mul(add(a, b), 0.5f);

			float segLen = len(sub(b, a));

			// floor
			Vec3 half = V3(e.width * 0.5f, 0.25f, segLen * 0.5f);
			er.floorColliders.push_back(make_obb(c, axisX, axisY, axisZ, half));

			// guard rails
			if (e.guard) {
				Vec3 guardHalf = V3(0.15f, 1.0f, segLen * 0.5f);

				Vec3 leftC = add(c, mul(axisX, -(e.width * 0.5f + 0.15f)));
				Vec3 rightC = add(c, mul(axisX, +(e.width * 0.5f + 0.15f)));

				er.guardColliders.push_back(make_obb(leftC, axisX, axisY, axisZ, guardHalf));
				er.guardColliders.push_back(make_obb(rightC, axisX, axisY, axisZ, guardHalf));
			}
		}

		rt.edgeRuntimes.push_back(er);
	}

	// ---- entities ----
	for (const auto& en : data.entities) {
		EntityRuntime r{};
		r.id = en.id;
		r.type = en.type;
		r.pos = en.pos;
		r.links = en.links;
		r.params = en.params;

		// トリガーサイズ（typeで変えたいならここ）
		float rad = 1.5f;
		auto it = r.params.find("radius");
		if (it != r.params.end()) {
			try {
				rad = std::stof(it->second);
			} catch (...) {
			}
		}
		r.triggerAabb = makeAabbFromCenter(en.pos, rad);

		rt.entityRuntimes.push_back(r);
	}

	// lookup
	for (int i = 0; i < (int)rt.entityRuntimes.size(); ++i) {
		rt.entityIndex[rt.entityRuntimes[i].id] = i;
	}

	return rt;
}
