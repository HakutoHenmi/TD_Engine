#include "PhaseSystemScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "Editor/EditorUI.h"
#include <imgui.h>
#include <cmath>
#include <iostream>

namespace Game {

	bool PhaseSystemScript::isPreparation_ = false;

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(entity);
	(scene);
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(entity);
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

void PhaseSystemScript::Installation(GameScene* scene, const ImVec2& gameImageMin, float tW, float tH) {
	static bool preKey1 = false;
	static bool placeMode = false; // 1 押下後にクリック待ちするモード
	(gameImageMin);

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
	float localX = mousePos.x;
	float localY = mousePos.y;
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

		auto view = scene->GetRegistry().view<NameComponent, TransformComponent>();
		for (auto e : view) {
			const std::string& name = scene->GetRegistry().get<NameComponent>(e).name;
			bool isTerrain = (name.find("Terrain") != std::string::npos) || (name.find("Floor") != std::string::npos);
			if (!isTerrain) continue;

			Engine::Model* model = nullptr;
			if (scene->GetRegistry().all_of<GpuMeshColliderComponent>(e)) {
				model = renderer->GetModel(scene->GetRegistry().get<GpuMeshColliderComponent>(e).meshHandle);
			}
			if (!model && scene->GetRegistry().all_of<MeshRendererComponent>(e)) {
				model = renderer->GetModel(scene->GetRegistry().get<MeshRendererComponent>(e).modelHandle);
			}
			if (model) {
				float d; Engine::Vector3 hp;
				if (model->RayCast(rayOrig, rayDir, scene->GetRegistry().get<TransformComponent>(e).ToMatrix(), d, hp)) {
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
		entt::entity newObj = scene->GetRegistry().create();
		
		scene->GetRegistry().emplace<NameComponent>(newObj).name = "New Object";
		auto& tc = scene->GetRegistry().emplace<TransformComponent>(newObj);
		tc.translate = { hitPoint.x, hitPoint.y + 0.5f, hitPoint.z };

		auto& health = scene->GetRegistry().emplace<HealthComponent>(newObj);
		health.hp = 100.0f;
		health.maxHp = 100.0f;

		auto& mr = scene->GetRegistry().emplace<MeshRendererComponent>(newObj);
		mr.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		mr.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
		mr.color = { 1.0f, 1.0f, 1.0f, 1.0f };

		// TODO: obj.modelPath, obj.texturePath properties were used before... maybe consider string components if necessary.

		// 一度設置したらモード解除
		placeMode = false;
	}

	preKey1 = key1;
	prevMouseLeft = mouseLeft;
}

void PhaseSystemScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game