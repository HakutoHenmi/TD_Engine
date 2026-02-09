#include "GameOverScene.h"

namespace Game {

void GameOverScene::Initialize(Engine::WindowDX*) {
	end_ = false;
	next_.clear();
	for (int i = 0; i < 256; ++i)
		prevKey_[i] = GetAsyncKeyState(i);
}

void GameOverScene::Update() {
	// Enter → Title
	if (WasPressed_(VK_RETURN)) {
		end_ = true;
		next_ = "Title";
	}
}

void GameOverScene::Draw() {}

} // namespace Game
