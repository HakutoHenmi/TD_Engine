#include "InstallationManager.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "PlayerScript.h" // ★追加
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "TutorialScript.h"

#if defined(USE_IMGUI) && !defined(NDEBUG)
#include "Editor/EditorUI.h"
#include <imgui.h>
#endif

#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <dinput.h>

using json = nlohmann::json;

namespace Game {

InstallationManager* InstallationManager::instance_ = nullptr;

InstallationManager::InstallationManager() {
	instance_ = this;
	currentPage_ = 0;

	// 削除機能用ボタンの設定
	prefabPaths_[0] = "";
	texPaths_[0] = "Resources/Textures/Button/DeleteButton.png";
	buttons_[0].name = "DeleteButton";
	buttons_[0].cost = 0;

	// ★ここでパスを直接設定してください（ImGuiでの入力によるクラッシュを回避）
	prefabPaths_[1] = "Resources/Prefabs/NewCannon.prefab";
	texPaths_[1] = "Resources/Textures/Button/CannonButton.png";
	buttons_[1].name = "CannonButton";
	buttons_[1].cost = 150;

	prefabPaths_[2] = "Resources/Prefabs/Missile.prefab";
	texPaths_[2] = "Resources/Textures/Button/MissileButton.png";
	buttons_[2].name = "MissikeButton";
	buttons_[2].cost = 200;

	prefabPaths_[3] = "Resources/Prefabs/Poison.prefab";
	texPaths_[3] = "Resources/Textures/Button/PisonTrapButton.png";
	buttons_[3].name = "PoisonTrapButton";
	buttons_[3].cost = 120;

	prefabPaths_[4] = "Resources/Prefabs/IceCanon.prefab";
	texPaths_[4] = "Resources/Textures/Button/IceCannonButton.png";
	buttons_[4].name = "IceCannonButton";
	buttons_[4].cost = 250;
}

void InstallationManager::Start(entt::entity /*entity*/, GameScene* scene) {
	instance_ = this;
	currentScene_ = scene;
	if (!scene)
		return;

	auto& registry = scene->GetRegistry();
	panelTexture_ = scene->GetRenderer()->LoadTexture2D("Resources/Textures/white1x1.png");
	// 古い/重複したボタンエンティティを一掃する（二重描画バグの修正）
	std::vector<entt::entity> toDestroy;
	for (auto e : registry.view<NameComponent, UIButtonComponent>()) {
		const auto& name = registry.get<NameComponent>(e).name;
		if (name == "DeleteButton" || name == "CannonButton" || name == "MissikeButton" || name == "PoisonTrapButton" || name == "IceCannonButton" || name == "TankButton" || name == "PipeButton" ||
		    name.find("Button_") != std::string::npos) {
			toDestroy.push_back(e);
		}
	}
	for (auto e : toDestroy) {
		registry.destroy(e);
	}

	for (int i = 0; i < 5; ++i) {
		auto& btn = buttons_[i];
		if (btn.name.empty()) {
			btn.name = "Button_" + std::to_string(i);
		}
		if (btn.texturePath.empty()) {
			btn.texturePath = "Resources/Textures/white1x1.png";
		}
		btn.entity = entt::null; // Updateで新しく生成させる
	}

	descriptionTextEntity_ = scene->CreateEntity("DescriptionText");
	auto& textRect = registry.emplace<RectTransformComponent>(descriptionTextEntity_);

	textRect.pos = {50.0f, 450.0f};
	textRect.size = {400.0f, 200.0f};

	textRect.anchor = {0.0f, 0.0f};
	textRect.pivot = {0.0f, 0.0f};
	auto& text = registry.emplace<UITextComponent>(descriptionTextEntity_);

	text.text = "";

	text.fontSize = 32.0f;

	text.color = {1, 1, 1, 1};

	text.outlineEnabled = true;
}

void InstallationManager::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	if (instance_ == this) {
		instance_ = nullptr;
	}
}

void InstallationManager::Update(entt::entity /*entity*/, GameScene* scene, float dt) {
	(void)dt;
	if (!scene)
		return;
	currentScene_ = scene;
	instance_ = this;

	if (scene->IsPaused()) {
		auto& reg = scene->GetRegistry();
		for (int i = 0; i < 5; ++i) {
			auto& btn = buttons_[i];
			if (reg.valid(btn.entity)) {
				if (reg.all_of<UIImageComponent>(btn.entity))
					reg.get<UIImageComponent>(btn.entity).enabled = false;
				if (reg.all_of<UIButtonComponent>(btn.entity))
					reg.get<UIButtonComponent>(btn.entity).enabled = false;
				if (reg.all_of<RectTransformComponent>(btn.entity))
					reg.get<RectTransformComponent>(btn.entity).enabled = false;
			}
		}
		if (reg.valid(descriptionTextEntity_) && reg.all_of<UITextComponent>(descriptionTextEntity_)) {
			reg.get<UITextComponent>(descriptionTextEntity_).text = "";
		}
		return;
	}

	// ページ切り替え処理
	auto input = Engine::Input::GetInstance();
	if (input && !input->IsGameInputBlocked()) {
		int totalPages = 2; // 6ボタン、3個ずつ固定
		if (input->Trigger(DIK_LEFT)) {
			currentPage_ = (currentPage_ > 0) ? currentPage_ - 1 : (totalPages - 1);
		}
		if (input->Trigger(DIK_RIGHT)) {
			currentPage_ = (currentPage_ + 1) % totalPages;
		}
	}

	auto& registry = scene->GetRegistry();
	auto currentPhase = PhaseSystemScript::IsPhase();

	for (int i = 0; i < 5; ++i) {
		auto& btn = buttons_[i];

		// パスの同期（コードで設定した変数から反映）
		btn.texturePath = texPaths_[i];
		btn.prefabPath = prefabPaths_[i];

		if (!registry.valid(btn.entity)) {
			EnsureButtonEntity(btn, scene);
			if (!registry.valid(btn.entity))
				continue;
		}

		// サイズと位置の自動設定
		btn.size = {160.0f, 160.0f}; // ★変更: 140だと小さすぎたので160に拡大

		btn.pos.y = 300.0f; // ★変更: 右下のHPゲージと被らないよう、Y位置を上に上げて回避

		bool isTutorialSoloButton = false;
		{
			bool isTutScene = false;
			if (scene) {
				const auto& path = scene->GetStagePath();
				if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos)
					isTutScene = true;
			}
			if (isTutScene) {
				if (auto* tutorial = TutorialScript::GetInstance()) {
					auto step = tutorial->GetCurrentStep();
					if (step == TutorialScript::TutorialStep::Step6_CannonInstall || step == TutorialScript::TutorialStep::Step7_DeleteIntro) {
						isTutorialSoloButton = true;
					}
				}
			}
		}

		if (isTutorialSoloButton) {
			if (btn.name == "DeleteButton") {
				btn.pos.x = -180.0f; // 大砲の左隣
			} else {
				btn.pos.x = 0.0f; // 中央へ
			}
		} else {
			btn.pos.x = (i - 2.0f) * 180.0f; // ★変更: サイズ拡大に合わせて間隔を180に
		}

		// 状態の更新
		if (registry.all_of<UIButtonComponent>(btn.entity)) {
			btn.isPressed = registry.get<UIButtonComponent>(btn.entity).isPressed;
		}

		// テクスチャと色の同期
		if (registry.all_of<UIImageComponent>(btn.entity)) {
			auto& img = registry.get<UIImageComponent>(btn.entity);
			if (img.texturePath != btn.texturePath) {
				img.texturePath = btn.texturePath;
				if (scene->GetRenderer()) {
					img.textureHandle = scene->GetRenderer()->LoadTexture2D(btn.texturePath);
				}
			}
			// ★追加: チュートリアルで注目させるボタンを光らせる
			bool isHighlighted = false;
			{
				bool isTutScene = false;
				if (scene) {
					const auto& path = scene->GetStagePath();
					if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos)
						isTutScene = true;
				}
				if (isTutScene) {
					if (auto* tutorial = TutorialScript::GetInstance()) {
						auto step = tutorial->GetCurrentStep();
						if (step == TutorialScript::TutorialStep::Step6_CannonInstall && btn.name == "CannonButton") {
							isHighlighted = true;
						} else if (step == TutorialScript::TutorialStep::Step7_DeleteIntro && btn.name == "DeleteButton") {
							isHighlighted = true;
						}
					}
				}
			}

			if (isHighlighted) {
				img.color = {2.5f, 2.5f, 2.5f, 1.0f}; // RGBを強めにブーストして光らせる
			} else if (btn.name == "DeleteButton") {
				img.color = {1.5f, 1.5f, 1.5f, 1.0f}; // 通常時もDeleteは少し明るめ
			} else {
				img.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 通常のボタンは等倍
			}
		}

		// RectTransformの同期
		if (registry.all_of<RectTransformComponent>(btn.entity)) {
			auto& rect = registry.get<RectTransformComponent>(btn.entity);
			rect.pos = btn.pos;
			rect.size = btn.size;
			rect.anchor = {0.5f, 0.5f};
			rect.pivot = {0.5f, 0.5f};
		}

		// フェーズに応じた表示・非表示の管理（ページ制限を廃止）

		bool enabled = ((!scene->IsPlaying()) || (currentPhase == PhaseSystemScript::PreparationPhase));
		if (PhaseSystemScript::isSkillTreeOpen_) {
			enabled = false;
		}
		// チュートリアル中の表示制御 (チュートリアルシーンでのみ適用)
		{
			bool isTutScene = false;
			if (scene) {
				const auto& path = scene->GetStagePath();
				if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos)
					isTutScene = true;
			}
			if (isTutScene) {
				if (auto* tutorial = TutorialScript::GetInstance()) {
					auto step = tutorial->GetCurrentStep();

					// ステップごとに許可するボタンを限定
					if (step >= TutorialScript::TutorialStep::Step1_Greeting && step < TutorialScript::TutorialStep::Step14_EndExplanation) {
						enabled = false; // 基本はすべて非表示
						if (step == TutorialScript::TutorialStep::Step6_CannonInstall) {
							if (btn.prefabPath == "Resources/Prefabs/NewCannon.prefab")
								enabled = true;
						} else if (step == TutorialScript::TutorialStep::Step7_DeleteIntro) {
							if (btn.prefabPath == "Resources/Prefabs/NewCannon.prefab")
								enabled = true;
							if (btn.name == "DeleteButton")
								enabled = true;
						}
					}
				}
			}
		}

		if (registry.all_of<UIImageComponent>(btn.entity))
			registry.get<UIImageComponent>(btn.entity).enabled = enabled;
		if (registry.all_of<UIButtonComponent>(btn.entity))
			registry.get<UIButtonComponent>(btn.entity).enabled = enabled;
		if (registry.all_of<RectTransformComponent>(btn.entity))
			registry.get<RectTransformComponent>(btn.entity).enabled = enabled;
	}
	isDescriptionVisible_ = false;

	if (!PlayerScript::IsHelpOpen()) {
		for (int i = 0; i < 5; i++) {

			auto& btn = buttons_[i];

			if (!registry.valid(btn.entity)) {
				continue;
			}

			if (!registry.all_of<UIButtonComponent>(btn.entity)) {
				continue;
			}

			UIButtonComponent& button = registry.get<UIButtonComponent>(btn.entity);

		if (button.isHovered && button.enabled) {

				isDescriptionVisible_ = true;
				hoveredButtonIndex_ = i;
				break;
			}
		}
	}
	float targetX = -500.0f;
	UITextComponent& text = registry.get<UITextComponent>(descriptionTextEntity_);
	if (hoveredButtonIndex_ == 0) {
		text.text = "施設を削除\n"
		            "設置済みの施設を\n"
		            "削除します";
	}

	if (hoveredButtonIndex_ == 1) {
		text.text = "標準的な大砲\n"
		            "最も基本的な防衛施設\n"
		            "敵を自動的に攻撃します";
	}

	if (hoveredButtonIndex_ == 2) {
		text.text = "ミサイル砲台\n"
		            "着弾地点で爆発\n"
		            "複数の敵をまとめて攻撃";
	}
	if (hoveredButtonIndex_ == 3) {
		text.text = "毒トラップ\n"
		            "敵に継続ダメージを与える\n"
		            "通路の要所に有効";
	}
	if (hoveredButtonIndex_ == 4) {
		text.text = "アイスキャノン\n"
		            "敵の動きを凍結\n"
		            "進軍速度を大きく低下";
	}
	if (!isDescriptionVisible_) {
		text.text = "";
	}
	if (isDescriptionVisible_) {
		targetX = 50.0f;
	}

	if (isDescriptionVisible_) {
		descriptionPanelX_ = 50.0f;
	} else {
		descriptionPanelX_ = -500.0f;
	}

	Engine::Renderer::SpriteDesc panel;

	panel.x = descriptionPanelX_;
	panel.y = 400.0f;

	panel.w = 400.0f;
	panel.h = 500.0f;

	panel.color = {0.1f, 0.1f, 0.1f, 0.9f};

	scene->GetRenderer()->DrawSprite(panelTexture_, panel);
}
void InstallationManager::Draw(entt::entity /*entity*/, GameScene* /*scene*/) {
	// UISystem が RectTransformComponent を通じて描画するため、
	// ここでの二重描画は不要。エディタでも UISystem は動作する。
}

void InstallationManager::DrawUI(entt::entity /*entity*/, GameScene* scene) {

	if (!scene || scene->IsPaused()) {
		return;
	}

	auto* renderer = scene->GetRenderer();

	if (!renderer) {
		return;
	}

	float targetX = -500.0f;

	if (isDescriptionVisible_) {
		targetX = 0.0f;
	}

	descriptionPanelX_ += (targetX - descriptionPanelX_) * 0.1f;

	Engine::Renderer::SpriteDesc panel;

	panel.x = descriptionPanelX_;
	panel.y = 100.0f;

	panel.w = 400.0f;
	panel.h = 500.0f;

	panel.color = {0.1f, 0.1f, 0.1f, 0.9f};

	renderer->DrawSprite(panelTexture_, panel);
}

void InstallationManager::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Installation Manager");

	ImGui::Text("Fixed 5 Buttons Mode");
	ImGui::Separator();

	for (int i = 0; i < 5; ++i) {
		ImGui::PushID(static_cast<int>(i));
		std::string label = buttons_[i].name + " (##" + std::to_string(i) + ")";
		if (ImGui::TreeNode(label.c_str())) {
			char nameBuf[1024] = {0};
			strncpy_s(nameBuf, buttons_[i].name.c_str(), _TRUNCATE);
			if (ImGui::InputText("Name (Link to Entity)", nameBuf, sizeof(nameBuf))) {
				buttons_[i].name = nameBuf;
			}

			ImGui::Text("Texture: %s", texPaths_[i].c_str());
			ImGui::Text("Prefab: %s", prefabPaths_[i].c_str());
			ImGui::Separator();

			ImGui::InputInt("Cost", &buttons_[i].cost);

			float p[2] = {buttons_[i].pos.x, buttons_[i].pos.y};
			if (ImGui::DragFloat2("Position", p)) {
				buttons_[i].pos = {p[0], p[1]};
			}

			float s[2] = {buttons_[i].size.x, buttons_[i].size.y};
			if (ImGui::DragFloat2("Size", s)) {
				buttons_[i].size = {s[0], s[1]};
			}

			ImGui::Text("Entity Status: %s", (buttons_[i].entity == entt::null) ? "Null" : "Linked");
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
#endif
}

std::string InstallationManager::SerializeParameters() {
	json j;
	json btns = json::array();
	for (int i = 0; i < 5; ++i) {
		const auto& btn = buttons_[i];
		json b;
		b["name"] = btn.name;
		b["texturePath"] = btn.texturePath;
		b["prefabPath"] = btn.prefabPath;
		b["cost"] = btn.cost;
		b["posX"] = btn.pos.x;
		b["posY"] = btn.pos.y;
		b["sizeX"] = btn.size.x;
		b["sizeY"] = btn.size.y;
		btns.push_back(b);
	}
	j["buttons"] = btns;
	return j.dump();
}

void InstallationManager::DeserializeParameters(const std::string& data) {
	if (data.empty() || data == "{}")
		return;
	try {
		json j = json::parse(data);
		if (j.contains("buttons") && j["buttons"].is_array()) {
			int totalSerialized = static_cast<int>(j["buttons"].size());
			int idx = (totalSerialized <= 6) ? 1 : 0; // Old format logic
			for (const auto& b : j["buttons"]) {
				if (idx >= 5)
					break;
				auto& btn = buttons_[idx++];
				// ★バグ回避: ボタンの名前、テクスチャ、プレハブパスは、
				// コード上の定義（コンストラクタ）を絶対的に保護するため、JSONからのロードによる上書きを禁止する！
				// これにより、名前の重複によるUIエンティティの紐づけバグを完全に解消します。
				btn.cost = b.value("cost", btn.cost);
				btn.pos.x = b.value("posX", btn.pos.x);
				btn.pos.y = b.value("posY", btn.pos.y);
				btn.size.x = b.value("sizeX", btn.size.x);
				btn.size.y = b.value("sizeY", btn.size.y);
			}
		}
	} catch (const std::exception& e) {
		(void)e;
	}
}

bool InstallationManager::IsButtonPressed(const std::string& prefabPath) {
	if (!instance_)
		return false;
	for (int i = 0; i < 5; ++i) {
		if (instance_->buttons_[i].prefabPath == prefabPath)
			return instance_->buttons_[i].isPressed;
	}
	return false;
}

int InstallationManager::GetCost(const std::string& prefabPath) {
	if (!instance_)
		return 0;
	for (int i = 0; i < 5; ++i) {
		if (instance_->buttons_[i].prefabPath == prefabPath)
			return instance_->buttons_[i].cost;
	}
	return 0;
}

bool InstallationManager::IsButtonPressedByName(const std::string& name) {
	if (!instance_)
		return false;
	for (int i = 0; i < 5; ++i) {
		if (instance_->buttons_[i].name == name)
			return instance_->buttons_[i].isPressed;
	}
	return false;
}

bool InstallationManager::IsManagedButton(entt::entity entity) {
	if (!instance_)
		return false;
	for (int i = 0; i < 5; ++i) {
		if (instance_->buttons_[i].entity == entity)
			return true;
	}
	return false;
}

void InstallationManager::EnsureButtonEntity(ButtonData& data, GameScene* scene) {
	if (!scene)
		return;
	auto& registry = scene->GetRegistry();

	if (registry.valid(data.entity))
		return;

	data.entity = scene->FindObjectByName(data.name);
	if (registry.valid(data.entity))
		return;

	// 新規作成
	data.entity = scene->CreateEntity(data.name);
	auto& rect = registry.emplace<RectTransformComponent>(data.entity);
	rect.pos = data.pos;
	rect.size = data.size;
	rect.anchor = {0.0f, 0.0f}; // 絶対座標指定にするため 0,0
	rect.pivot = {0.0f, 0.0f};  // 左上基準

	auto& img = registry.emplace<UIImageComponent>(data.entity);
	img.texturePath = data.texturePath;
	img.layer = 10; // 前面に
	if (scene->GetRenderer()) {
		img.textureHandle = scene->GetRenderer()->LoadTexture2D(data.texturePath);
	}
	registry.emplace<UIButtonComponent>(data.entity);
}

REGISTER_SCRIPT(InstallationManager);
REGISTER_SCRIPT(InstallationButton);

} // namespace Game
