#pragma once
#include "../../externals/entt/entt.hpp"
#include "IScript.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {
class Renderer;
}

namespace Game {

class GameScene;

struct SkillNode {
	int id = 0;
	std::string name;
	std::string texturePath;
	int cost = 1;
	int parentId = -1;
	bool unlocked = false;
	float gridX = 0.0f;
	float gridY = 0.0f;
	std::string description;
	uint32_t textureHandle = 0;
	int pageId = 0;
};

class SkillTree : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void SetUIContext(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY);
	void ApplyToBaseDefenseScript(entt::entity entity, GameScene* scene);

	void Toggle(GameScene* scene) {
		isOpen_ = !isOpen_;
		if (!isOpen_) {
			pendingUnlockId_ = -1;
			ClearText(scene);
		}
	}

	bool IsOpen() const { return isOpen_; }

	void Close(GameScene* scene) {
		isOpen_ = false;
		pendingUnlockId_ = -1;
		ClearText(scene);
	}

	int GetSkillPoints() const { return skillPoints_; }

	void AddSkillPoints(int pts) { skillPoints_ += pts; }

	bool IsSkillUnlocked(int skillId) const;
	void LoadFromJson(const std::string& path);
	void ClearText(GameScene* scene);

private:
	void DrawBackground(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawNodes(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY);
	void DrawConnections(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawSkillPointsText(Engine::Renderer* renderer, float screenW, float screenH);
	void DrawDescriptionPanel(Engine::Renderer* renderer, float screenW, float screenH, int hoveredNodeIndex);
	void DrawConfirmationDialog(Engine::Renderer* renderer, float screenW, float screenH);
	void HandleInput(float screenW, float screenH, float mouseX, float mouseY);
	bool TryUnlockSkill(int index);
	void ConfirmUnlock();
	void CancelUnlock();
	void GetPrerequisites(int index, std::vector<int>& outIndices);
	void GetNodeScreenPos(const SkillNode& node, float screenW, float screenH, float& outX, float& outY) const;
	void SetCurrentPageId(int pageId);
	void NextPage();
	void PrevPage();
	bool HandlePageButtonInput(float screenW, float screenH, float mouseX, float mouseY);
	void DrawPageButtons(Engine::Renderer* renderer, float screenW, float screenH);
	void UpdatePageButtonRect(float screenW, float screenH);

private:
	bool isOpen_ = false;
	bool initialized_ = false;
	bool dataLoaded_ = false;
	bool eventSubscribed_ = false;
	int skillPoints_ = 100;
	int pendingUnlockId_ = -1;
	int currentPageId_ = 0;
	int pageCount_ = 5;

	std::vector<SkillNode> nodes_;


	// テクスチャハンドル（リソース管理は別途行うことを想定）
	uint32_t texBg_ = 0;
	uint32_t texNodeLocked_ = 0;
	uint32_t texNodeUnlocked_ = 0;
	uint32_t texLine_ = 0;
	uint32_t texPrevArrow_ = 0;
	uint32_t texNextArrow_ = 0;
	uint32_t texSkillPoint_ = 0;
	uint32_t texPanel_ = 0;
	entt::entity yesTextEntity_ = entt::null;
	entt::entity noTextEntity_ = entt::null;
	entt::entity confirmMessageEntity_ = entt::null;
	entt::entity messageTextEntity_ = entt::null;
	Engine::Renderer* renderer_ = nullptr;
	float screenW_ = 0.0f;
	float screenH_ = 0.0f;
	float mouseX_ = 0.0f;
	float mouseY_ = 0.0f;

	static constexpr float kPanelMargin = 100.0f;
	static constexpr float kNodeSize = 64.0f;
	static constexpr float kNodeSpacingX = 100.0f;
	static constexpr float kNodeSpacingY = 100.0f;
	static constexpr float kLineWidth = 4.0f;

	float prevButtonLeft_ = 100.0f;
	float prevButtonTop_ = 520.0f;
	float prevButtonRight_ = 180.0f;
	float prevButtonBottom_ = 570.0f;

	float nextButtonLeft_ = 1000.0f;
	float nextButtonTop_ = 520.0f;
	float nextButtonRight_ = 1080.0f;
	float nextButtonBottom_ = 570.0f;

	entt::entity titleTextEntity_ = entt::null;
	entt::entity costTextEntity_ = entt::null;
	entt::entity descTextEntity_ = entt::null;
	entt::entity statusTextEntity_ = entt::null;
	entt::entity pageTitleEntity_ = entt::null;
};

} // namespace Game