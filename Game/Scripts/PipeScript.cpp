#include "PipeScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

namespace Game {

static bool HasTag(GameScene* scene, entt::entity entity, TagType tagName) {
	if (!scene->GetRegistry().all_of<TagComponent>(entity)) return false;
	return scene->GetRegistry().get<TagComponent>(entity).tag == tagName;
}

static float DistanceBetween(GameScene* scene, entt::entity a, entt::entity b) {
	if (!scene->GetRegistry().all_of<TransformComponent>(a) || !scene->GetRegistry().all_of<TransformComponent>(b)) return 99999.0f;
	
	auto& aTc = scene->GetRegistry().get<TransformComponent>(a);
	auto& bTc = scene->GetRegistry().get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
   float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static bool IsConnectedSphere(GameScene* scene, entt::entity a, entt::entity b, float connectRange) {
	entt::registry& registry = scene->GetRegistry();

	if (!registry.all_of<TransformComponent>(a)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(b)) {
		return false;
	}

	TransformComponent& aTc = registry.get<TransformComponent>(a);
	TransformComponent& bTc = registry.get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
	float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

	float connectRangeSq = connectRange * connectRange;
	float distance3DSq = dx * dx + dy * dy + dz * dz;

	if (distance3DSq <= connectRangeSq) {
		return true;
	}

	float distanceXZSq = dx * dx + dz * dz;
	float heightDifference = std::abs(dy);

	if (heightDifference >= 0.1f) {
		if (distanceXZSq <= connectRangeSq) {
			return true;
		}
	}

	return false;
}
static bool IsAlreadyVisited(const std::vector<entt::entity>& visitedObjects, entt::entity obj) {
	for (size_t i = 0; i < visitedObjects.size(); ++i) {
		if (visitedObjects[i] == obj) {
			return true;
		}
	}
	return false;
}

static bool IsConnectedToBulletTankRecursive(GameScene* scene, entt::entity currentPipe, std::vector<entt::entity>& visitedObjects, float connectRange) {
	visitedObjects.push_back(currentPipe);

	// ★最適化: パイプとタンクのみを対象に走査 (高速タグ検索を利用)
	auto checkEntities = [&](const std::vector<entt::entity>& entities, TagType expectedTag) -> bool {
		for (auto other : entities) {
			if (other == currentPipe) continue;
			if (!scene->GetRegistry().valid(other)) continue;
			if (!IsConnectedSphere(scene, currentPipe, other, connectRange)) continue;

			if (expectedTag == TagType::BulletTank) {
				return true;
			}

			if (expectedTag == TagType::Pipe) {
				if (IsAlreadyVisited(visitedObjects, other)) continue;
				if (IsConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange)) {
					return true;
				}
			}
		}
		return false;
	};

	if (checkEntities(scene->GetEntitiesByTag(TagType::BulletTank), TagType::BulletTank)) return true;
	if (checkEntities(scene->GetEntitiesByTag(TagType::Pipe), TagType::Pipe)) return true;

	return false;
}

static bool IsConnectedToBulletTank(GameScene* scene, entt::entity selfObj) {
    const float connectRange = 2.5f;

	std::vector<entt::entity> visitedObjects;

	return IsConnectedToBulletTankRecursive(scene, selfObj, visitedObjects, connectRange);
}

// 接続対象となるタグかどうか判定
static bool IsConnectableTag(GameScene* scene, entt::entity entity) {
	return HasTag(scene, entity, TagType::Pipe) ||
	       HasTag(scene, entity, TagType::BulletTank) ||
	       HasTag(scene, entity, TagType::Cannon) ||
	       HasTag(scene, entity, TagType::Canon) ||
		HasTag(scene, entity, TagType::PipeCannon) ||
		   HasTag(scene, entity, TagType::Poison) ||
		   HasTag(scene, entity, TagType::Missile);
}

void PipeScript::Start(entt::entity obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void PipeScript::Update(entt::entity obj, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	float speed = rotationSpeed_;

	if (registry.all_of<TransformComponent>(obj)) {
		TransformComponent& pipeTransform = registry.get<TransformComponent>(obj);
		pipeTransform.rotate.z += speed * dt;
	}

	connectionCheckTimer_ += dt;

	if (connectionCheckTimer_ < connectionCheckInterval_) {
		return;
	}

	connectionCheckTimer_ = 0.0f;

	auto* renderer = scene->GetRenderer();
	if (!renderer) {
		return;
	}

	if (!registry.valid(obj)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(obj)) {
		return;
	}

	const float connectRange = 2.5f;

	currentConnections_.clear();
	currentConnections_.reserve(8);

	const TagType connectableTags[] = {
		TagType::Pipe, TagType::BulletTank, TagType::Cannon, 
		TagType::Canon, TagType::PipeCannon, TagType::Poison, TagType::Missile
	};

	for (TagType tag : connectableTags) {
		const auto& entities = scene->GetEntitiesByTag(tag);
		for (entt::entity other : entities) {
			if (other == obj) {
				continue;
			}

			if (!registry.valid(other)) {
				continue;
			}

			if (!IsConnectedSphere(scene, obj, other, connectRange)) {
				continue;
			}

			bool shouldCreate = false;

			if (tag == TagType::Pipe) {
				if (static_cast<uint32_t>(obj) < static_cast<uint32_t>(other)) {
					shouldCreate = true;
				}
			} else {
				shouldCreate = true;
			}

			if (shouldCreate) {
				currentConnections_.push_back(other);
			}
		}
	}

	for (auto it = connectionCylinders_.begin(); it != connectionCylinders_.end();) {
		entt::entity target = it->first;
		entt::entity cylinder = it->second;

		bool found = false;

		for (size_t i = 0; i < currentConnections_.size(); ++i) {
			if (currentConnections_[i] == target) {
				found = true;
				break;
			}
		}

		if (!found || !registry.valid(target)) {
			if (registry.valid(cylinder)) {
				scene->DestroyObject(static_cast<uint32_t>(cylinder));
			}

			it = connectionCylinders_.erase(it);
		} else {
			++it;
		}
	}

	TransformComponent& objTc = registry.get<TransformComponent>(obj);

	for (size_t i = 0; i < currentConnections_.size(); ++i) {
		entt::entity target = currentConnections_[i];

		if (connectionCylinders_.find(target) == connectionCylinders_.end()) {
			entt::entity cylinder = scene->CreateEntity("PipeConnection");

			MeshRendererComponent& mesh = registry.emplace<MeshRendererComponent>(cylinder);
			mesh.modelPath = "Resources/Models/Cylinder/cylinder.obj";
			mesh.modelHandle = renderer->LoadObjMesh(mesh.modelPath);
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.textureHandle = renderer->LoadTexture2D(mesh.texturePath);
			mesh.shaderName = "Toon";

			if (registry.all_of<MeshRendererComponent>(obj)) {
				MeshRendererComponent& pipeMesh = registry.get<MeshRendererComponent>(obj);
				mesh.color = pipeMesh.color;
			} else {
				mesh.color = {0.75f, 0.75f, 0.75f, 1.0f};
			}

			scene->SetTag(cylinder, TagType::Default);
			connectionCylinders_[target] = cylinder;
		}

		entt::entity cylinder = connectionCylinders_[target];

		if (!registry.valid(cylinder)) {
			continue;
		}

		if (!registry.valid(target)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(cylinder)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(target)) {
			continue;
		}

		TransformComponent& cylTc = registry.get<TransformComponent>(cylinder);
		TransformComponent& tgtTc = registry.get<TransformComponent>(target);

		Engine::Vector3 startPos = {objTc.translate.x, objTc.translate.y, objTc.translate.z};

		Engine::Vector3 endPos = {tgtTc.translate.x, tgtTc.translate.y, tgtTc.translate.z};

		Engine::Vector3 diff = endPos - startPos;

		float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

		if (distSq <= 0.0001f) {
			continue;
		}

		float dist = std::sqrt(distSq);
		Engine::Vector3 dir = diff * (1.0f / dist);

		float cyLen = (dist - 0.2f) / 3.0f;

		if (cyLen < 0.01f) {
			cyLen = 0.01f;
		}

		Engine::Vector3 center = startPos + diff * 0.5f;

		cylTc.translate = {center.x, center.y, center.z};
		cylTc.scale = {0.35f, cyLen, 0.35f};

		Engine::Vector3 euler = Engine::LookRotation(dir);
		cylTc.rotate = {euler.x - 3.14159265f * 0.5f, euler.y, euler.z};
	}
}

void PipeScript::OnDestroy(entt::entity obj, GameScene* scene) {
	(void)obj;
	auto& registry = scene->GetRegistry();
	for (auto& pair : connectionCylinders_) {
		if (registry.valid(pair.second)) {
			registry.destroy(pair.second);
		}
	}
	connectionCylinders_.clear();
}

REGISTER_SCRIPT(PipeScript);

} // namespace Game