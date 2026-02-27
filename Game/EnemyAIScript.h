#pragma once
#include "IScript.h"
#include <DirectXMath.h>

namespace Game {

class EnemyAIScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	float speed_ = 2.0f;
	float sightRange_ = 100.0f; // プレイヤーを追尾する距離
	bool exploded_ = false;     // ★追加: 爆発済みフラグ
	size_t playerIndexCache_ = static_cast<size_t>(-1); // ★追加: プレイヤーのインデックスキャッシュ
};

} // namespace Game