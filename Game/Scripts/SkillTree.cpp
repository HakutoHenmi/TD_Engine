#include "SkillTree.h"
#include "../../Engine/Audio.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include "../Scenes/GameScene.h"
#include "ObjectTypes.h"
#include "PlayerScript.h"
#include "ScriptEngine.h"
#include "TutorialScript.h"
#include "PhaseSystemScript.h"
#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace Game {

#pragma region json読み込み処理
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
				node.icon = skillJson.value("icon", "None");
				node.name = skillJson.value("name", "Skill");
				node.cost = skillJson.value("cost", 1);
				node.parentId = skillJson.value("parentId", -1);
				node.unlocked = skillJson.value("unlocked", false);
				node.gridX = skillJson.value("gridX", 0.0f);
				node.gridY = skillJson.value("gridY", 0.0f);
				node.description = skillJson.value("description", "");
				node.pageId = skillJson.value("pageId", 0);

				// アイコン読み込み
				std::string iconName = skillJson.value("icon", "");
				// アイコン読み込み
				std::string iconPath = "Resources/Skills/" + node.icon + ".png";

				node.texturePath = iconPath;

				if (renderer_) {
					node.textureHandle = renderer_->LoadTexture2D(iconPath);
				}

				nodes_.push_back(node);
			}
		}
		int maxPage = 0;

		for (const SkillNode& node : nodes_) {
			if (node.pageId > maxPage) {
				maxPage = node.pageId;
			}
		}

		pageCount_ = maxPage + 1;
	} catch (...) {
		// パース失敗時は何もしない
	}
}

#pragma endregion

void SkillTree::SetUIContext(Engine::Renderer* renderer, float screenW, float screenH, float mouseX, float mouseY) {
	renderer_ = renderer;
	screenW_ = screenW;
	screenH_ = screenH;
	mouseX_ = mouseX;
	mouseY_ = mouseY;
}

void SkillTree::Start(entt::entity entity, GameScene* scene) {
	(void)entity;

	if (!eventSubscribed_ && scene) {
		scene->GetEventSystem().Subscribe("GainSkillPoint", [this](float pts) { AddSkillPoints(static_cast<int>(pts)); });
		eventSubscribed_ = true;
	}

	if (!renderer_) {
		return;
	}

	// 初期化（テクスチャ）
	if (!initialized_) {
		texBg_ = renderer_->LoadTexture2D("Resources/Textures/SkillBack.png");
		texNodeLocked_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texNodeUnlocked_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texLine_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		texPrevArrow_ = renderer_->LoadTexture2D("Resources/Textures/left.png");
		texNextArrow_ = renderer_->LoadTexture2D("Resources/Textures/Right.png");
		texSkillPoint_ = renderer_->LoadTexture2D("Resources/Textures/SkillPoints.png");
		texNButton_ = renderer_->LoadTexture2D("Resources/Textures/Button/N.png");
		texPanel_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		scene->GetEventSystem().Subscribe("GainSkillPoint", [this](float pts) { skillPoints_ += static_cast<int>(pts); });

		initialized_ = true;
	}

	// JSON読み込み（1回だけ）
	if (!dataLoaded_) {
		LoadFromJson("Resources/Scenes/skills.json");
		dataLoaded_ = true;
	}
}

void SkillTree::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)dt;
	UpdatePageButtonRect(screenW_, screenH_);
	if (!scene) {
		return;
	}

	if (!isOpen_) {
		ClearText(scene);

		// ★追加: 準備フェーズ中にスキルツリーの存在を知らせるUI(N.png)をお金のUIの少し下に表示する
		if (renderer_ && PhaseSystemScript::IsPhase() == PhaseSystemScript::PhaseState::PreparationPhase && !PlayerScript::IsHelpOpen()) {
			Engine::Renderer::SpriteDesc s;
			// お金のUIの下あたりに配置 (1920x1080解像度を想定)
			s.x = screenW_ - 150.0f; // 右に寄せる
			s.y = 120.0f;
			s.w = 90.0f; // 大きくする
			s.h = 90.0f;
			s.color = {2.5f, 2.5f, 2.5f, 1.0f}; // 明るくする

			// スキルポイントがある時は、プレイヤーに気付かせるためにアニメーション(拡縮とカラー点滅)させる
			if (skillPoints_ > 0) {
				nButtonAnimTimer_ += dt;
				float scale = 1.0f + 0.3f * std::sin(nButtonAnimTimer_ * 15.0f); // より大きく、早くバウンスさせる
				s.w *= scale;
				s.h *= scale;
				
				// より激しく発光させる
				float colorAnim = std::max(0.0f, std::sin(nButtonAnimTimer_ * 20.0f));
				s.color = {2.5f + colorAnim * 2.0f, 2.5f + colorAnim * 2.0f, 0.5f + colorAnim, 1.0f};
				
				float tx = s.x - 80.0f; // 見切れないように大幅に左へずらす
				float ty = s.y + 60.0f;
				// 黒いアウトラインを描画
				renderer_->DrawString("Skill Point UP!", tx - 2.0f, ty, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Point UP!", tx + 2.0f, ty, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Point UP!", tx, ty - 2.0f, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Point UP!", tx, ty + 2.0f, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Point UP!", tx, ty, 0.6f, s.color);
			} else {
				float tx = s.x - 50.0f; // こちらも見切れないように左へ
				float ty = s.y + 60.0f;
				// 黒いアウトラインを描画
				renderer_->DrawString("Skill Tree", tx - 2.0f, ty, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Tree", tx + 2.0f, ty, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Tree", tx, ty - 2.0f, 0.6f, {0, 0, 0, 1});
				renderer_->DrawString("Skill Tree", tx, ty + 2.0f, 0.6f, {0, 0, 0, 1});
				// スキルポイントが無いときは静かに表示
				renderer_->DrawString("Skill Tree", tx, ty, 0.6f, {1.0f, 1.0f, 1.0f, 1.0f});
			}

			renderer_->DrawSprite(texNButton_, s);
		}

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
	DrawPageButtons(renderer_, screenW_, screenH_);
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

	// テキストコンポーネントの更新
	auto& registry = scene->GetRegistry();

	auto getOrCreateTextEntity = [&](entt::entity& e) {
		if (!registry.valid(e)) {
			e = registry.create();
			registry.emplace<NameComponent>(e, "SkillTreeText");
			auto& rect = registry.emplace<RectTransformComponent>(e);
			rect.size = {0.0f, 0.0f};
			rect.anchor = {0.0f, 0.0f}; // 左上基準にする
			rect.pivot = {0.0f, 0.0f};  // 左上基準にする
			registry.emplace<UITextComponent>(e);
		}
		return e;
	};

	bool showText = (hoveredIndex >= 0 && pendingUnlockId_ == -1);

	entt::entity titleE = getOrCreateTextEntity(titleTextEntity_);
	entt::entity costE = getOrCreateTextEntity(costTextEntity_);
	entt::entity descE = getOrCreateTextEntity(descTextEntity_);
	entt::entity statusE = getOrCreateTextEntity(statusTextEntity_);

	auto& titleComp = registry.get<UITextComponent>(titleE);
	auto& titleRect = registry.get<RectTransformComponent>(titleE);
	auto& costComp = registry.get<UITextComponent>(costE);
	auto& costRect = registry.get<RectTransformComponent>(costE);
	auto& descComp = registry.get<UITextComponent>(descE);
	auto& descRect = registry.get<RectTransformComponent>(descE);
	auto& statusComp = registry.get<UITextComponent>(statusE);
	auto& statusRect = registry.get<RectTransformComponent>(statusE);
	// ページタイトル用
	entt::entity pageTitleE = getOrCreateTextEntity(pageTitleEntity_);

	auto& pageComp = registry.get<UITextComponent>(pageTitleE);
	auto& pageRect = registry.get<RectTransformComponent>(pageTitleE);

	if (pendingUnlockId_ != -1) {
		float centerX = screenW_ * 0.5f;
		float centerY = screenH_ * 0.5f;

		entt::entity yesE = getOrCreateTextEntity(yesTextEntity_);
		entt::entity noE = getOrCreateTextEntity(noTextEntity_);

		UITextComponent& yesComp = registry.get<UITextComponent>(yesE);
		RectTransformComponent& yesRect = registry.get<RectTransformComponent>(yesE);

		UITextComponent& noComp = registry.get<UITextComponent>(noE);
		RectTransformComponent& noRect = registry.get<RectTransformComponent>(noE);
		entt::entity messageE = getOrCreateTextEntity(messageTextEntity_);

		UITextComponent& messageComp = registry.get<UITextComponent>(messageE);
		RectTransformComponent& messageRect = registry.get<RectTransformComponent>(messageE);

		const SkillNode& node = nodes_[pendingUnlockId_];

		std::string message = node.name + " を解放しますか？";

		std::vector<int> neededIndices;
		GetPrerequisites(pendingUnlockId_, neededIndices);

		if (neededIndices.size() > 1) {
			message = node.name + " と必要スキルを解放しますか？";
		}

		messageComp.text = message;
		messageComp.fontSize = 28.0f;
		messageComp.color = {1, 1, 1, 1};
		messageRect.pos = {centerX - 170.0f, centerY - 45.0f};
		yesComp.text = "YES";
		yesComp.fontSize = 18.0f;
		yesComp.color = {1, 1, 1, 1};
		yesRect.pos = {centerX - 75.0f, centerY + 28.0f};

		noComp.text = "NO";
		noComp.fontSize = 18.0f;
		noComp.color = {1, 1, 1, 1};
		noRect.pos = {centerX + 50.0f, centerY + 28.0f};

		DrawConfirmationDialog(renderer_, screenW_, screenH_);
	} else {
		if (registry.valid(yesTextEntity_)) {
			registry.get<UITextComponent>(yesTextEntity_).text = "";
		}

		if (registry.valid(noTextEntity_)) {
			registry.get<UITextComponent>(noTextEntity_).text = "";
		}
		if (registry.valid(messageTextEntity_)) {
			registry.get<UITextComponent>(messageTextEntity_).text = "";
		}
	}

	// タイトル決定
	std::string pageTitle = "";

	if (currentPageId_ == 0) {
		pageTitle = "Canon Skill";
	} else if (currentPageId_ == 1) {
		pageTitle = "Poison Skill";
	} else if (currentPageId_ == 2) {
		pageTitle = "Missile Skill";
	} else if (currentPageId_ == 3) {
		pageTitle = "IceCanon Skill";
	} else if (currentPageId_ == 4) {
		pageTitle = "Player Skill";
	}

	// UI反映
	pageComp.text = pageTitle;
	pageComp.fontSize = 70.0f;
	pageComp.color = {1, 1, 1, 1};

	// 上の方に表示
	pageRect.pos = {screenW_ * 0.5f - 150.0f, 120.0f};

	if (showText) {
		const SkillNode& node = nodes_[hoveredIndex];
		float panelWidth = 400.0f;
		float panelX = screenW_ - kPanelMargin - panelWidth;
		float panelY = kPanelMargin;

		titleComp.text = node.name;
		titleComp.fontSize = 50.0f;
		titleComp.color = {1, 1, 1, 1};
		titleRect.pos = {panelX + 20.0f, panelY + 30.0f};

		costComp.text = "Cost: " + std::to_string(node.cost) + " SP";
		costComp.fontSize = 40.0f;
		costComp.color = {0.8f, 0.8f, 0.8f, 1};
		costRect.pos = {panelX + 20.0f, panelY + 75.0f};

		descComp.text = node.description;
		descComp.fontSize = 30.0f;
		descComp.color = {0.9f, 0.9f, 0.9f, 1};
		descRect.pos = {panelX + 20.0f, panelY + 140.0f};

		if (node.unlocked) {
			statusComp.text = "[Unlocked]";
			statusComp.fontSize = 22.0f;
			statusComp.color = {0.2f, 1.0f, 0.4f, 1};
			statusRect.pos = {panelX + 20.0f, panelY + 200.0f};
		} else {
			statusComp.text = "";
		}
	} else {
		titleComp.text = "";
		costComp.text = "";
		descComp.text = "";
		statusComp.text = "";
	}

	if (pendingUnlockId_ != -1) {
		DrawConfirmationDialog(renderer_, screenW_, screenH_);
	}
}

void SkillTree::ClearText(GameScene* scene) {
	if (!scene) {
		return;
	}

	auto& registry = scene->GetRegistry();

	if (registry.valid(titleTextEntity_) && registry.all_of<UITextComponent>(titleTextEntity_)) {
		registry.get<UITextComponent>(titleTextEntity_).text = "";
	}

	if (registry.valid(costTextEntity_) && registry.all_of<UITextComponent>(costTextEntity_)) {
		registry.get<UITextComponent>(costTextEntity_).text = "";
	}

	if (registry.valid(descTextEntity_) && registry.all_of<UITextComponent>(descTextEntity_)) {
		registry.get<UITextComponent>(descTextEntity_).text = "";
	}

	if (registry.valid(statusTextEntity_) && registry.all_of<UITextComponent>(statusTextEntity_)) {
		registry.get<UITextComponent>(statusTextEntity_).text = "";
	}

	if (registry.valid(pageTitleEntity_) && registry.all_of<UITextComponent>(pageTitleEntity_)) {
		registry.get<UITextComponent>(pageTitleEntity_).text = "";
	}
}

void SkillTree::OnDestroy(entt::entity entity, GameScene* scene) {
	(void)entity;

	if (scene) {
		auto& registry = scene->GetRegistry();
		if (registry.valid(titleTextEntity_))
			registry.destroy(titleTextEntity_);
		if (registry.valid(costTextEntity_))
			registry.destroy(costTextEntity_);
		if (registry.valid(descTextEntity_))
			registry.destroy(descTextEntity_);
		if (registry.valid(statusTextEntity_))
			registry.destroy(statusTextEntity_);
	}

	titleTextEntity_ = entt::null;
	costTextEntity_ = entt::null;
	descTextEntity_ = entt::null;
	statusTextEntity_ = entt::null;

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
	// Canon
	float attackPowerRateCanon = 1.0f;
	float attackRangeRateCanon = 1.0f;
	float attackSpeedRateCanon = 1.0f;

	// CanonPower
	if (IsSkillUnlocked(1)) {
		attackPowerRateCanon *= 1.10f;
	}

	// CanonSpeed
	if (IsSkillUnlocked(2)) {
		attackSpeedRateCanon *= 1.10f;
	}

	// CanonRange
	if (IsSkillUnlocked(3)) {
		attackRangeRateCanon *= 1.10f;
	}

	// CanonPower2
	if (IsSkillUnlocked(4)) {
		attackPowerRateCanon *= 1.15f;
	}

	// CanonAllUp
	if (IsSkillUnlocked(5)) {
		attackPowerRateCanon *= 1.05f;
		attackSpeedRateCanon *= 1.05f;
		attackRangeRateCanon *= 1.05f;
	}

	// CanonSpeed2
	if (IsSkillUnlocked(6)) {
		attackSpeedRateCanon *= 1.10f;
	}

	// CanonCritical
	if (IsSkillUnlocked(7)) {
		attackPowerRateCanon *= 1.25f;
	}

	// CanonOverclock
	if (IsSkillUnlocked(8)) {
		attackSpeedRateCanon *= 1.20f;
	}

	// CanonMastery
	if (IsSkillUnlocked(9)) {
		attackPowerRateCanon *= 1.15f;
		attackSpeedRateCanon *= 1.15f;
		attackRangeRateCanon *= 1.15f;
	}

	// poisonTrap
	float attackPowerRatePoison = 1.0f;
	float attackRangeRatePoison = 1.0f;
	float poisonDurationRate = 1.0f;
	float poisonCooldownRate = 1.0f;

	// PoisonPower
	if (IsSkillUnlocked(101)) {
		attackPowerRatePoison *= 1.50f;
	}

	// PoisonRange
	if (IsSkillUnlocked(102)) {
		attackRangeRatePoison *= 1.50f;
	}

	// PoisonAllUp
	if (IsSkillUnlocked(103)) {
		attackPowerRatePoison *= 1.20f;
		attackRangeRatePoison *= 1.20f;
	}

	// PoisonDuration
	if (IsSkillUnlocked(104)) {
		poisonDurationRate *= 1.30f;
	}

	// PoisonCooldown
	if (IsSkillUnlocked(105)) {
		poisonCooldownRate *= 0.80f;
	}

	// PoisonMastery
	if (IsSkillUnlocked(106)) {
		attackPowerRatePoison *= 1.25f;
		attackRangeRatePoison *= 1.25f;
		poisonDurationRate *= 1.15f;
	}

	// Misiile
	float attackPowerRateMisile = 1.0f;
	float attackAreaRateMisile = 1.0f;
	float missileCooldownRate = 1.0f;
	// page3
	if (IsSkillUnlocked(201)) {
		attackPowerRateMisile *= 1.50f;
	}
	if (IsSkillUnlocked(202)) {
		attackAreaRateMisile *= 1.50f;
	}

	// MissileAllUp
	if (IsSkillUnlocked(203)) {
		attackPowerRateMisile *= 1.15f;
		attackAreaRateMisile *= 1.15f;
		missileCooldownRate *= 0.90f;
	}
	// MissilePower2
	if (IsSkillUnlocked(204)) {
		attackPowerRateMisile *= 1.30f;
	}

	// MissileExplosion2
	if (IsSkillUnlocked(205)) {
		attackAreaRateMisile *= 1.30f;
	}
	// MissileMastery
	if (IsSkillUnlocked(206)) {
		attackPowerRateMisile *= 1.30f;
		attackAreaRateMisile *= 1.30f;
		missileCooldownRate *= 0.80f;
	}

	// IceCanon
	float attackPowerRateIceCanon = 1.0f;
	float attackRangeRateIceCanon = 1.0f;
	float attackSpeedRateIceCanon = 1.0f;
	float stopTimeRateIceCanon = 1.0f;
	float bulletCountRateIceCanon = 1.0f;

	// page4
	if (IsSkillUnlocked(301)) {
		attackPowerRateIceCanon *= 1.50f;
	}

	if (IsSkillUnlocked(302)) {
		attackRangeRateIceCanon *= 1.50f;
		attackPowerRateIceCanon *= 1.20f;
	}

	if (IsSkillUnlocked(303)) {
		stopTimeRateIceCanon *= 1.50f;
	}

	if (IsSkillUnlocked(304)) {
		bulletCountRateIceCanon *= 1.50f;
	}

	if (IsSkillUnlocked(305)) {
		attackSpeedRateIceCanon *= 1.20f;
	}

	if (IsSkillUnlocked(306)) {
		attackPowerRateIceCanon *= 2.50f;
		attackRangeRateIceCanon *= 1.50f;
		stopTimeRateIceCanon *= 1.50f;
	}
	// page4
	//   Player
	// Player
	float playerMoveSpeedRate = 1.0f; // 移動速度倍率

	float playerSwordAttackSpeedRate = 1.0f; // 剣攻撃速度倍率
	float playerGunAttackSpeedRate = 1.0f;   // 銃攻撃速度倍率

	float playerSwordAttackPowerRate = 1.0f; // 剣攻撃力倍率
	float playerGunAttackPowerRate = 1.0f;   // 銃攻撃力倍率

	float playerMaxSteamPressureRate = 1.0f; // 最大蒸気圧倍率

	float playerSwordSkillCooldownRate = 1.0f; // 剣スキルクールダウン倍率
	float playerGunSkillCooldownRate = 1.0f;   // 銃スキルクールダウン倍率

	float playerSwordSkillAttackPowerRate = 1.0f; // 剣スキル攻撃力倍率
	float playerGunSkillAttackPowerRate = 1.0f;   // 銃スキル攻撃力倍率

	float buffRadiusRate = 1.0f;

	if (IsSkillUnlocked(401)) {
		playerGunAttackPowerRate *= 1.20f;
	}

	if (IsSkillUnlocked(402)) {
		playerSwordAttackPowerRate *= 1.20f;
	}

	if (IsSkillUnlocked(403)) {
		playerMoveSpeedRate *= 1.10f;
		playerGunAttackPowerRate *= 1.10f;
		playerSwordAttackPowerRate *= 1.10f;
		playerMaxSteamPressureRate *= 1.10f;
	}

	if (IsSkillUnlocked(404)) {
		buffRadiusRate *= 1.5f;
	}

	if (IsSkillUnlocked(405)) {
		playerSwordAttackPowerRate *= 1.25f;
	}

	if (IsSkillUnlocked(406)) {
		playerMaxSteamPressureRate *= 0.85f;
	}

	if (IsSkillUnlocked(407)) {
		playerGunAttackSpeedRate *= 1.15f;
	}

	if (IsSkillUnlocked(408)) {
		playerSwordAttackSpeedRate *= 1.15f;
	}

	if (IsSkillUnlocked(409)) {
		playerMoveSpeedRate *= 1.15f;
	}

	if (IsSkillUnlocked(410)) {
		playerGunSkillCooldownRate *= 0.85f;
	}

	if (IsSkillUnlocked(411)) {
		playerSwordSkillCooldownRate *= 0.85f;
	}

	if (IsSkillUnlocked(412)) {
		playerMoveSpeedRate *= 1.10f;
		playerGunAttackSpeedRate *= 1.10f;
		playerSwordAttackSpeedRate *= 1.10f;
	}

	if (IsSkillUnlocked(413)) {
		playerGunSkillAttackPowerRate *= 1.30f;
	}

	if (IsSkillUnlocked(414)) {
		playerSwordSkillAttackPowerRate *= 1.50f;
	}

	// player
	SetVar(entity, scene, "PlayerMoveSpeedRate", playerMoveSpeedRate);

	SetVar(entity, scene, "PlayerSwordAttackSpeedRate", playerSwordAttackSpeedRate);
	SetVar(entity, scene, "PlayerGunAttackSpeedRate", playerGunAttackSpeedRate);

	SetVar(entity, scene, "PlayerSwordAttackPowerRate", playerSwordAttackPowerRate);
	SetVar(entity, scene, "PlayerGunAttackPowerRate", playerGunAttackPowerRate);

	SetVar(entity, scene, "PlayerMaxSteamPressureRate", playerMaxSteamPressureRate);

	SetVar(entity, scene, "PlayerSwordSkillCooldownRate", playerSwordSkillCooldownRate);
	SetVar(entity, scene, "PlayerGunSkillCooldownRate", playerGunSkillCooldownRate);

	SetVar(entity, scene, "PlayerSwordSkillAttackPowerRate", playerSwordSkillAttackPowerRate);
	SetVar(entity, scene, "PlayerGunSkillAttackPowerRate", playerGunSkillAttackPowerRate);

	SetVar(entity, scene, "PlayerBuffRangeRate", buffRadiusRate);
	// canon
	SetVar(entity, scene, "AttackPowerRateCanon", attackPowerRateCanon);
	SetVar(entity, scene, "AttackSpeedRateCanon", attackSpeedRateCanon);
	SetVar(entity, scene, "AttackRangeRateCanon", attackRangeRateCanon);
	// poisonTrap
	SetVar(entity, scene, "AttackPowerRatePoison", attackPowerRatePoison);
	SetVar(entity, scene, "AttackRangeRatePoison", attackRangeRatePoison);
	SetVar(entity, scene, "PoisonDurationRate", poisonDurationRate);
	SetVar(entity, scene, "PoisonCooldownRate", poisonCooldownRate);
	// misile
	SetVar(entity, scene, "AttackPowerRateMisile", attackPowerRateMisile);
	SetVar(entity, scene, "AttackAreaRateMisile", attackAreaRateMisile);
	SetVar(entity, scene, "MissileCooldownRate", missileCooldownRate);
	// iceCanon
	SetVar(entity, scene, "AttackPowerRateIceCanon", attackPowerRateIceCanon);
	SetVar(entity, scene, "AttackRangeRateIceCanon", attackRangeRateIceCanon);
	SetVar(entity, scene, "AttackSpeedRateIceCanon", attackSpeedRateIceCanon);
	SetVar(entity, scene, "StopTimeRateIceCanon", stopTimeRateIceCanon);
	SetVar(entity, scene, "BulletCountRateIceCanon", bulletCountRateIceCanon);
}

void SkillTree::HandleInput(float screenW, float screenH, float mouseX, float mouseY) {
	Engine::Input* input = Engine::Input::GetInstance();
	if (!input) {
		return;
	}

	if (!input->IsMouseTrigger(0)) {
		return;
	}

	// 解放確認中はページ変更禁止
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

	// 確認ダイアログ出てない時だけページ変更
	if (HandlePageButtonInput(screenW, screenH, mouseX, mouseY)) {
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

bool SkillTree::HandlePageButtonInput(float screenW, float screenH, float mouseX, float mouseY) {
	(void)screenW;
	(void)screenH;

	Engine::Input* input = Engine::Input::GetInstance();
	if (!input) {
		return false;
	}

	if (!input->IsMouseTrigger(0)) {
		return false;
	}

	if (mouseX >= prevButtonLeft_ && mouseX <= prevButtonRight_ && mouseY >= prevButtonTop_ && mouseY <= prevButtonBottom_) {
		if (auto* tutorial = TutorialScript::GetInstance()) {
			if (tutorial->GetCurrentStep() == TutorialScript::TutorialStep::Step13_SkillTree) {
				return false; // チュートリアル中はページ変更禁止
			}
		}
		if (currentPageId_ > 0) {
			currentPageId_ -= 1;
			if (auto* audio = Engine::Audio::GetInstance()) {
				static uint32_t s_turnOverSeHandle = 0xFFFFFFFF;
				if (s_turnOverSeHandle == 0xFFFFFFFF) {
					s_turnOverSeHandle = audio->Load("Resources/Audio/SE/TurnOver.mp3");
				}
				if (s_turnOverSeHandle != 0xFFFFFFFF) {
					audio->Play(s_turnOverSeHandle, false, 0.9f * audio->GetMasterSEVolume());
				}
			}
		}
		return true;
	}

	if (mouseX >= nextButtonLeft_ && mouseX <= nextButtonRight_ && mouseY >= nextButtonTop_ && mouseY <= nextButtonBottom_) {
		if (auto* tutorial = TutorialScript::GetInstance()) {
			if (tutorial->GetCurrentStep() == TutorialScript::TutorialStep::Step13_SkillTree) {
				return false; // チュートリアル中はページ変更禁止
			}
		}
		if (currentPageId_ < pageCount_ - 1) {
			currentPageId_ += 1;
			if (auto* audio = Engine::Audio::GetInstance()) {
				static uint32_t s_turnOverSeHandle = 0xFFFFFFFF;
				if (s_turnOverSeHandle == 0xFFFFFFFF) {
					s_turnOverSeHandle = audio->Load("Resources/Audio/SE/TurnOver.mp3");
				}
				if (s_turnOverSeHandle != 0xFFFFFFFF) {
					audio->Play(s_turnOverSeHandle, false, 0.6f * audio->GetMasterSEVolume());
				}
			}
		}
		return true;
	}

	return false;
}

bool SkillTree::TryUnlockSkill(int index) {
	if (index < 0 || index >= (int)nodes_.size()) {
		return false;
	}

	if (nodes_[index].unlocked) {
		return false;
	}

	if (auto* tutorial = TutorialScript::GetInstance()) {
		if (tutorial->GetCurrentStep() == TutorialScript::TutorialStep::Step13_SkillTree) {
			// チュートリアル中は id == 1 (一番左の基本的なキャノンスキル) のみ選べるようにする
			if (nodes_[index].id != 1) {
				return false;
			}
		}
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
		if (auto* audio = Engine::Audio::GetInstance()) {
			static uint32_t s_levelUpSeHandle = 0xFFFFFFFF;
			if (s_levelUpSeHandle == 0xFFFFFFFF) {
				s_levelUpSeHandle = audio->Load("Resources/Audio/SE/LevelUp.mp3");
			}
			if (s_levelUpSeHandle != 0xFFFFFFFF) {
				audio->Play(s_levelUpSeHandle, false, 0.8f * audio->GetMasterSEVolume());
			}
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
	bg.color = {0.55f, 0.40f, 0.22f, 1.0f};
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

		float drawSize = kNodeSize;

		Engine::Vector4 bgColor;
		Engine::Vector4 iconColor = {1.0f, 1.0f, 1.0f, 1.0f}; // アイコン自体は本来の明るい色(白)を維持

		if (node.unlocked) {
			bgColor = {0.2f, 0.85f, 0.3f, 1.0f}; // 解放済み：緑の枠
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
				bool isTutorialRestricted = false;
				if (auto* tutorial = TutorialScript::GetInstance()) {
					if (tutorial->GetCurrentStep() == TutorialScript::TutorialStep::Step13_SkillTree) {
						if (node.id != 1) {
							isTutorialRestricted = true;
						}
					}
				}

				if (isTutorialRestricted) {
					bgColor = {0.3f, 0.3f, 0.3f, 0.7f};   // ロック中扱い
					iconColor = {0.4f, 0.4f, 0.4f, 1.0f}; // アイコンも暗く
				} else {
					if (isHovered) {
						bgColor = {1.0f, 0.9f, 0.3f, 1.0f}; // 解放可能(ホバー)：明るい黄色の枠
					} else {
						bool isTutorialTarget = false;
						if (auto* tutorial = TutorialScript::GetInstance()) {
							if (tutorial->GetCurrentStep() == TutorialScript::TutorialStep::Step13_SkillTree && node.id == 1) {
								isTutorialTarget = true;
							}
						}

						if (isTutorialTarget) {
							float blink = std::sin((float)GetTickCount64() * 0.01f) * 0.5f + 0.5f;
							bgColor = {1.0f, 0.5f + 0.5f * blink, 0.0f, 1.0f}; // チュートリアル中は対象を点滅させる
						} else {
							bgColor = {0.8f, 0.7f, 0.2f, 0.9f}; // 解放可能：暗い黄色の枠
						}
					}
				}
			} else {
				bgColor = {0.3f, 0.3f, 0.3f, 0.7f};   // ロック中：灰色の枠
				iconColor = {0.4f, 0.4f, 0.4f, 1.0f}; // ロック中のアイコンは暗くする
			}


			if (isHovered) {
				drawSize *= 1.15f;
			}
			bool canUnlockAnimation = false;

			if (!node.unlocked && canUnlock && skillPoints_ >= node.cost) {
				canUnlockAnimation = true;
			}

			if (canUnlockAnimation) {

				float pulse = std::sin((float)GetTickCount64() * 0.005f);

				drawSize += pulse * 6.0f;
			}
		}
		
		// ★追加: 状態を示すカラーを「背景の枠」として描画する
		Engine::Renderer::SpriteDesc bgSprite;
		bgSprite.x = nodeX - drawSize * 0.5f - 4.0f;
		bgSprite.y = nodeY - drawSize * 0.5f - 4.0f;
		bgSprite.w = drawSize + 8.0f;
		bgSprite.h = drawSize + 8.0f;
		bgSprite.color = bgColor;
		// texPanel_ (または無地の白テクスチャ) を使って枠を描画
		renderer->DrawSprite(texPanel_, bgSprite);

		// ★修正: アイコン自体は本来の色(iconColor)で描画する
		Engine::Renderer::SpriteDesc sprite;
		sprite.x = nodeX - drawSize * 0.5f;
		sprite.y = nodeY - drawSize * 0.5f;
		sprite.w = drawSize;
		sprite.h = drawSize;
		sprite.color = iconColor;

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

#pragma region PageButtons
void SkillTree::DrawSkillPointsText(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;

	float baseX = kPanelMargin + 40.0f;
	float baseY = screenH - kPanelMargin - 70.0f;

	std::string text = "スキルポイント " + std::to_string(skillPoints_);
	float outline = 2.0f;
	std::string font = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";

	renderer->DrawString(text, baseX - outline, baseY, 0.6f, {0.0f, 0.0f, 0.0f, 1.0f}, font);
	renderer->DrawString(text, baseX + outline, baseY, 0.6f, {0.0f, 0.0f, 0.0f, 1.0f}, font);
	renderer->DrawString(text, baseX, baseY - outline, 0.6f, {0.0f, 0.0f, 0.0f, 1.0f}, font);
	renderer->DrawString(text, baseX, baseY + outline, 0.6f, {0.0f, 0.0f, 0.0f, 1.0f}, font);
	renderer->DrawString(text, baseX, baseY, 0.6f, {1.0f, 1.0f, 1.0f, 1.0f}, font);
}

#pragma endregion

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
	renderer->DrawSprite(texPanel_, bg);

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
	renderer->DrawSprite(texPanel_, border);

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
	// テキストは UITextComponent で描画するため、ここでは描画しません。
	// ImGui での描画処理は削除されました。
#endif
}

void SkillTree::DrawConfirmationDialog(Engine::Renderer* renderer, float screenW, float screenH) {
	if (pendingUnlockId_ < 0 || pendingUnlockId_ >= (int)nodes_.size()) {
		return;
	}

	// const SkillNode& node = nodes_[pendingUnlockId_];

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
	renderer->DrawSprite(texPanel_, bg);

	Engine::Renderer::SpriteDesc border;
	border.x = centerX - dialogWidth * 0.5f;
	border.y = centerY - dialogHeight * 0.5f;
	border.w = dialogWidth;
	border.h = 2.0f;
	border.color = {1.0f, 0.8f, 0.2f, 1.0f};
	renderer->DrawSprite(texPanel_, border);

#ifdef USE_IMGUI
	/*ImDrawList* drawList = ImGui::GetBackgroundDrawList();
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
	}*/
#endif

	Engine::Renderer::SpriteDesc yesButton;
	yesButton.x = centerX - 110.0f;
	yesButton.y = centerY + 20.0f;
	yesButton.w = 100.0f;
	yesButton.h = 40.0f;
	yesButton.color = {0.2f, 0.7f, 0.3f, 0.8f};
	renderer->DrawSprite(texPanel_, yesButton);

	Engine::Renderer::SpriteDesc noButton;
	noButton.x = centerX + 10.0f;
	noButton.y = centerY + 20.0f;
	noButton.w = 100.0f;
	noButton.h = 40.0f;
	noButton.color = {0.8f, 0.2f, 0.2f, 0.8f};
	renderer->DrawSprite(texPanel_, noButton);
}
void SkillTree::UpdatePageButtonRect(float screenW, float screenH) {
	float padding = 20.0f;
	float buttonWidth = 80.0f;
	float buttonHeight = 50.0f;
	float offsetY = 770.0f;
	float paddingX = 40.0f;
	// 左ボタン
	prevButtonLeft_ = kPanelMargin + padding + paddingX;
	prevButtonTop_ = screenH - kPanelMargin - padding - buttonHeight - offsetY;
	prevButtonRight_ = prevButtonLeft_ + buttonWidth;
	prevButtonBottom_ = prevButtonTop_ + buttonHeight;

	// 右ボタン
	nextButtonLeft_ = screenW - kPanelMargin - padding - buttonWidth - paddingX;
	nextButtonTop_ = screenH - kPanelMargin - padding - buttonHeight - offsetY;
	nextButtonRight_ = nextButtonLeft_ + buttonWidth;
	nextButtonBottom_ = nextButtonTop_ + buttonHeight;
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

void SkillTree::DrawPageButtons(Engine::Renderer* renderer, float screenW, float screenH) {
	(void)screenW;
	(void)screenH;

float arrowAnim = std::sin((float)GetTickCount64() * 0.005f) * 10.0f;

	Engine::Renderer::SpriteDesc prev;
	prev.x = prevButtonLeft_ - arrowAnim;
	prev.y = prevButtonTop_;
	prev.w = prevButtonRight_ - prevButtonLeft_;
	prev.h = prevButtonBottom_ - prevButtonTop_;

	if (currentPageId_ <= 0) {
		prev.color = {0.25f, 0.25f, 0.25f, 0.5f};
	} else {
		prev.color = {4.0f, 1.8f, 1.2f, 1.0f};
	}
	// 前ページ戻る
	renderer->DrawSprite(texPrevArrow_, prev);

Engine::Renderer::SpriteDesc next;
	next.x = nextButtonLeft_ + arrowAnim;
	next.y = nextButtonTop_;
	next.w = nextButtonRight_ - nextButtonLeft_;
	next.h = nextButtonBottom_ - nextButtonTop_;

	if (currentPageId_ >= pageCount_ - 1) {
		next.color = {0.25f, 0.25f, 0.25f, 0.5f};
	} else {
		next.color = {4.0f, 3.5f, 2.5f, 1.0f};
	}
	// 次ページ進む
	renderer->DrawSprite(texNextArrow_, next);

#ifdef USE_IMGUI
	// ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	// if (drawList) {
	//	drawList->AddText(ImVec2(prevButtonLeft_ + 35.0f, prevButtonTop_ + 20.0f), IM_COL32(255, 255, 255, 255), "<");
	//	drawList->AddText(ImVec2(nextButtonLeft_ + 35.0f, nextButtonTop_ + 20.0f), IM_COL32(255, 255, 255, 255), ">");

	//	std::string pageText = "Page " + std::to_string(currentPageId_ + 1) + " / " + std::to_string(pageCount_);
	//	drawList->AddText(ImVec2(screenW * 0.5f - 50.0f, screenH - 80.0f), IM_COL32(255, 255, 255, 255), pageText.c_str());
	//}
#endif
}

REGISTER_SCRIPT(SkillTree);

} // namespace Game