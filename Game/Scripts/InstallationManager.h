#pragma once
#include "IScript.h"
#include <DirectXMath.h>
#include <vector>
#include <string>

namespace Game {

struct ButtonData {
	std::string name;
	std::string texturePath;
	std::string prefabPath;
	int cost = 0;
	DirectX::XMFLOAT2 pos = {0, 0};
	DirectX::XMFLOAT2 size = {100, 100};
	entt::entity entity = entt::null;
	bool isPressed = false;
};

class InstallationManager : public IScript {
public:
	InstallationManager();

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnEditorUI() override;

	void Draw(entt::entity entity, GameScene* scene) override;
	void DrawUI(entt::entity entity, GameScene* scene) override;

	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	static bool IsButtonPressed(const std::string& prefabPath);
	static int GetCost(const std::string& prefabPath);
	static bool IsButtonPressedByName(const std::string& name);
	static bool IsManagedButton(entt::entity entity);

private:
	GameScene* currentScene_ = nullptr;
	ButtonData buttons_[6];
	int currentPage_ = 0;
	static InstallationManager* instance_;

	// 固定パス（ImGuiではなくコードで管理するための変数）
	std::string texPaths_[6];
	std::string prefabPaths_[6];

	void EnsureButtonEntity(ButtonData& data, GameScene* scene);
};

} // namespace Game
