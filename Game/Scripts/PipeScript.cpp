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
	if (!scene->GetRegistry().all_of<TransformComponent>(a)) {
		return false;
	}

	if (!scene->GetRegistry().all_of<TransformComponent>(b)) {
		return false;
	}

	auto& aTc = scene->GetRegistry().get<TransformComponent>(a);
	auto& bTc = scene->GetRegistry().get<TransformComponent>(b);

	float dx = bTc.translate.x - aTc.translate.x;
	float dy = bTc.translate.y - aTc.translate.y;
	float dz = bTc.translate.z - aTc.translate.z;

	float distance3D = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (distance3D <= connectRange) {
		return true;
	}

	float distanceXZ = std::sqrt(dx * dx + dz * dz);
	float heightDifference = std::abs(dy);

	// 高低差があるときだけ、横方向の距離で接続を許可する
	if (heightDifference >= 0.1f) {
		if (distanceXZ <= connectRange) {
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

	auto view = scene->GetRegistry().view<TransformComponent>();
	for (auto other : view) {

		if (other == currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(scene, currentPipe, other, connectRange)) {
			continue;
		}

		// 隣に弾倉があれば到達成功
		if (HasTag(scene, other, TagType::BulletTank)) {
			return true;
		}

		// 隣がパイプならさらに先を調べる
		if (HasTag(scene, other, TagType::Pipe)) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

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
	float speed = rotationSpeed_;

	if (scene->GetRegistry().all_of<TransformComponent>(obj)) {
		scene->GetRegistry().get<TransformComponent>(obj).rotate.z += speed * dt;
	}

	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

	// 接続判定と円柱の生成・更新
    const float connectRange = 2.5f;
	std::vector<entt::entity> currentConnections;

	auto view = scene->GetRegistry().view<TransformComponent>();
	for (auto other : view) {
		if (other == obj) continue;
		
		// 接続対象のタグチェック
		if (!IsConnectableTag(scene, other)) continue;

		if (!IsConnectedSphere(scene, obj, other, connectRange)) continue;

		bool shouldCreate = false;
		if (HasTag(scene, other, TagType::Pipe)) {
			// Pipe同士ならIDが小さい方がCylinderを作る（重複防止）
			if (static_cast<uint32_t>(obj) < static_cast<uint32_t>(other)) {
				shouldCreate = true;
			}
		} else {
			// Pipe以外（Tank, Cannon等）ならPipeが作る
			shouldCreate = true;
		}

		if (shouldCreate) {
			currentConnections.push_back(other);
		}
	}

	// 接続が切れたものを削除
	for (auto it = connectionCylinders_.begin(); it != connectionCylinders_.end(); ) {
		auto target = it->first;
		auto cylinder = it->second;
		
		bool found = false;
		for (auto c : currentConnections) {
			if (c == target) {
				found = true;
				break;
			}
		}
		
		if (!found || !scene->GetRegistry().valid(target)) {
			if (scene->GetRegistry().valid(cylinder)) {
				scene->DestroyObject(static_cast<uint32_t>(cylinder));
			}
			it = connectionCylinders_.erase(it);
		} else {
			++it;
		}
	}

	// 新規接続の円柱生成 + 既存の円柱Transform更新
	for (auto target : currentConnections) {
		if (connectionCylinders_.find(target) == connectionCylinders_.end()) {
			// 新規円柱生成
			entt::entity cylinder = scene->CreateEntity("PipeConnection");
			auto& mesh = scene->GetRegistry().emplace<MeshRendererComponent>(cylinder);
			mesh.modelPath = "Resources/Models/Cylinder/cylinder.obj";
			mesh.modelHandle = renderer->LoadObjMesh(mesh.modelPath);
			mesh.texturePath = "Resources/Textures/white1x1.png";
			mesh.textureHandle = renderer->LoadTexture2D(mesh.texturePath);
			mesh.shaderName = "Toon";
			
			// 色をPipe自身に合わせる
			if (scene->GetRegistry().all_of<MeshRendererComponent>(obj)) {
				auto& pipeMesh = scene->GetRegistry().get<MeshRendererComponent>(obj);
				mesh.color = pipeMesh.color;
			} else {
				mesh.color = {0.75f, 0.75f, 0.75f, 1.0f};
			}
			
			scene->SetTag(cylinder, TagType::Default);
			connectionCylinders_[target] = cylinder;
		}

		// Transform更新（PipeEditorのOnGeneratePipeと同じロジック）
		entt::entity cylinder = connectionCylinders_[target];
		if (!scene->GetRegistry().valid(cylinder)) continue;
		if (!scene->GetRegistry().all_of<TransformComponent>(cylinder)) continue;
		if (!scene->GetRegistry().all_of<TransformComponent>(target)) continue;
		if (!scene->GetRegistry().all_of<TransformComponent>(obj)) continue;

		auto& cylTc = scene->GetRegistry().get<TransformComponent>(cylinder);
		auto& objTc = scene->GetRegistry().get<TransformComponent>(obj);
		auto& tgtTc = scene->GetRegistry().get<TransformComponent>(target);

		Engine::Vector3 startPos = {objTc.translate.x, objTc.translate.y, objTc.translate.z};
		Engine::Vector3 endPos = {tgtTc.translate.x, tgtTc.translate.y, tgtTc.translate.z};
		Engine::Vector3 diff = endPos - startPos;
		Engine::Vector3 dir = Engine::Normalize(diff);
		float dist = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);

		// シリンダーの高さは3.0（OBJモデルの仕様）なのでそれに合わせてスケール
		float cyLen = (dist - 0.2f) / 3.0f;  // 球体にめり込ませるため少し短く
		if (cyLen < 0.01f) cyLen = 0.01f;

		// 中間地点に配置
		Engine::Vector3 center = startPos + diff * 0.5f;
		cylTc.translate = {center.x, center.y, center.z};
		cylTc.scale = {0.35f, cyLen, 0.35f};

		// PipeEditorと同じ回転計算
		auto euler = Engine::LookRotation(dir);
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