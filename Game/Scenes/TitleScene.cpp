#include "TitleScene.h"
#include "SceneManager.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"

namespace Game {

void TitleScene::Initialize(Engine::WindowDX* dx) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
}

void TitleScene::Update() {
	// Simple logic to switch to Game scene
}

void TitleScene::Draw() {
	// Draw nothing or a simple background for now
	// Renderer clears screen automatically
	
	// ★ 追加：以前のシーン（GameScene等）で変更されたViewportを元（全体）に戻す
	if (renderer_) {
		renderer_->ResetGameViewport();
	}
}

void TitleScene::DrawEditor() {
#ifdef _DEBUG
	// Call common editor UI
	// We might want to pass 'this' if TitleScene needs specific inspection, 
	// but for now EditorUI logic is global-ish or handles the active scene.
	// Since EditorUI::Show takes a GameScene*, we might need to refactor EditorUI 
	// to take a generic IScene* or just handle the global editor state.
	// For now, let's just make sure we can switch scenes.
	
	ImGui::Begin("Title Menu");
	if (ImGui::Button("Start Game")) {
		Engine::SceneManager::GetInstance()->Change("Game");
	}
	ImGui::End();
#endif
}

} // namespace Game
