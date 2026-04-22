#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include "../../externals/entt/entt.hpp"
#include <vector>
#include <string>

namespace Game {

// タイトル画面のUIとロジックを管理するスクリプト
// エディタでTitle用シーンファイルにアタッチして使用
class TitleManagerScript : public IScript {
public:
	static void CreateFallbackUI(GameScene* scene);
	static entt::entity CreateTitleButton(entt::registry& reg, const std::string& text, float yPos, entt::entity parent);

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void DrawUI(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	// メニュー状態
	enum class MenuState { Main, Settings };
	MenuState state_ = MenuState::Main;

	// UIエンティティ参照
	entt::entity btnStart_ = entt::null;
	entt::entity btnSettings_ = entt::null;
	entt::entity btnExit_ = entt::null;

	// Settings
	entt::entity btnFullscreen_ = entt::null;
	entt::entity btnBGMMinus_ = entt::null;
	entt::entity btnBGMPlus_ = entt::null;
	entt::entity btnSEMinus_ = entt::null;
	entt::entity btnSEPlus_ = entt::null;
	entt::entity btnBack_ = entt::null;
	entt::entity textFullscreen_ = entt::null;
	entt::entity textBGM_ = entt::null;
	entt::entity textSE_ = entt::null;

	std::vector<entt::entity> mainEntities_;
	std::vector<entt::entity> settingsEntities_;

	bool uiInitialized_ = false;
};

} // namespace Game
