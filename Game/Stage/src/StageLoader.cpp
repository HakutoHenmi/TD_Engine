// StageLoader.cpp
#include "StageLoader.h"
#include <fstream>
#include <sstream>

// nlohmann/json
#include < nlohmann/json.hpp>
using json = nlohmann::json;

static Vec3 readVec3(const json& a) { return V3(a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()); }

static std::string getStr(const json& obj, const char* key, const char* defv = "") {
	if (!obj.contains(key))
		return defv;
	return obj.at(key).get<std::string>();
}

static float getF(const json& obj, const char* key, float defv) {
	if (!obj.contains(key))
		return defv;
	return obj.at(key).get<float>();
}

static bool getB(const json& obj, const char* key, bool defv) {
	if (!obj.contains(key))
		return defv;
	return obj.at(key).get<bool>();
}

bool StageLoader::LoadFromFile(const std::string& path, StageData& out) {
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return false;

	json root;
	try {
		ifs >> root;
	} catch (...) {
		return false;
	}

	// meta
	if (root.contains("meta")) {
		auto m = root["meta"];
		out.meta.name = getStr(m, "name", "");
		out.meta.targetTimeSec = getF(m, "targetTimeSec", 60.0f);
	}

	// spawn
	if (root.contains("spawn")) {
		auto sp = root["spawn"];
		if (sp.contains("player"))
			out.spawn.playerPos = readVec3(sp["player"]["pos"]);
		if (sp.contains("ball")) {
			out.spawn.ballPos = readVec3(sp["ball"]["pos"]);
			if (sp["ball"].contains("radius"))
				out.spawn.ballRadius = sp["ball"]["radius"].get<float>();
		}
	}

	// route
	out.nodes.clear();
	out.edges.clear();
	out.nodeIndex.clear();
	if (root.contains("route")) {
		auto r = root["route"];

		if (r.contains("nodes")) {
			for (auto& n : r["nodes"]) {
				RouteNode node{};
				node.id = n["id"].get<std::string>();
				node.pos = readVec3(n["pos"]);
				out.nodeIndex[node.id] = (int)out.nodes.size();
				out.nodes.push_back(node);
			}
		}

		if (r.contains("edges")) {
			for (auto& e : r["edges"]) {
				RouteEdge edge{};
				edge.id = e["id"].get<std::string>();
				edge.fromId = e["from"].get<std::string>();
				edge.toId = e["to"].get<std::string>();
				edge.width = getF(e, "width", 3.0f);
				edge.guard = getB(e, "guard", false);
				edge.friction = getF(e, "friction", 0.9f);

				// gravity
				if (e.contains("gravity")) {
					auto g = e["gravity"];
					std::string mode = getStr(g, "mode", "world");
					if (mode == "custom")
						edge.gravity.mode = GravityMode::Custom;
					else
						edge.gravity.mode = GravityMode::World;

					if (g.contains("dir"))
						edge.gravity.dir = readVec3(g["dir"]);
				}

				out.edges.push_back(edge);
			}
		}
	}

	// entities
	out.entities.clear();
	if (root.contains("entities")) {
		for (auto& en : root["entities"]) {
			Entity e{};
			e.id = en["id"].get<std::string>();
			e.type = en["type"].get<std::string>();
			e.pos = readVec3(en["pos"]);

			if (en.contains("rot"))
				e.rot = readVec3(en["rot"]);
			if (en.contains("scale"))
				e.scale = readVec3(en["scale"]);

			if (en.contains("params")) {
				for (auto it = en["params"].begin(); it != en["params"].end(); ++it) {
					// 数値でも文字列化して保持（統一）
					if (it.value().is_string())
						e.params[it.key()] = it.value().get<std::string>();
					else
						e.params[it.key()] = it.value().dump();
				}
			}
			if (en.contains("links")) {
				for (auto& l : en["links"])
					e.links.push_back(l.get<std::string>());
			}
			out.entities.push_back(e);
		}
	}

	// camera volumes
	out.cameraVolumes.clear();
	if (root.contains("camera") && root["camera"].contains("volumes")) {
		for (auto& cv : root["camera"]["volumes"]) {
			CameraVolume v{};
			v.id = cv["id"].get<std::string>();
			v.aabb.minv = readVec3(cv["aabbMin"]);
			v.aabb.maxv = readVec3(cv["aabbMax"]);

			if (cv.contains("params")) {
				for (auto it = cv["params"].begin(); it != cv["params"].end(); ++it) {
					if (it.value().is_string())
						v.params[it.key()] = it.value().get<std::string>();
					else
						v.params[it.key()] = it.value().dump();
				}
			}
			out.cameraVolumes.push_back(v);
		}
	}

	return true;
}
