#pragma once
#include <string>

#include "../../externals/entt/entt.hpp"

namespace Game {

struct SceneObject;
class GameScene;

class IScript {
public:
	virtual ~IScript() = default;

	// スクリプトの初期化（アタッチ時やシーン開始時に1回呼ばれる）
	virtual void Start(entt::entity /*entity*/, GameScene* /*scene*/) {}
	
	// 毎フレーム呼ばれる更新処理
	virtual void Update(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {}
	
	// UIクリック時に呼ばれる
	virtual void OnClick(entt::entity /*entity*/, GameScene* /*scene*/, const std::string& /*callbackName*/) {}
	
	// オブジェクト破棄時やスクリプトが外れた時に呼ばれる
	virtual void OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

	// エディターUI描画用
	virtual void OnEditorUI() {}

	// パラメーターの個別保存・読み込み用 (エディター用)
	virtual std::string SerializeParameters() { return ""; }
	virtual void DeserializeParameters(const std::string& /*data*/) {}
};

} // namespace Game
