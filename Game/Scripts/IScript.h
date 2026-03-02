#pragma once

namespace Game {

struct SceneObject;
class GameScene;

class IScript {
public:
	virtual ~IScript() = default;

	// スクリプトの初期化（アタッチ時やシーン開始時に1回呼ばれる）
	virtual void Start(SceneObject& /*obj*/, GameScene* /*scene*/) {}
	
	// 毎フレーム呼ばれる更新処理
	virtual void Update(SceneObject& /*obj*/, GameScene* /*scene*/, float /*dt*/) {}
	
	// オブジェクト破棄時やスクリプトが外れた時に呼ばれる
	virtual void OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}
};

} // namespace Game
