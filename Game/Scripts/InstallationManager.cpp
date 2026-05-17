#include "InstallationManager.h"
#include "Scenes/GameScene.h"
#include "ObjectTypes.h"
#include "PhaseSystemScript.h"
#include "ScriptEngine.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#include "Editor/EditorUI.h"
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

	// ★ここでパスを直接設定してください（ImGuiでの入力によるクラッシュを回避）
	prefabPaths_[0] = "Resources/Prefabs/BulletTank.prefab";
	texPaths_[0] = "Resources/Textures/Button/TankButton.png";
	buttons_[0].name = "TankButton";
	buttons_[0].cost = 100;

	prefabPaths_[1] = "Resources/Prefabs/Pipe.prefab";
	texPaths_[1] = "Resources/Textures/Button/PipeButton.png";
	buttons_[1].name = "PipeButton";
	buttons_[1].cost = 5;

	prefabPaths_[2] = "Resources/Prefabs/Canon.prefab";
	texPaths_[2] = "Resources/Textures/Button/CannonButton.png";
	buttons_[2].name = "CannonButton";
	buttons_[2].cost = 150;

	prefabPaths_[3] = "Resources/Prefabs/Missile.prefab";
	texPaths_[3] = "Resources/Textures/Button/MissileButton.png";
	buttons_[3].name = "MissikeButton";
	buttons_[3].cost = 200;

	prefabPaths_[4] = "Resources/Prefabs/Poison.prefab";
	texPaths_[4] = "Resources/Textures/Button/PisonTrapButton.png";
	buttons_[4].name = "PoisonTrapButton";
	buttons_[4].cost = 120;

	prefabPaths_[5] = "Resources/Prefabs/IceCanon.prefab";
	texPaths_[5] = "Resources/Textures/Button/IceCannonButton.png";
	buttons_[5].name = "IceCannonButton";
	buttons_[5].cost = 250;
}

void InstallationManager::Start(entt::entity /*entity*/, GameScene* scene) {
	instance_ = this;
	currentScene_ = scene;
	if (!scene) return;

	auto& registry = scene->GetRegistry();
	for (int i = 0; i < 6; ++i) {
		auto& btn = buttons_[i];
		if (btn.name.empty()) {
			btn.name = "Button_" + std::to_string(i);
		}
		if (btn.texturePath.empty()) {
			btn.texturePath = "Resources/Textures/white1x1.png";
		}

		entt::entity found = scene->FindObjectByName(btn.name);
		if (registry.valid(found)) {
			btn.entity = found;
		}
	}
}

void InstallationManager::Update(entt::entity /*entity*/, GameScene* scene, float dt) {
	(void)dt;
	if (!scene) return;
	currentScene_ = scene;
	instance_ = this;

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

	for (int i = 0; i < 6; ++i) {
		auto& btn = buttons_[i];

		// パスの同期（コードで設定した変数から反映）
		btn.texturePath = texPaths_[i];
		btn.prefabPath = prefabPaths_[i];

		if (!registry.valid(btn.entity)) {
			EnsureButtonEntity(btn, scene);
			if (!registry.valid(btn.entity)) continue;
		}

		// サイズと位置の自動設定（6ボタンを画面下部に綺麗に横並びにする）
		btn.size = { 180.0f, 180.0f };
		btn.pos.x = (i - 2.5f) * 210.0f;
		btn.pos.y = 400.0f;

		// 状態の更新
		if (registry.all_of<UIButtonComponent>(btn.entity)) {
			btn.isPressed = registry.get<UIButtonComponent>(btn.entity).isPressed;
		}

		// テクスチャの同期
		if (registry.all_of<UIImageComponent>(btn.entity)) {
			auto& img = registry.get<UIImageComponent>(btn.entity);
			if (img.texturePath != btn.texturePath) {
				img.texturePath = btn.texturePath;
				if (scene->GetRenderer()) {
					img.textureHandle = scene->GetRenderer()->LoadTexture2D(btn.texturePath);
				}
			}
		}

		// RectTransformの同期
		if (registry.all_of<RectTransformComponent>(btn.entity)) {
			auto& rect = registry.get<RectTransformComponent>(btn.entity);
			rect.pos = btn.pos;
			rect.size = btn.size;
			rect.anchor = { 0.5f, 0.5f };
			rect.pivot = { 0.5f, 0.5f };
		}

		// フェーズに応じた表示・非表示の管理（ページ制限を廃止）
		bool enabled = ((!scene->IsPlaying()) || (currentPhase == PhaseSystemScript::PreparationPhase));

		if (registry.all_of<UIImageComponent>(btn.entity))
			registry.get<UIImageComponent>(btn.entity).enabled = enabled;
		if (registry.all_of<UIButtonComponent>(btn.entity))
			registry.get<UIButtonComponent>(btn.entity).enabled = enabled;
		if (registry.all_of<RectTransformComponent>(btn.entity))
			registry.get<RectTransformComponent>(btn.entity).enabled = enabled;
	}
}

void InstallationManager::Draw(entt::entity /*entity*/, GameScene* /*scene*/) {
	// UISystem が RectTransformComponent を通じて描画するため、
	// ここでの二重描画は不要。エディタでも UISystem は動作する。
}

void InstallationManager::DrawUI(entt::entity /*entity*/, GameScene* scene) {
	(void)scene;
	// ページ情報の表示を削除
}

void InstallationManager::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::SeparatorText("Installation Manager");
	
	ImGui::Text("Fixed 6 Buttons Mode");
	ImGui::Separator();

	for (int i = 0; i < 6; ++i) {
		ImGui::PushID(static_cast<int>(i));
		std::string label = buttons_[i].name + " (##" + std::to_string(i) + ")";
		if (ImGui::TreeNode(label.c_str())) {
			char nameBuf[1024] = { 0 };
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
	for (int i = 0; i < 6; ++i) {
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
	if (data.empty() || data == "{}") return;
	try {
		json j = json::parse(data);
		if (j.contains("buttons") && j["buttons"].is_array()) {
			int idx = 0;
			for (const auto& b : j["buttons"]) {
				if (idx >= 6) break;
				auto& btn = buttons_[idx++];
				btn.name = b.value("name", "");
				btn.texturePath = b.value("texturePath", "");
				btn.prefabPath = b.value("prefabPath", "");
				btn.cost = b.value("cost", 0);
				btn.pos.x = b.value("posX", 0.0f);
				btn.pos.y = b.value("posY", 0.0f);
				btn.size.x = b.value("sizeX", 200.0f);
				btn.size.y = b.value("sizeY", 200.0f);
			}
		}
	} catch (const std::exception& e) {
		(void)e;
	}
}

bool InstallationManager::IsButtonPressed(const std::string& prefabPath) {
	if (!instance_) return false;
	for (int i = 0; i < 6; ++i) {
		if (instance_->buttons_[i].prefabPath == prefabPath) return instance_->buttons_[i].isPressed;
	}
	return false;
}

int InstallationManager::GetCost(const std::string& prefabPath) {
	if (!instance_) return 0;
	for (int i = 0; i < 6; ++i) {
		if (instance_->buttons_[i].prefabPath == prefabPath) return instance_->buttons_[i].cost;
	}
	return 0;
}

bool InstallationManager::IsButtonPressedByName(const std::string& name) {
	if (!instance_) return false;
	for (int i = 0; i < 6; ++i) {
		if (instance_->buttons_[i].name == name) return instance_->buttons_[i].isPressed;
	}
	return false;
}

bool InstallationManager::IsManagedButton(entt::entity entity) {
	if (!instance_) return false;
	for (int i = 0; i < 6; ++i) {
		if (instance_->buttons_[i].entity == entity) return true;
	}
	return false;
}

void InstallationManager::EnsureButtonEntity(ButtonData& data, GameScene* scene) {
	if (!scene) return;
	auto& registry = scene->GetRegistry();

	if (registry.valid(data.entity)) return;

	data.entity = scene->FindObjectByName(data.name);
	if (registry.valid(data.entity)) return;

	// 新規作成
	data.entity = scene->CreateEntity(data.name);
	auto& rect = registry.emplace<RectTransformComponent>(data.entity);
	rect.pos = data.pos;
	rect.size = data.size;
	rect.anchor = { 0.0f, 0.0f }; // 絶対座標指定にするため 0,0
	rect.pivot = { 0.0f, 0.0f };  // 左上基準

	auto& img = registry.emplace<UIImageComponent>(data.entity);
	img.texturePath = data.texturePath;
	img.layer = 10; // 前面に
	if (scene->GetRenderer()) {
		img.textureHandle = scene->GetRenderer()->LoadTexture2D(data.texturePath);
	}
	registry.emplace<UIButtonComponent>(data.entity);
}

REGISTER_SCRIPT(InstallationManager);

} // namespace Game
