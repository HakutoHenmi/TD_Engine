#include "TitleScene.h"
#include "SceneManager.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"

namespace Game {

void TitleScene::Initialize(Engine::WindowDX* dx) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	// ★追加: 明示的にプロジェクションを設定 (1920x1080のアスペクト比)
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
}

#include "../Engine/Input.h"
#include <dinput.h>

void TitleScene::Update() {
	// Simple logic to switch to Game scene
	bool isSpacePressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	if (isSpacePressed) {
		Engine::SceneManager::GetInstance()->Change("Game");
	}
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
	ImGui::Begin("Title Menu");
	if (ImGui::Button("Start Game")) {
		Engine::SceneManager::GetInstance()->Change("Game");
	}
	ImGui::Text("Press SPACE to start");
	ImGui::End();
}

} // namespace Game
