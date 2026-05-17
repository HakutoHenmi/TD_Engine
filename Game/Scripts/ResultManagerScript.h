#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"
#include <string>

namespace Game {

// リザルト画面のUIとロジックを管理するスクリプト
class ResultManagerScript : public IScript {
public:
	static void CreateFallbackUI(GameScene* scene, bool isWin, int score, float clearTime);

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	bool uiInitialized_ = false;

	// SceneParametersから渡される情報（あるいはstatic変数から）
	bool isWin_ = false;
	int score_ = 0;
	float clearTime_ = 0.0f;
	std::string originalScene_ = "tesuto_light";

public:
	static inline bool pendingIsWin = false;
	static inline std::string pendingOriginalScene = "tesuto_light";
};

} // namespace Game
