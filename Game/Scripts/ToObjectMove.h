#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"

namespace Game {

class ToObjectMove : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(SceneObject& obj, GameScene* scene) override;

	// 毎フレーム処理
	void Update(SceneObject& obj, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:	// メンバ変数
	// 参照するプレイヤーのポインタと位置
	const SceneObject* target_ = nullptr;
	DirectX::XMFLOAT3 myPos_ = {};
	DirectX::XMFLOAT3 targetPos_ = {};
	// 移動速度
	float speed_ = 5.0f;
};

} // namespace Game
