#include "InstallationButton.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include "PhaseSystemScript.h"
#include <cmath>
#include <iostream>

#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#include "Editor/EditorUI.h"
#endif

#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;

namespace Game {

InstallationButton::InstallationButton() {}

void InstallationButton::Start(entt::entity /*entity*/, GameScene* scene) {
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

	for (auto& btn : buttons_) {
		if (!btn.texturePath.empty()) {
			btn.textureHandle = renderer->LoadTexture2D(btn.texturePath);
		}
	}
	
	arrowTexHandle_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
}

void InstallationButton::Update(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {
}

void InstallationButton::DrawUI(entt::entity /*entity*/, GameScene* scene) {
	if (buttons_.empty() || PhaseSystemScript::IsPhase() != PhaseSystemScript::PreparationPhase) return;

	auto* renderer = scene->GetRenderer();
	auto* input = Engine::Input::GetInstance();
	if (!renderer || !input) return;

	float sw = (float)Engine::WindowDX::kW;
	float sh = (float)Engine::WindowDX::kH;

	float mx = 0, my = 0;
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImVec2 mousePos = ImGui::GetMousePos();
	ImVec2 gameMin = EditorUI::GetGameImageMin();
	ImVec2 gameMax = EditorUI::GetGameImageMax();
	float viewW = gameMax.x - gameMin.x;
	float viewH = gameMax.y - gameMin.y;
	if (viewW > 0.0f && viewH > 0.0f) {
		mx = (mousePos.x - gameMin.x) * (sw / viewW);
		my = (mousePos.y - gameMin.y) * (sh / viewH);
	}
#else
	input->GetMousePos(mx, my);
#endif

	int startIdx = currentPage_ * 6;
	int endIdx = std::min((int)buttons_.size(), startIdx + 6);
	int numOnPage = endIdx - startIdx;

	float totalW = numOnPage * kButtonSize + (numOnPage - 1) * kButtonSpacing;
	float startX = (sw - totalW) * 0.5f;
	float startY = sh - kMarginBottom - kButtonSize;

	for (int i = 0; i < numOnPage; ++i) {
		int btnIdx = startIdx + i;
		auto& btn = buttons_[btnIdx];

		float bx = startX + i * (kButtonSize + kButtonSpacing);
		float by = startY;

		bool hovered = (mx >= bx && mx <= bx + kButtonSize && my >= by && my <= by + kButtonSize);
		
		Engine::Renderer::SpriteDesc desc;
		desc.x = bx;
		desc.y = by;
		desc.w = kButtonSize;
		desc.h = kButtonSize;
		desc.color = hovered ? Engine::Vector4{1.2f, 1.2f, 1.2f, 1.0f} : Engine::Vector4{1, 1, 1, 1};

		if (btn.textureHandle == 0 && !btn.texturePath.empty()) {
			btn.textureHandle = renderer->LoadTexture2D(btn.texturePath);
		}

		renderer->DrawSprite(btn.textureHandle, desc);

		if (hovered && input->IsMouseTrigger(0)) {
			json data;
			data["prefab"] = btn.prefabPath;
			data["cost"] = btn.cost;
			data["isPipe"] = btn.isPipe;
			EmitString(scene, "StartInstallation", data.dump());
		}
	}

	if (buttons_.size() > 6) {
		float arrowSize = 40.0f;
		float leftArrowX = startX - arrowSize - kButtonSpacing;
		float rightArrowX = startX + totalW + kButtonSpacing;
		float arrowY = startY + (kButtonSize - arrowSize) * 0.5f;

		if (currentPage_ > 0) {
			bool hL = (mx >= leftArrowX && mx <= leftArrowX + arrowSize && my >= arrowY && my <= arrowY + arrowSize);
			Engine::Renderer::SpriteDesc sL;
			sL.x = leftArrowX; sL.y = arrowY; sL.w = arrowSize; sL.h = arrowSize;
			sL.color = hL ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{1, 1, 1, 1};
			renderer->DrawSprite(arrowTexHandle_, sL);
			if (hL && input->IsMouseTrigger(0)) currentPage_--;
		}

		if (endIdx < (int)buttons_.size()) {
			bool hR = (mx >= rightArrowX && mx <= rightArrowX + arrowSize && my >= arrowY && my <= arrowY + arrowSize);
			Engine::Renderer::SpriteDesc sR;
			sR.x = rightArrowX; sR.y = arrowY; sR.w = arrowSize; sR.h = arrowSize;
			sR.color = hR ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{1, 1, 1, 1};
			renderer->DrawSprite(arrowTexHandle_, sR);
			if (hR && input->IsMouseTrigger(0)) currentPage_++;
		}
	}
}

void InstallationButton::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void InstallationButton::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	if (ImGui::Button("Add Button")) {
		buttons_.push_back({"New Button", "", "", 0, false, 0});
	}

	for (int i = 0; i < (int)buttons_.size(); ++i) {
		ImGui::PushID(i);
		if (ImGui::CollapsingHeader(buttons_[i].name.empty() ? "Button" : buttons_[i].name.c_str())) {
			char nameBuf[128];
			strncpy_s(nameBuf, buttons_[i].name.c_str(), sizeof(nameBuf));
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) buttons_[i].name = nameBuf;

			char texBuf[256];
			strncpy_s(texBuf, buttons_[i].texturePath.c_str(), sizeof(texBuf));
			if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
				buttons_[i].texturePath = texBuf;
				if (auto* r = Engine::Renderer::GetInstance()) {
					buttons_[i].textureHandle = r->LoadTexture2D(buttons_[i].texturePath);
				}
			}

			char prefBuf[256];
			strncpy_s(prefBuf, buttons_[i].prefabPath.c_str(), sizeof(prefBuf));
			if (ImGui::InputText("Prefab Path", prefBuf, sizeof(prefBuf))) buttons_[i].prefabPath = prefBuf;

			ImGui::InputInt("Cost", &buttons_[i].cost);
			ImGui::Checkbox("Is Pipe", &buttons_[i].isPipe);

			if (ImGui::Button("Remove")) {
				buttons_.erase(buttons_.begin() + i);
				ImGui::PopID();
				break;
			}
		}
		ImGui::PopID();
	}
#endif
}

std::string InstallationButton::SerializeParameters() {
	json root;
	json btnArray = json::array();
	for (const auto& btn : buttons_) {
		json b;
		b["name"] = btn.name;
		b["texture"] = btn.texturePath;
		b["prefab"] = btn.prefabPath;
		b["cost"] = btn.cost;
		b["isPipe"] = btn.isPipe;
		btnArray.push_back(b);
	}
	root["buttons"] = btnArray;
	return root.dump();
}

void InstallationButton::DeserializeParameters(const std::string& data) {
	try {
		json root = json::parse(data);
		if (root.contains("buttons") && root["buttons"].is_array()) {
			buttons_.clear();
			for (const auto& b : root["buttons"]) {
				ButtonData btn;
				btn.name = b.value("name", "");
				btn.texturePath = b.value("texture", "");
				btn.prefabPath = b.value("prefab", "");
				btn.cost = b.value("cost", 0);
				btn.isPipe = b.value("isPipe", false);
				buttons_.push_back(btn);
			}
		}
	} catch (...) {}
}

REGISTER_SCRIPT(InstallationButton);

} // namespace Game