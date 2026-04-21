#include "SkillTree.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include "../Scenes/GameScene.h"
#include "ObjectTypes.h"
#include "PlayerScript.h"
#include "ScriptEngine.h"
#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace Game {
void SkillTree::LoadFromJson(const std::string& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}

	json root;

	try {
		file >> root;
		nodes_.clear();

		if (root.contains("skills") && root["skills"].is_array()) {
			for (const auto& skillJson : root["skills"]) {
				SkillNode node;

				node.id = skillJson.value("id", 0);
				node.name = skillJson.value("name", "Skill");
				node.cost = skillJson.value("cost", 1);
				node.parentId = skillJson.value("parentId", -1);
				node.unlocked = skillJson.value("unlocked", false);
				node.gridX = skillJson.value("gridX", 0.0f);
				node.gridY = skillJson.value("gridY", 0.0f);
				node.description = skillJson.value("description", "");
				node.pageId = skillJson.value("pageId", 0);

				// アイコン読み込み
				std::string iconName = skillJson.value("name", "");
				std::string iconPath = "Resources/Skills/" + iconName + ".png";

				node.texturePath = iconPath;

				if (renderer_) {
					node.textureHandle = renderer_->LoadTexture2D(iconPath);
				}

				nodes_.push_back(node);
			}
		}
	} catch (...) {
		// パース失敗時は何もしない
	}
}
void SkillTree::SetUIContext(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY) {
	renderer_ = renderer;
	screenW_ = screenW;
	screenH_ = screenH;
	mouseX_ = mouseX;
	mouseY_ = mouseY;
}

void SkillTree::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;

	if (!renderer_) {
		return;
	}

	// 初期化（テクスチャ）
	if (!initialized_) {
		texBg_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texNodeLocked_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texNodeUnlocked_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texLine_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

		initialized_ = true;
	}

	// JSON読み込み（1回だけ）
	if (!dataLoaded_) {
		LoadFromJson("Resources/Data/skills.json");
		dataLoaded_ = true;
	}
}

void SkillTree::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;

	if (!scene) {
		return;
	}

	if (!isOpen_) {
		return;
	}

	if (!renderer_) {
		return;
	}

	Engine::Input* input = Engine::Input::GetInstance();
	if (input) {
		if (input->Trigger(DIK_RIGHT)) {
			NextPage();
		}

		if (input->Trigger(DIK_LEFT)) {
			PrevPage();
		}
	}

	HandleInput(screenW_, screenH_, mouseX_, mouseY_);
	DrawBackground(renderer_, screenW_, screenH_);
	DrawConnections(renderer_, screenW_, screenH_);
	ApplyToBaseDefenseScript(entity, scene);

	int hoveredIndex = -1;

	for (int i = 0; i < (int)nodes_.size(); ++i) {
		if (nodes_[i].pageId != currentPageId_) {
			continue;
		}

		float nodeX = 0.0f;
		float nodeY = 0.0f;
		GetNodeScreenPos(nodes_[i], screenW_, screenH_, nodeX, nodeY);

		float halfSize = kNodeSize * 0.5f;
		if (mouseX_ >= nodeX - halfSize && mouseX_ <= nodeX + halfSize && mouseY_ >= nodeY - halfSize && mouseY_ <= nodeY + halfSize) {
			hoveredIndex = i;
			break;
		}
	}

	DrawNodes(renderer_, screenW_, screenH_, mouseX_, mouseY_);
	DrawSkillPointsText(renderer_, screenW_, screenH_);

	if (hoveredIndex >= 0 && pendingUnlockId_ == -1) {
		DrawDescriptionPanel(renderer_, screenW_, screenH_, hoveredIndex);
	}

	if (pendingUnlockId_ != -1) {
		DrawConfirmationDialog(renderer_, screenW_, screenH_);
	}

	ImGui::Begin("SkillTree Debug");
	ImGui::Text("currentPageId = %d", currentPageId_);
	ImGui::End();
}

void SkillTree::OnDestroy(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;

	renderer_ = nullptr;
	screenW_ = 0.0f;
	screenH_ = 0.0f;
	mouseX_ = 0.0f;
	mouseY_ = 0.0f;
	pendingUnlockId_ = -1;
	isOpen_ = false;
}

void SkillTree::ApplyToBaseDefenseScript(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	float attackPowerRate = 1.0f;
	float moveSpeedRate = 1.0f;
	float attackRangeRate = 1.0f;
	float attackSpeedRate = 1.0f;
	float criticalRateBonus = 0.0f;

	if (IsSkillUnlocked(1)) {
		attackPowerRate *= 1.10f;
	}

	if (IsSkillUnlocked(2)) {
		moveSpeedRate *= 1.15f;
	}

	if (IsSkillUnlocked(3)) {
		attackRangeRate *= 1.20f;
	}

	if (IsSkillUnlocked(4)) {
		attackPowerRate *= 1.20f;
	}

	if (IsSkillUnlocked(5)) {
		attackSpeedRate *= 1.25f;
	}

	if (IsSkillUnlocked(6)) {
		criticalRateBonus += 0.10f;
	}

	if (IsSkillUnlocked(7)) {
		attackPowerRate *= 1.30f;
		moveSpeedRate *= 1.30f;
		attackRangeRate *= 1.30f;
		attackSpeedRate *= 1.30f;
		criticalRateBonus += 0.20f;
	}

	SetVar(entity, scene, "AttackPowerRate", attackPowerRate);
	SetVar(entity, scene, "MoveSpeedRate", moveSpeedRate);
	SetVar(entity, scene, "AttackRangeRate", attackRangeRate);
	SetVar(entity, scene, "AttackSpeedRate", attackSpeedRate);
	SetVar(entity, scene, "CriticalRateBonus", criticalRateBonus);
}

void SkillTree::HandleInput(float screenW, float screenH, float mouseX, float mouseY) {
	Engine::Input* input = Engine::Input::GetInstance();
	if (!input) {
		return;
	}

	if (!input->IsMouseTrigger(0)) {
		return;
	}

	if (pendingUnlockId_ != -1) {
		float centerX = screenW * 0.5f;
		float centerY = screenH * 0.5f;

		if (mouseX >= centerX - 110 && mouseX <= centerX - 10 && mouseY >= centerY + 20 && mouseY <= centerY + 60) {
			ConfirmUnlock();
		} else if (mouseX >= centerX + 10 && mouseX <= centerX + 110 && mouseY >= centerY + 20 && mouseY <= centerY + 60) {
			CancelUnlock();
		}
		return;
	}

	for (int i = 0; i < (int)nodes_.size(); ++i) {
		if (nodes_[i].pageId != currentPageId_) {
			continue;
		}

		float nodeX = 0.0f;
		float nodeY = 0.0f;
		GetNodeScreenPos(nodes_[i], screenW, screenH, nodeX, nodeY);

		float halfSize = kNodeSize * 0.5f;
		if (mouseX >= nodeX - halfSize && mouseX <= nodeX + halfSize && mouseY >= nodeY - halfSize && mouseY <= nodeY + halfSize) {
			TryUnlockSkill(i);
			break;
		}
	}
}

bool SkillTree::TryUnlockSkill(int index) {
	if (index < 0 || index >= (int)nodes_.size()) {
		return false;
	}

	if (nodes_[index].unlocked) {
		return false;
	}

	std::vector<int> neededIndices;
	GetPrerequisites(index, neededIndices);

	int totalCost = 0;
	for (int idx : neededIndices) {
		totalCost += nodes_[idx].cost;
	}

	if (skillPoints_ < totalCost) {
		return false;
	}

	pendingUnlockId_ = index;
	return true;
}

void SkillTree::GetPrerequisites(int index, std::vector<int>& outIndices) {
	if (nodes_[index].unlocked) {
		return;
	}

	int parentId = nodes_[index].parentId;
	if (parentId >= 0) {
		for (int i = 0; i < (int)nodes_.size(); ++i) {
			if (nodes_[i].id == parentId && nodes_[i].pageId == currentPageId_) {
				GetPrerequisites(i, outIndices);
				break;
			}
		}
	}

	if (std::find(outIndices.begin(), outIndices.end(), index) == outIndices.end()) {
		outIndices.push_back(index);
	}
}

void SkillTree::ConfirmUnlock() {
	if (pendingUnlockId_ == -1) {
		return;
	}

	std::vector<int> neededIndices;
	GetPrerequisites(pendingUnlockId_, neededIndices);

	int totalCost = 0;
	for (int idx : neededIndices) {
		totalCost += nodes_[idx].cost;
	}

	if (skillPoints_ >= totalCost) {
		skillPoints_ -= totalCost;
		for (int idx : neededIndices) {
			nodes_[idx].unlocked = true;
		}
	}

	pendingUnlockId_ = -1;
}

void SkillTree::CancelUnlock() { pendingUnlockId_ = -1; }

bool SkillTree::IsSkillUnlocked(int skillId) const {
	for (const SkillNode& node : nodes_) {
		if (node.id == skillId) {
			return node.unlocked;
		}
	}
	return false;
}

void SkillTree::DrawBackground(Engine::Renderer* renderer, float screenW, float screenH) {
	Engine::Renderer::SpriteDesc bg;
	bg.x = kPanelMargin;
	bg.y = kPanelMargin;
	bg.w = screenW - kPanelMargin * 2.0f;
	bg.h = screenH - kPanelMargin * 2.0f;
	bg.color = {0.05f, 0.05f, 0.15f, 0.85f};
	renderer->DrawSprite(texBg_, bg);
}

void SkillTree::DrawConnections(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;
	(void)screenH;

	for (const SkillNode& node : nodes_) {
		if (node.parentId < 0) {
			continue;
		}

		if (node.pageId != currentPageId_) {
			continue;
		}

		const SkillNode* parent = nullptr;
		for (const SkillNode& checkNode : nodes_) {
			if (checkNode.id == node.parentId && checkNode.pageId == currentPageId_) {
				parent = &checkNode;
				break;
			}
		}

		if (!parent) {
			continue;
		}

		float childX = 0.0f;
		float childY = 0.0f;
		float parentX = 0.0f;
		float parentY = 0.0f;
		GetNodeScreenPos(node, screenW, screenH, childX, childY);
		GetNodeScreenPos(*parent, screenW, screenH, parentX, parentY);

		Engine::Vector4 lineColor;
		if (node.unlocked && parent->unlocked) {
			lineColor = {0.2f, 0.9f, 0.3f, 0.9f};
		} else {
			lineColor = {0.4f, 0.4f, 0.4f, 0.7f};
		}

		float minY = (std::min)(childY, parentY);
		float maxY = (std::max)(childY, parentY);

		Engine::Renderer::SpriteDesc verticalLine;
		verticalLine.x = childX - kLineWidth * 0.5f;
		verticalLine.y = minY;
		verticalLine.w = kLineWidth;
		verticalLine.h = maxY - minY;
		verticalLine.color = lineColor;
		renderer->DrawSprite(texLine_, verticalLine);

		if (std::abs(childX - parentX) > 1.0f) {
			float leftX = (std::min)(childX, parentX);
			float rightX = (std::max)(childX, parentX);

			Engine::Renderer::SpriteDesc horizontalLine;
			horizontalLine.x = leftX;
			horizontalLine.y = parentY - kLineWidth * 0.5f;
			horizontalLine.w = rightX - leftX;
			horizontalLine.h = kLineWidth;
			horizontalLine.color = lineColor;
			renderer->DrawSprite(texLine_, horizontalLine);
		}
	}
}

void SkillTree::DrawNodes(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY) {
	for (const SkillNode& node : nodes_) {
		if (node.pageId != currentPageId_) {
			continue;
		}

		float nodeX = 0.0f;
		float nodeY = 0.0f;
		GetNodeScreenPos(node, screenW, screenH, nodeX, nodeY);

		float halfSize = kNodeSize * 0.5f;
		bool isHovered = false;
		if (mouseX >= nodeX - halfSize && mouseX <= nodeX + halfSize && mouseY >= nodeY - halfSize && mouseY <= nodeY + halfSize) {
			isHovered = true;
		}

		Engine::Vector4 color;
		if (node.unlocked) {
			color = {0.2f, 0.85f, 0.3f, 1.0f};
		} else {
			bool canUnlock = true;
			if (node.parentId >= 0) {
				canUnlock = false;
				for (const SkillNode& parentNode : nodes_) {
					if (parentNode.id == node.parentId && parentNode.pageId == currentPageId_ && parentNode.unlocked) {
						canUnlock = true;
						break;
					}
				}
			}

			if (canUnlock && skillPoints_ >= node.cost) {
				if (isHovered) {
					color = {1.0f, 0.9f, 0.3f, 1.0f};
				} else {
					color = {0.8f, 0.7f, 0.2f, 0.9f};
				}
			} else {
				color = {0.3f, 0.3f, 0.3f, 0.7f};
			}
		}

		Engine::Renderer::SpriteDesc sprite;
		sprite.x = nodeX - halfSize;
		sprite.y = nodeY - halfSize;
		sprite.w = kNodeSize;
		sprite.h = kNodeSize;
		sprite.color = color;

		uint32_t textureHandle = 0;
		if (node.textureHandle != 0) {
			textureHandle = node.textureHandle;
		} else {
			if (node.unlocked) {
				textureHandle = texNodeUnlocked_;
			} else {
				textureHandle = texNodeLocked_;
			}
		}

		renderer->DrawSprite(textureHandle, sprite);
	}
}

void SkillTree::DrawSkillPointsText(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;

	float baseX = kPanelMargin + 20.0f;
	float baseY = screenH - kPanelMargin - 40.0f;
	float dotSize = 16.0f;
	float dotGap = 4.0f;
	int displayPoints = (std::min)(skillPoints_, 20);

	for (int i = 0; i < displayPoints; ++i) {
		Engine::Renderer::SpriteDesc dot;
		dot.x = baseX + i * (dotSize + dotGap);
		dot.y = baseY;
		dot.w = dotSize;
		dot.h = dotSize;
		dot.color = {0.9f, 0.8f, 0.1f, 1.0f};
		renderer->DrawSprite(texBg_, dot);
	}
}

void SkillTree::DrawDescriptionPanel(Engine::Renderer* renderer, float screenW, float screenH, int hoveredNodeIndex) {
	(void)screenH;

	const SkillNode& node = nodes_[hoveredNodeIndex];

	float panelWidth = 400.0f;
	float panelHeight = screenH - kPanelMargin * 2.0f;
	float panelX = screenW - kPanelMargin - panelWidth;
	float panelY = kPanelMargin;

	Engine::Renderer::SpriteDesc bg;
	bg.x = panelX;
	bg.y = panelY;
	bg.w = panelWidth;
	bg.h = panelHeight;
	bg.color = {0.05f, 0.05f, 0.15f, 0.9f};
	renderer->DrawSprite(texBg_, bg);

	Engine::Renderer::SpriteDesc border;
	border.x = panelX;
	border.y = panelY;
	border.w = panelWidth;
	border.h = 4.0f;
	if (node.unlocked) {
		border.color = {0.2f, 0.8f, 0.4f, 1.0f};
	} else {
		border.color = {0.3f, 0.6f, 1.0f, 1.0f};
	}
	renderer->DrawSprite(texBg_, border);

	if (node.textureHandle != 0) {
		Engine::Renderer::SpriteDesc icon;
		icon.x = panelX + 20.0f;
		icon.y = panelY + panelHeight - 80.0f;
		icon.w = 60.0f;
		icon.h = 60.0f;
		icon.color = {1.0f, 1.0f, 1.0f, 1.0f};
		renderer->DrawSprite(node.textureHandle, icon);
	}

#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList) {
		ImVec2 pos(panelX + 20.0f, panelY + 30.0f);

		drawList->AddText(ImGui::GetFont(), 28.0f, pos, IM_COL32(255, 255, 255, 255), node.name.c_str());

		pos.y += 45.0f;
		std::string costString = "Cost: " + std::to_string(node.cost) + " SP";
		drawList->AddText(ImGui::GetFont(), 20.0f, pos, IM_COL32(200, 200, 200, 255), costString.c_str());

		pos.y += 35.0f;
		drawList->AddLine(pos, ImVec2(pos.x + panelWidth - 40.0f, pos.y), IM_COL32(100, 100, 100, 255), 1.0f);

		pos.y += 30.0f;
		drawList->AddText(ImGui::GetFont(), 18.0f, pos, IM_COL32(220, 220, 220, 255), node.description.c_str());

		if (node.unlocked) {
			pos.y += 60.0f;
			drawList->AddText(ImGui::GetFont(), 22.0f, pos, IM_COL32(50, 255, 100, 255), "[Unlocked]");
		}
	}
#endif
}

void SkillTree::DrawConfirmationDialog(Engine::Renderer* renderer, float screenW, float screenH) {
	if (pendingUnlockId_ < 0 || pendingUnlockId_ >= (int)nodes_.size()) {
		return;
	}

	const SkillNode& node = nodes_[pendingUnlockId_];

	float dialogWidth = 400.0f;
	float dialogHeight = 160.0f;
	float centerX = screenW * 0.5f;
	float centerY = screenH * 0.5f;

	Engine::Renderer::SpriteDesc bg;
	bg.x = centerX - dialogWidth * 0.5f;
	bg.y = centerY - dialogHeight * 0.5f;
	bg.w = dialogWidth;
	bg.h = dialogHeight;
	bg.color = {0.1f, 0.1f, 0.2f, 1.0f};
	renderer->DrawSprite(texBg_, bg);

	Engine::Renderer::SpriteDesc border;
	border.x = centerX - dialogWidth * 0.5f;
	border.y = centerY - dialogHeight * 0.5f;
	border.w = dialogWidth;
	border.h = 2.0f;
	border.color = {1.0f, 0.8f, 0.2f, 1.0f};
	renderer->DrawSprite(texBg_, border);

#ifdef USE_IMGUI
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList) {
		std::string message = "Unlock '" + node.name + "'?";

		std::vector<int> neededIndices;
		GetPrerequisites(pendingUnlockId_, neededIndices);
		if (neededIndices.size() > 1) {
			message = "Unlock '" + node.name + "' and its prerequisites?";
		}

		ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
		drawList->AddText(ImGui::GetFont(), 20.0f, ImVec2(centerX - textSize.x * 0.5f, centerY - 40.0f), IM_COL32(255, 255, 255, 255), message.c_str());

		drawList->AddText(ImGui::GetFont(), 18.0f, ImVec2(centerX - 85.0f, centerY + 28.0f), IM_COL32(255, 255, 255, 255), "YES");
		drawList->AddText(ImGui::GetFont(), 18.0f, ImVec2(centerX + 45.0f, centerY + 28.0f), IM_COL32(255, 255, 255, 255), "NO");
	}
#endif

	Engine::Renderer::SpriteDesc yesButton;
	yesButton.x = centerX - 110.0f;
	yesButton.y = centerY + 20.0f;
	yesButton.w = 100.0f;
	yesButton.h = 40.0f;
	yesButton.color = {0.2f, 0.7f, 0.3f, 0.8f};
	renderer->DrawSprite(texBg_, yesButton);

	Engine::Renderer::SpriteDesc noButton;
	noButton.x = centerX + 10.0f;
	noButton.y = centerY + 20.0f;
	noButton.w = 100.0f;
	noButton.h = 40.0f;
	noButton.color = {0.8f, 0.2f, 0.2f, 0.8f};
	renderer->DrawSprite(texBg_, noButton);
}

void SkillTree::GetNodeScreenPos(const SkillNode& node, float screenW, float screenH, float& outX, float& outY) const {
	float panelHeight = screenH - kPanelMargin * 2.0f;
	float panelCenterX = screenW * 0.5f;

	float maxGridX = 0.0f;
	float maxGridY = 0.0f;
	for (const SkillNode& checkNode : nodes_) {
		if (checkNode.pageId != currentPageId_) {
			continue;
		}

		if (checkNode.gridX > maxGridX) {
			maxGridX = checkNode.gridX;
		}

		if (checkNode.gridY > maxGridY) {
			maxGridY = checkNode.gridY;
		}
	}

	float treeWidth = maxGridX * kNodeSpacingX;
	float treeHeight = maxGridY * kNodeSpacingY;

	float startX = panelCenterX - treeWidth * 0.5f;
	float startY = kPanelMargin + panelHeight * 0.5f - treeHeight * 0.5f;

	outX = startX + node.gridX * kNodeSpacingX;
	outY = startY + (maxGridY - node.gridY) * kNodeSpacingY;
}

void SkillTree::SetCurrentPageId(int pageId) {
	if (pageId < 0) {
		return;
	}

	if (pageId >= pageCount_) {
		return;
	}

	currentPageId_ = pageId;
	pendingUnlockId_ = -1;
}

void SkillTree::NextPage() {
	currentPageId_++;

	if (currentPageId_ >= pageCount_) {
		currentPageId_ = pageCount_ - 1;
	}

	pendingUnlockId_ = -1;
}

void SkillTree::PrevPage() {
	if (currentPageId_ > 0) {
		currentPageId_--;
	}

	pendingUnlockId_ = -1;
}

REGISTER_SCRIPT(SkillTree);

} // namespace Game