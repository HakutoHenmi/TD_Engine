#pragma once
#include "IScript.h"
#include <string>
#include <vector>

namespace Game {

struct ButtonData {
	std::string name;
	std::string texturePath;
	std::string prefabPath;
	int cost = 0;
	bool isPipe = false;
	uint32_t textureHandle = 0;
};

class InstallationButton : public IScript {
public:
	InstallationButton();

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void DrawUI(entt::entity entity, GameScene* scene) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

private:
	std::vector<ButtonData> buttons_;
	int currentPage_ = 0;
	uint32_t arrowTexHandle_ = 0;
	bool initialized_ = false;

	// Constants for UI layout
	const float kButtonSize = 80.0f;
	const float kButtonSpacing = 20.0f;
	const float kMarginBottom = 50.0f;
};

} // namespace Game