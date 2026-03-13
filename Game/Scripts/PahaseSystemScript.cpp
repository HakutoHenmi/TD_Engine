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

	// ゲームビューの矩形情報を Editor 側から取得する想定で、ここでは 0,0/1280x720 を仮で渡す
	ImVec2 gameImageMin = ImVec2(0.0f, 0.0f);
	float tW = 1280.0f;
	float tH = 720.0f;

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
	static bool preKey1 = false;
	static bool placeMode = false; // 1 押下後にクリック待ちするモード

	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
	bool mouseLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prevMouseLeft = false;

	if (!scene || !scene->GetRenderer()) {
		preKey1 = key1;
		prevMouseLeft = mouseLeft;
		return;
	}

	// 1 キー押下で「次のクリックで設置」モード ON
	if (key1 && !preKey1) {
		placeMode = true;
	}

	auto* renderer = scene->GetRenderer();

	// Editor 上と同じように ImGui のゲームビュー座標からレイを飛ばす
	ImVec2 mousePos = ImGui::GetMousePos();
	float localX = mousePos.x - gameImageMin.x;
	float localY = mousePos.y - gameImageMin.y;
	bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

	Engine::Vector3 hitPoint = {0, 0, 0};
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
			if (model) {
				float d; Engine::Vector3 hp;
				if (model->RayCast(rayOrig, rayDir, obj.GetTransform().ToMatrix(), d, hp)) {
					if (d < bestDist) {
						bestDist = d;
						hitPoint = hp;
						hitTerrain = true;
					}
				}
			}
		}
	}

	// placeMode 中に、左クリックの立ち上がり かつ 地面ヒット で設置
	if (placeMode && hitTerrain && mouseLeft && !prevMouseLeft) {
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

		// 一度設置したらモード解除
		placeMode = false;
	}

	preKey1 = key1;
	prevMouseLeft = mouseLeft;
}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game