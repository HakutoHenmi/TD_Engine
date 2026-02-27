#pragma once
#include "IScript.h"

namespace Game {

class kariScript : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(SceneObject& obj, GameScene* scene) override;

	// 毎フレーム処理
	void Update(SceneObject& obj, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(SceneObject& obj, GameScene* scene) override;
};

} // namespace Game
