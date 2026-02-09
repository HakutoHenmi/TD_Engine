#include "SelectScene.h"
// ★追加: Renderer機能を使うためにインクルード
#include "Renderer.h"

namespace Game {

void SelectScene::Initialize(Engine::WindowDX*) {
	end_ = false;
	next_.clear();
	for (int i = 0; i < 256; ++i)
		prevKey_[i] = GetAsyncKeyState(i);

	// ★修正: ローカル変数 renderer を使用するように統一
	auto* renderer = Engine::Renderer::GetInstance();
	if (renderer) {
		// 環境光
		renderer->SetAmbientColor({0.5f, 0.5f, 0.5f});

		// 太陽光 (真上から強めに)
		renderer->SetDirectionalLight({0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, true);

		// ポストプロセスは一旦OFF
		renderer->SetPostProcessEnabled(false);
	}
}

void SelectScene::Update() {
	// Enter → Game
	if (WasPressed_(VK_RETURN)) {
		end_ = true;
		next_ = "Game";
	}
}

void SelectScene::Draw() {
	// 現在描画するものはありません
}

} // namespace Game