#include "PhaseSystemScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "Editor/EditorUI.h"
#include <imgui.h>
#include <cmath>
#include <iostream>

namespace Game {

	//bool PhaseSystemScript::isPreparation_ = false;

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(entity);
	(scene);
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(entity);
	(scene);
	(dt);
	bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;

	// ゲームビューの矩形情報を Editor 側から取得する
	ImVec2 gameImageMin = EditorUI::GetGameImageMin();
	ImVec2 gameImageMax = EditorUI::GetGameImageMax();
	float tW = gameImageMax.x - gameImageMin.x;
	float tH = gameImageMax.y - gameImageMin.y;

	if (isPreparation_) {
		Installation(scene, gameImageMin, tW, tH);
		
		if (keyP && !preKeyP_) {
			isPreparation_ = false;
		}

	} else {
		if (keyP && !preKeyP_) {
			isPreparation_ = true;
		}
	}
	preKeyP_ = keyP;
}

void PhaseSystemScript::Installation(GameScene* scene, const ImVec2& gameImageMin, float tW, float tH) {
	static bool placeMode = false;
	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
	bool mouseLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prevKey1 = false;
	static bool prevMouseLeft = false;

	if (!scene || !scene->GetRenderer()) {
		prevKey1 = key1;
		prevMouseLeft = mouseLeft;
		return;
	}

	if (key1 && !prevKey1) {
		placeMode = !placeMode;
	}

	Engine::Vector3 hitPoint = {0, 0, 0};
	bool hitTerrain = false;

	ImVec2 mousePos = ImGui::GetMousePos();
	float localX = mousePos.x - gameImageMin.x;
	float localY = mousePos.y - gameImageMin.y;
	bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

	if (insideImage) {
		auto& cam = scene->GetCamera();
		DirectX::XMVECTOR rayOrig, rayDir;
		EditorUI::ScreenToWorldRay(localX, localY, tW, tH, cam.View(), cam.Proj(), rayOrig, rayDir);

		float bestDist = FLT_MAX;
		auto& registry = scene->GetRegistry();
		auto view = registry.view<TransformComponent, NameComponent>();

		for (auto entity : view) {
			auto& nameComp = view.get<NameComponent>(entity);
			bool isTerrain = (nameComp.name.find("Terrain") != std::string::npos) || (nameComp.name.find("Floor") != std::string::npos);
			if (!isTerrain) continue;

			uint32_t modelHandle = 0;
			if (registry.all_of<GpuMeshColliderComponent>(entity)) {
				modelHandle = registry.get<GpuMeshColliderComponent>(entity).meshHandle;
			}
			if (modelHandle == 0 && registry.all_of<MeshRendererComponent>(entity)) {
				modelHandle = registry.get<MeshRendererComponent>(entity).modelHandle;
			}

			auto* model = scene->GetRenderer()->GetModel(modelHandle);
			if (model) {
				float d; Engine::Vector3 hp;
				Engine::Matrix4x4 worldMat = scene->GetWorldMatrix(static_cast<int>(entity));
				if (model->RayCast(rayOrig, rayDir, worldMat, d, hp)) {
					if (d < bestDist) {
						bestDist = d;
						hitPoint = hp;
						hitTerrain = true;
					}
				}
			}
		}
	}

	if (placeMode && hitTerrain && mouseLeft && !prevMouseLeft) {
		SpawnPlacedObject(scene, hitPoint, selectedObjPath_);
		placeMode = false;
	}

	prevKey1 = key1;
	prevMouseLeft = mouseLeft;
}

void PhaseSystemScript::OnDestroy(entt::entity entity, GameScene* scene) {
	(entity);
	(scene);
}

bool PhaseSystemScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
	(scene); (outHitPoint);
	return false;
}

void PhaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace) {
	(scene); (hitPoint); (objPath); (canPlace);
}

void PhaseSystemScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto entity = scene->CreateEntity("Placed Object");
	auto& registry = scene->GetRegistry();
	
	auto& transform = registry.get<TransformComponent>(entity);
	transform.translate = { hitPoint.x, hitPoint.y + 0.5f, hitPoint.z };

	auto& mr = registry.emplace<MeshRendererComponent>(entity);
	mr.modelPath = objPath;
	mr.texturePath = "Resources/white1x1.png";
	mr.modelHandle = scene->GetRenderer()->LoadObjMesh(mr.modelPath);
	mr.textureHandle = scene->GetRenderer()->LoadTexture2D(mr.texturePath);
	
	auto& hc = registry.emplace<HealthComponent>(entity);
	hc.hp = 100.0f;
	hc.maxHp = 100.0f;
}

bool PhaseSystemScript::IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const {
	(scene); (hitPoint);
	return false;
}

bool PhaseSystemScript::IsPrefabPath(const std::string& path) const {
	return path.find(".json") != std::string::npos;
}

bool PhaseSystemScript::ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const {
	(prefabPath); (outModelPath); (outTexturePath);
	return false;
}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game