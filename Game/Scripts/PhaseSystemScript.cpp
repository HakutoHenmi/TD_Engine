#include "PhaseSystemScript.h"
#include "Editor/EditorUI.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
#include <iostream>

namespace Game {

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	(void)scene;
	(void)dt;
	auto* input = Engine::Input::GetInstance();
	bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;
	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
	bool key2 = (GetAsyncKeyState('2') & 0x8000) != 0;
	bool key3 = (GetAsyncKeyState('3') & 0x8000) != 0;

	if (isPreparation_) {
		if (key1 && !preKey1_) {
			selectedObjPath_ = "Resources/BulletTank.prefab";
			isPlacementMode_ = true;
		}

		if (key2 && !preKey2_) {
			selectedObjPath_ = "Resources/Pipe.prefab";
			isPlacementMode_ = true;
		}

		if (key3 && !preKey3_) {
			selectedObjPath_ = "Resources/Canon.prefab";
			isPlacementMode_ = true;
		}
		if (input->IsMouseTrigger(1) && isPlacementMode_) {
			isPlacementMode_ = false;
		}

		Installation(scene, selectedObjPath_);

		if (keyP && !preKeyP_) {
			isPreparation_ = false;
			isPlacementMode_ = false;
		}

	} else {
		isPlacementMode_ = false;
		if (keyP && !preKeyP_) {
			isPreparation_ = true;
		}
	}
	preKeyP_ = keyP;
	preKey1_ = key1;
	preKey2_ = key2;
	preKey3_ = key3;
}

void PhaseSystemScript::Installation(GameScene* scene, const std::string& objPath) {
	if (!isPlacementMode_)
		return;

	auto* input = Engine::Input::GetInstance();
	Engine::Vector3 hitPoint{};
	if (!TryGetTerrainHitPoint(scene, hitPoint))
		return;

	Engine::Vector3 snappedHitPoint = hitPoint;
	snappedHitPoint.x = std::floor(snappedHitPoint.x);
	snappedHitPoint.z = std::floor(snappedHitPoint.z);

	const bool canPlace = !IsPlacementBlocked(scene, snappedHitPoint);

   DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

	if (input->IsMouseTrigger(0) && canPlace) {
        SpawnPlacedObject(scene, snappedHitPoint, objPath);
		isPlacementMode_ = false;
	}
}

bool PhaseSystemScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
	float localX = 0, localY = 0;
	float tW = 0, tH = 0;

#ifdef USE_IMGUI
	ImVec2 mousePos = ImGui::GetMousePos();
	ImVec2 gameMin = EditorUI::GetGameImageMin();
	ImVec2 gameMax = EditorUI::GetGameImageMax();
	tW = gameMax.x - gameMin.x;
	tH = gameMax.y - gameMin.y;
	if (tW <= 0.0f || tH <= 0.0f)
		return false;

	localX = mousePos.x - gameMin.x;
	localY = mousePos.y - gameMin.y;
	bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
	if (!insideImage)
		return false;
#else
    // リリース時は画面中央や、ネイティブなマウス座標を使う必要があるが、
    // 基本的にプレイ中にこの配置モードに入らない想定なら false を返す
    return false;
#endif

	auto& camera = scene->GetCamera();
	DirectX::XMMATRIX view = camera.View();
	DirectX::XMMATRIX proj = camera.Proj();

	DirectX::XMVECTOR rayOrig, rayDir;
	EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return false;

	float bestDist = FLT_MAX;
	bool hitTerrain = false;

	auto& registry = scene->GetRegistry();
	auto terrainView = registry.view<NameComponent, TransformComponent>();
	
	for (auto entity : terrainView) {
		const auto& nc = terrainView.get<NameComponent>(entity);
		const auto& tc = terrainView.get<TransformComponent>(entity);

		bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos);
		if (!isTerrain)
			continue;

		Engine::Model* model = nullptr;
		// GpuMeshCollider か MeshRenderer からモデルを取得
		if (registry.all_of<GpuMeshColliderComponent>(entity)) {
			auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
			if (gmc.meshHandle != 0) {
				model = renderer->GetModel(gmc.meshHandle);
			}
		}
		
		if (!model && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			if (mr.modelHandle != 0) {
				model = renderer->GetModel(mr.modelHandle);
			}
		}

		if (!model)
			continue;

		float d;
		Engine::Vector3 hp;
		if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
			bestDist = d;
			outHitPoint = hp;
			hitTerrain = true;
		}
	}

	return hitTerrain;
}

void PhaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	std::string previewModelPath = objPath;
	std::string previewTexturePath = "Resources/white1x1.png";
	if (IsPrefabPath(objPath)) {
		ExtractPrefabRenderPaths(objPath, previewModelPath, previewTexturePath);
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != previewModelPath) {
		previewModelHandle_ = renderer->LoadObjMesh(previewModelPath);
		previewObjPath_ = previewModelPath;
		previewTextureHandle_ = 0;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D(previewTexturePath);
	}

	Engine::Transform tr;
	tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tr.scale = {1.0f, 1.0f, 1.0f};
	const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");
}

bool PhaseSystemScript::IsPrefabPath(const std::string& path) const {
	if (path.size() < 7)
		return false;
	return path.compare(path.size() - 7, 7, ".prefab") == 0;
}

bool PhaseSystemScript::ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const {
	std::ifstream f(prefabPath);
	if (!f.is_open())
		return false;

	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto extractValue = [&](const char* key, std::string& outValue) {
		size_t keyPos = content.find(key);
		if (keyPos == std::string::npos)
			return;
		size_t colonPos = content.find(':', keyPos);
		if (colonPos == std::string::npos)
			return;
		size_t firstQuote = content.find('"', colonPos);
		if (firstQuote == std::string::npos)
			return;
		size_t secondQuote = content.find('"', firstQuote + 1);
		if (secondQuote == std::string::npos)
			return;
		outValue = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
	};

	extractValue("\"modelPath\"", outModelPath);
	extractValue("\"texturePath\"", outTexturePath);

	return !outModelPath.empty();
}

bool PhaseSystemScript::IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const {
	constexpr float kBlockRadius = 1.0f;
	constexpr float kBlockRadiusSq = kBlockRadius * kBlockRadius;

	auto& registry = scene->GetRegistry();
	auto view = registry.view<TransformComponent>();
	for (auto entity : view) {
		// Terrain や Floor は除外したいが、TransformComponent だけでは判定できない
		// 名前が必要
		if (registry.all_of<NameComponent>(entity)) {
			const auto& nc = registry.get<NameComponent>(entity);
			const bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos);
			if (isTerrain)
				continue;
		}

		const auto& tc = view.get<TransformComponent>(entity);
		const float dx = tc.translate.x - hitPoint.x;
		const float dz = tc.translate.z - hitPoint.z;
		const float distSq = dx * dx + dz * dz;
		if (distSq < kBlockRadiusSq) {
			return true;
		}
	}

	return false;
}

void PhaseSystemScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	auto& registry = scene->GetRegistry();

	if (IsPrefabPath(objPath)) {
		// 現在のエンティティ一覧を記録
		std::vector<entt::entity> beforeEntities;
		for (auto entity : registry.storage<entt::entity>()) {
			beforeEntities.push_back(entity);
		}

		EditorUI::LoadPrefab(scene, objPath);

		// 新しく追加されたエンティティを見つける
		for (auto entity : registry.storage<entt::entity>()) {
			bool found = false;
			for(auto b : beforeEntities) { if(b == entity) { found = true; break; } }
			if (!found) {
				// 新しいエンティティの座標をセット
				if (registry.all_of<TransformComponent>(entity)) {
					auto& tc = registry.get<TransformComponent>(entity);
					// 親がいない（ルート）のエンティティのみ座標を更新
					if (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null) {
						tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
					}
				}
			}
		}
		return;
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
		previewModelHandle_ = renderer->LoadObjMesh(objPath);
		previewObjPath_ = objPath;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D("Resources/white1x1.png");
	}

	entt::entity newEntity = scene->CreateEntity((objPath.find("cylinder") != std::string::npos || objPath.find("Cylinder") != std::string::npos) ? "PlacedCylinder" : "PlacedCube");
	
	auto& tc = registry.get<TransformComponent>(newEntity);
	tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tc.scale = {1.0f, 1.0f, 1.0f};

	auto& mr = registry.emplace<MeshRendererComponent>(newEntity);
	mr.modelHandle = previewModelHandle_;
	mr.textureHandle = previewTextureHandle_;
	mr.modelPath = objPath;
	mr.texturePath = "Resources/white1x1.png";
	mr.shaderName = "Toon";
}

void PhaseSystemScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game