#include "PahaseSystemScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "Editor/EditorUI.h"
#include <imgui.h>
#include <cmath>
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

   ImVec2 gameImageMin = ImVec2(0.0f, 0.0f);
	float tW = 1280.0f;
	float tH = 720.0f;
	EditorUI::GetGameViewRect(gameImageMin, tW, tH);

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

void PahaseSystemScript::Installation(GameScene* scene, const ImVec2& gameImageMin, float tW, float tH) {
    static bool prevMouseLeft = false;
	static bool placeMode = false;

	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
	bool mouseLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	bool mouseLeftPressed = mouseLeft && !prevMouseLeft;
	static bool prevKey1 = false;

	if (key1 && !prevKey1) {
		placeMode = true;
	}

	if (!scene || !scene->GetRenderer()) {
		prevMouseLeft = mouseLeft;
		return;
	}

	auto* renderer = scene->GetRenderer();

	ImVec2 mousePos = ImGui::GetMousePos();
	float localX = mousePos.x - gameImageMin.x;
	float localY = mousePos.y - gameImageMin.y;
	bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

	Engine::Vector3 hitPoint = { 0, 0, 0 };
	bool hitTerrain = false;

	if (insideImage) {
		auto& cam = scene->GetCamera();
		auto viewMat = cam.View();
		auto projMat = cam.Proj();
		DirectX::XMVECTOR rayOrig, rayDir;
		EditorUI::ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

		float bestDist = FLT_MAX;
		auto objects = scene->GetObjects();
		for (auto& obj : objects) {
			bool isTerrain = (obj.name.find("Terrain") != std::string::npos) || (obj.name.find("Floor") != std::string::npos);
			if (!isTerrain) continue;

			Engine::Model* model = nullptr;
			if (!obj.gpuMeshColliders.empty()) {
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
				hitPoint = hp;
				hitTerrain = true;
			}
		}
	}

   if (placeMode && mouseLeftPressed && hitTerrain) {
		SceneObject obj;
		obj.name = "New Object";
		obj.modelPath = "Resources/cube/cube.obj";
		obj.texturePath = "Resources/white1x1.png";
		obj.translate = { hitPoint.x, hitPoint.y + 0.5f, hitPoint.z };

		HealthComponent health;
		health.hp = 100.0f;
		health.maxHp = 100.0f;
		obj.healths.push_back(health);

		MeshRendererComponent mr;
		mr.modelPath = obj.modelPath;
		mr.texturePath = obj.texturePath;
		mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
		mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
		obj.meshRenderers.push_back(mr);

		scene->SpawnObject(obj);
	}

  prevKey1 = key1;
	prevMouseLeft = mouseLeft;
}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game