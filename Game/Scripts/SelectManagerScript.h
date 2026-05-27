#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"
#include <vector>
#include <string>

namespace Game {

// セレクト画面のUIとロジックを管理するスクリプト
class SelectManagerScript : public IScript {
public:
	static void CreateFallbackUI(GameScene* scene);

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	struct StageInfo {
		std::string name;
		std::string path;
		std::string description;
	};
	std::vector<StageInfo> stages_;

	bool uiInitialized_ = false;
	
	int selectedIndex_ = 0;
	float currentAngle_ = 0.0f;
	float targetAngle_ = 0.0f;
	
	float steamTimerH_ = 0.0f; // ★水平パイプ用タイマー
	float steamTimerV_ = 0.5f; // ★垂直パイプ用タイマー（タイミングをずらす）
};

} // namespace Game
