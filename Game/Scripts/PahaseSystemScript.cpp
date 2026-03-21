#include "PahaseSystemScript.h"
#include "Editor/EditorUI.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <iostream>

namespace Game {

bool PahaseSystemScript::isPreparation_ = false;

void PahaseSystemScript::Start(SceneObject& obj, GameScene* scene) {
	(obj);
	(scene);
}

void PahaseSystemScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	(obj);
	(scene);
	(dt);
	bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;
	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
	bool key2 = (GetAsyncKeyState('2') & 0x8000) != 0;

	if (isPreparation_) {
        if (key1 && !preKey1_) {
			selectedObjPath_ = "Resources/cube/cube.obj";
			isPlacementMode_ = true;
		}

		if (key2 && !preKey2_) {
			selectedObjPath_ = "Resources/Cylinder/cylinder.obj";
			isPlacementMode_ = true;
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
}

void PahaseSystemScript::Installation(GameScene* scene, const std::string& objPath) {
	if (!isPlacementMode_) return;

	auto* input = Engine::Input::GetInstance();
	Engine::Vector3 hitPoint{};
	if (!TryGetTerrainHitPoint(scene, hitPoint)) return;

  DrawPlacementPreview(scene, hitPoint, objPath);

	if (input->IsMouseTrigger(0)) {
       SpawnPlacedObject(scene, hitPoint, objPath);
		isPlacementMode_ = false;
	}
}

bool PahaseSystemScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
	ImVec2 mousePos = ImGui::GetMousePos();
	ImVec2 gameMin = EditorUI::GetGameImageMin();
	ImVec2 gameMax = EditorUI::GetGameImageMax();
	float tW = gameMax.x - gameMin.x;
	float tH = gameMax.y - gameMin.y;
	if (tW <= 0.0f || tH <= 0.0f) return false;

	float localX = mousePos.x - gameMin.x;
	float localY = mousePos.y - gameMin.y;
	bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
	if (!insideImage) return false;

	auto& camera = scene->GetCamera();
	DirectX::XMMATRIX view = camera.View();
	DirectX::XMMATRIX proj = camera.Proj();

	DirectX::XMVECTOR rayOrig, rayDir;
	EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

	auto* renderer = scene->GetRenderer();
	if (!renderer) return false;

	float bestDist = FLT_MAX;
	bool hitTerrain = false;

	const auto& objects = scene->GetObjects();
	for (const auto& obj : objects) {
		bool isTerrain = (obj.name.find("Terrain") != std::string::npos) || (obj.name.find("Floor") != std::string::npos);
		if (!isTerrain) continue;

		Engine::Model* model = nullptr;
		if (!obj.gpuMeshColliders.empty() && obj.gpuMeshColliders[0].meshHandle != 0) {
			model = renderer->GetModel(obj.gpuMeshColliders[0].meshHandle);
		}
		if (!model && obj.modelHandle != 0) {
			model = renderer->GetModel(obj.modelHandle);
		}
		if (!model && !obj.meshRenderers.empty() && obj.meshRenderers[0].modelHandle != 0) {
			model = renderer->GetModel(obj.meshRenderers[0].modelHandle);
		}

		if (!model) continue;

		float d;
		Engine::Vector3 hp;
		if (model->RayCast(rayOrig, rayDir, obj.GetTransform().ToMatrix(), d, hp) && d < bestDist) {
			bestDist = d;
			outHitPoint = hp;
			hitTerrain = true;
		}
	}

	return hitTerrain;
}

void PahaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

 if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
		previewModelHandle_ = renderer->LoadObjMesh(objPath);
		previewObjPath_ = objPath;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D("Resources/white1x1.png");
	}

	Engine::Transform tr;
	tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tr.scale = {1.0f, 1.0f, 1.0f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, {0.6f, 1.0f, 0.6f, 0.6f}, "Toon");
}

void PahaseSystemScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

 if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
		previewModelHandle_ = renderer->LoadObjMesh(objPath);
		previewObjPath_ = objPath;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D("Resources/white1x1.png");
	}

	SceneObject newObj;
 newObj.name = (objPath.find("cylinder") != std::string::npos || objPath.find("Cylinder") != std::string::npos) ? "PlacedCylinder" : "PlacedCube";
	newObj.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	newObj.scale = {1.0f, 1.0f, 1.0f};
	newObj.color = {1.0f, 1.0f, 1.0f, 1.0f};
   newObj.modelPath = objPath;
	newObj.texturePath = "Resources/white1x1.png";
	newObj.modelHandle = previewModelHandle_;
	newObj.textureHandle = previewTextureHandle_;

	MeshRendererComponent mr;
	mr.modelHandle = newObj.modelHandle;
	mr.textureHandle = newObj.textureHandle;
	mr.modelPath = newObj.modelPath;
	mr.texturePath = newObj.texturePath;
	mr.shaderName = "Toon";
	newObj.meshRenderers.push_back(mr);

	scene->SpawnObject(newObj);
}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game