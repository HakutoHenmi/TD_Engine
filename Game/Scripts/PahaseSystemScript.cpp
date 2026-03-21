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

	if (isPreparation_) {
		Installation(scene);

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

void PahaseSystemScript::Installation(GameScene* scene) {
	auto* input = Engine::Input::GetInstance();

	// 左クリックが押された瞬間
	if (input->IsMouseTrigger(0)) {
       ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 gameMin = EditorUI::GetGameImageMin();
		ImVec2 gameMax = EditorUI::GetGameImageMax();
		float tW = gameMax.x - gameMin.x;
		float tH = gameMax.y - gameMin.y;
		if (tW <= 0.0f || tH <= 0.0f) return;

		float localX = mousePos.x - gameMin.x;
		float localY = mousePos.y - gameMin.y;
		bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
		if (!insideImage) return;

      auto& camera = scene->GetCamera();
		DirectX::XMMATRIX view = camera.View();
		DirectX::XMMATRIX proj = camera.Proj();

		DirectX::XMVECTOR rayOrig, rayDir;
      EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

		auto* renderer = scene->GetRenderer();
		if (!renderer) return;

		float bestDist = FLT_MAX;
		Engine::Vector3 hitPoint = {0, 0, 0};
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
				hitPoint = hp;
				hitTerrain = true;
			}
		}

		if (!hitTerrain) return;

		SceneObject newObj;
		newObj.name = "PlacedCube";
		newObj.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
		newObj.scale = {1.0f, 1.0f, 1.0f};
		newObj.color = {1.0f, 1.0f, 1.0f, 1.0f};
		newObj.modelPath = "Resources/cube/cube.obj";
		newObj.texturePath = "Resources/white1x1.png";
		newObj.modelHandle = renderer->LoadObjMesh(newObj.modelPath);
		newObj.textureHandle = renderer->LoadTexture2D(newObj.texturePath);

		MeshRendererComponent mr;
		mr.modelHandle = newObj.modelHandle;
		mr.textureHandle = newObj.textureHandle;
		mr.modelPath = newObj.modelPath;
		mr.texturePath = newObj.texturePath;
		mr.shaderName = "Toon";
		newObj.meshRenderers.push_back(mr);

		scene->SpawnObject(newObj);
	}
}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game