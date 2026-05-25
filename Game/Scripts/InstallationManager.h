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
	ButtonData buttons_[7];
	int currentPage_ = 0;
	static InstallationManager* instance_;

	// 固定パス（ImGuiではなくコードで管理するための変数）
	std::string texPaths_[7];
	std::string prefabPaths_[7];

	void EnsureButtonEntity(ButtonData& data, GameScene* scene);
};

// 互換性維持のためのダミースクリプト
class InstallationButton : public IScript {
public:
	void Start(entt::entity, GameScene*) override {}
	void Update(entt::entity, GameScene*, float) override {}
};

} // namespace Game
