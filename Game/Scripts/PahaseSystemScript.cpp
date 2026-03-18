#include "PahaseSystemScript.h"
#include "Editor/EditorUI.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
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
	(gameImageMin);
	(tW);
	(tH);

	auto* input = Engine::Input::GetInstance();

	if (input->IsMouseTrigger(0)) {
		float mx, my;
		input->GetMousePos(mx, my);

		// 1. カメラ情報の取得
		auto& camera = scene->GetCamera();
		DirectX::XMMATRIX view = camera.View();
		DirectX::XMMATRIX proj = camera.Proj();

		// View-Projectionの逆行列を計算
		DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, view * proj);

		// 2. スクリーン座標を NDC (Normalized Device Coordinates) に変換 (-1.0 ～ 1.0)
		float nx = (mx / (float)Engine::WindowDX::kW) * 2.0f - 1.0f;
		float ny = 1.0f - (my / (float)Engine::WindowDX::kH) * 2.0f;

		// 3. レイの始点（近クリップ面）と終点（遠クリップ面）をワールド座標で求める
		DirectX::XMVECTOR nearPointNDC = DirectX::XMVectorSet(nx, ny, 0.0f, 1.0f);
		DirectX::XMVECTOR farPointNDC = DirectX::XMVectorSet(nx, ny, 1.0f, 1.0f);

		DirectX::XMVECTOR posNear = DirectX::XMVector3TransformCoord(nearPointNDC, invVP);
		DirectX::XMVECTOR posFar = DirectX::XMVector3TransformCoord(farPointNDC, invVP);
		DirectX::XMVECTOR rayDir = DirectX::XMVector3Normalize(posFar - posNear);

		// 4. 地面 (y=0 の平面) との交差判定
		DirectX::XMFLOAT3 pNear, d;
		DirectX::XMStoreFloat3(&pNear, posNear);
		DirectX::XMStoreFloat3(&d, rayDir);

		// レイが下を向いている場合のみ判定（平行な場合は除外）
		if (std::abs(d.y) > 0.0001f) {
			// pNear.y + d.y * t = 0  =>  t = -pNear.y / d.y
			float t = -pNear.y / d.y;

			if (t > 0) { // カメラより前方にある場合
				DirectX::XMFLOAT3 hitPos;
				hitPos.x = pNear.x + d.x * t;
				hitPos.y = pNear.y + d.y * t;
				hitPos.z = pNear.z + d.z * t;

				// 5. オブジェクトの生成
				SceneObject newObj;
				newObj.name = "PlacedObject";
				newObj.translate = hitPos;
				newObj.scale = {1.0f, 1.0f, 1.0f};
				newObj.color = {1.0f, 1.0f, 1.0f, 1.0f};

				// モデルやテクスチャを設定する場合の例
				auto* renderer = scene->GetRenderer();
				if (renderer) {
					newObj.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
					newObj.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
				}

				scene->SpawnObject(newObj);
			}
		}
	}
}

void PahaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PahaseSystemScript);

} // namespace Game