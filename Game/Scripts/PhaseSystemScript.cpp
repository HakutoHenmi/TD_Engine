#include "PhaseSystemScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

namespace Game {

	bool PhaseSystemScript::isPreparation_ = false;

void PhaseSystemScript::Start(SceneObject& obj, GameScene* scene) {
	(obj);
	(scene);
}

void PhaseSystemScript::Update(SceneObject& obj, GameScene* scene, float dt) {
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
	static bool preKey1 = false;
	bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;

	if (key1 && !preKey1) {
		SceneObject obj;
		obj.name = "New Object";
		// 必要に応じて、モデルやテクスチャ、位置などを設定します
		 obj.modelPath = "Resources/cube/cube.obj";
		 obj.texturePath = "Resources/white1x1.png";
		 obj.translate = { 0.0f, 10.0f, 0.0f };

		 HealthComponent health;
		 health.hp = 100.0f;
		 health.maxHp = 100.0f;
		 obj.healths.push_back(health);

		 // ★★★ 描画のための修正箇所 ★★★
		 // 1. MeshRendererコンポーネントを追加
		 MeshRendererComponent mr;
		 mr.modelPath = obj.modelPath;
		 mr.texturePath = obj.texturePath;

		 // 2. レンダラーを取得してリソースをロードし、ハンドルを設定
		 auto* renderer = Engine::Renderer::GetInstance();
		 if (renderer) {
			 mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
			 mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
		 }
		 obj.meshRenderers.push_back(mr);
		 // ★★★ ここまで ★★★

		scene->SpawnObject(obj);
	}

	preKey1 = key1;
}

void PhaseSystemScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game